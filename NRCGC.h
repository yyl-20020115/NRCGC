#ifndef __NRCGC_H__
#define __NRCGC_H__

#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <cassert>
#include <sys/types.h>
#include <malloc.h>
#ifdef _WIN32
#include <Windows.h>
#include <processthreadsapi.h>
#else
#include <sched.h>
#endif

// 定义环形缓冲区模板类
template<typename T>
class RingBuffer {
public:
    static const int DefaultIdleCount = 8;
public:
    static RingBuffer<T>* Create(size_t count, int idle_count = DefaultIdleCount) {
        unsigned char* address = (unsigned char*)
            malloc(sizeof(RingBuffer<T>) + count * sizeof(T) + count * sizeof(int));
        if (address != nullptr) {
            address = (unsigned char*)(new ((RingBuffer<T>*)address) RingBuffer<T>(address, count, idle_count));
        }
        return (RingBuffer<T>*)address;
    }

public:
    RingBuffer(unsigned char* address, size_t count, int idle_count = DefaultIdleCount)
        : pbuffer(0)
        , pflags_(0)
        , idle_count(idle_count)
        , capacity_mask(~count)
        , send_index()
        , receive_index() {
        // 确保size是2的整数次幂
        assert((count & (count - 1)) == 0 && "Size must be a power of 2");
        memset(address + (this->pbuffer = sizeof(RingBuffer<T>)), 0, count * sizeof(T));
        memset(address + (this->pflags_ = sizeof(RingBuffer<T>) + count * sizeof(T)), 0, count * sizeof(int));
    }

    ~RingBuffer() {}

public:
    // 发送数据到缓冲区
    size_t Send(const T& value) {
        // 原子操作当前的发送位置，保证不管多个进程还是多个线程都在缓存中找到唯一位置
        size_t i = send_index.fetch_add(1, std::memory_order_seq_cst);
        ((T*)((unsigned int*)this + pbuffer))[i & this->capacity_mask] = value;
        //原子操作将全1写入pflags[i]
#ifdef _WIN32
        InterlockedOr((long*)&((int*)this + this->pflags_)[i & capacity_mask], 0xffffffff);
#else
        __sync_lock_test_and_set(&((int*)this + this->pflags_)[i & capacity_mask], 0xffffffff);
#endif
        return i;
    }

    // 等待并从缓冲区中获取数据
    void Wait(T& value) {
        int ic = 0;
        size_t ir = receive_index.load(std::memory_order_seq_cst);
#ifdef _WIN32
        while (0!=_InterlockedCompareExchange(&((unsigned long*)this + pflags_)[ir & this->capacity_mask], 0xffffffff, 0))
#else
        while (!__sync_bool_compare_and_swap(&((unsigned int*)this + pflags_)[ir & this->capacity_mask], 0xffffffff, 0))
#endif   
        {
            ir++;
            ic++;
            if (ic >= this->idle_count) {
#ifdef _WIN32
                SwitchToThread();
#else
                sched_yield();
#endif
                ic = 0;
            }
        }
        ir &= this->capacity_mask;
        receive_index.store(ir, std::memory_order_seq_cst);
        value = ((T*)((unsigned char*)this + this->pbuffer))[ir];
    }

protected:
    size_t pbuffer;
    size_t pflags_;
    int idle_count;
    std::atomic_size_t send_index;
    std::atomic_size_t receive_index;
    const size_t capacity_mask;
};

struct Reference {
    void* Ptr = nullptr;
    intptr_t _Size = 0;
    intptr_t Count = 0;
};

class ReferenceRingBuffer 
    : public RingBuffer<Reference>
{
public:
    static ReferenceRingBuffer* Create(size_t count, int idle_count = DefaultIdleCount)
    {
        unsigned char* address = (unsigned char*)
            malloc(sizeof(ReferenceRingBuffer) + count * sizeof(Reference) + count * sizeof(unsigned char));

        if (address != nullptr) {
            address = (unsigned char*)(new ((ReferenceRingBuffer*)address) 
                ReferenceRingBuffer(address, count, idle_count));
        }
        return (ReferenceRingBuffer*)address;
    }
public:
    ReferenceRingBuffer(unsigned char* address, size_t count, int idle_count = DefaultIdleCount)
        : RingBuffer<Reference>(address,count,idle_count){}

public:
    size_t Allocate(size_t size) {
        void* ptr = malloc(size);
        if (ptr != nullptr) {
            Reference reference = { ptr,(intptr_t)size,1 };
            return this->Send(reference);
        }
        return ~0ULL;
    }
public:
    size_t Release(size_t handle) {
        Reference& reference =
            ((Reference*)((unsigned char*)this + this->pbuffer))[handle & this->capacity_mask];
#ifdef _WIN32
#ifdef _WIN64
        intptr_t count = _interlockeddecrement64(&reference.Count);
#else
        intptr_t count = _interlockeddecrement(&reference.Count);
#endif
#else
        intptr_t count = __sync_add_and_fetch(&reference.Count, -1);
#endif
        if (count == 0) {
            free(reference.Ptr);
            reference.Ptr = nullptr;
            reference.Count = 0;
            reference._Size = 0;
#ifdef _WIN32
            _InterlockedAnd((long*)&((int*)this + this->pflags_)[handle & this->capacity_mask], 0);
#else
            __sync_lock_test_and_set(&((long*)this + this->pflags_)[i & this->capacity_mask], 0);
#endif
        }
        return (size_t)count;
    }
};

#endif
