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

// ���廷�λ�����ģ����
template<typename T>
class RingBuffer {
public:
    static const int DefaultIdleCount = 8;
public:
    static RingBuffer<T>* Create(size_t count, int idle_count = DefaultIdleCount) {
        unsigned char* address = (unsigned char*)
            malloc(sizeof(RingBuffer<T>) + count * sizeof(T) + count * sizeof(unsigned char));
       
        if (address != nullptr) {
            address = (unsigned char*)(new ((RingBuffer<T>*)address) RingBuffer<T>(address, count, idle_count));
        }
        return (RingBuffer<T>*)address;
    }

public:

    RingBuffer(unsigned char* address, size_t count, int idle_count = DefaultIdleCount)
        : pbuffer(0)
        , pflags(0)
        , idle_count(idle_count)
        , capacity_mask(~count)
        , send_index()
        , receive_index() {
        // ȷ��size��2����������
        assert((count & (count - 1)) == 0 && "Size must be a power of 2");
        memset(address + (this->pbuffer = sizeof(RingBuffer<T>)), 0, count * sizeof(T));
        memset(address + (this->pflags = sizeof(RingBuffer<T>) + count * sizeof(T)), 0, count * sizeof(unsigned char));
    }

    ~RingBuffer() {}

public:
    // �������ݵ�������
    void Send(const T& value) {
        // ԭ�Ӳ�����ǰ�ķ���λ�ã���֤���ܶ�����̻��Ƕ���̶߳��ڻ������ҵ�Ψһλ��
        size_t i = send_index.fetch_add(1, std::memory_order_seq_cst);
        ((T*)((unsigned char*)this + pbuffer))[i & capacity_mask] = value;
        //ԭ�Ӳ�����ȫ1д��pflags[i]
        __sync_lock_test_and_set(&((unsigned char*)this + pflags)[i & capacity_mask], 0xff);
    }

    // �ȴ����ӻ������л�ȡ����
    void Wait(T& value) {
        int ic = 0;
        size_t ir = receive_index.load(std::memory_order_seq_cst);
        while (!__sync_bool_compare_and_swap(&((unsigned char*)this + pflags)[ir & capacity_mask], 0xff, 0)) {
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
        ir &= capacity_mask;
        receive_index.store(ir, std::memory_order_seq_cst);
        value = ((T*)((unsigned char*)this + pbuffer))[ir];
    }

private:
    size_t pbuffer;
    size_t pflags;
    int idle_count;
    std::atomic_size_t send_index;
    std::atomic_size_t receive_index;
    const size_t capacity_mask;
};

#endif
