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
	static const intptr_t DefaultIdleCount = 8;
public:
	static RingBuffer<T>* Create(intptr_t count, intptr_t idle_count = DefaultIdleCount) {
		unsigned char* address = (unsigned char*)
			malloc(sizeof(RingBuffer<T>) + count * sizeof(T) + count * sizeof(intptr_t));
		if (address != nullptr) 
			address = (unsigned char*)(new ((RingBuffer<T>*)address) RingBuffer<T>(address, count, idle_count));
		return (RingBuffer<T>*)address;
	}
public:
	RingBuffer(unsigned char* address, intptr_t count, intptr_t idle_count = DefaultIdleCount)
		: used(0)
		, pbuffer(0)
		, pflags_(0)
		, idle_count(idle_count)
		, capacity_mask(count - 1)
		, send_index()
		, receive_index()
	{
		// 确保count是2的整数次幂
		assert((count & (count - 1)) == 0 && "count must be a power of 2");
		memset(address + (this->pbuffer = sizeof(RingBuffer<T>)), 0, count * sizeof(T));
		memset(address + (this->pflags_ = sizeof(RingBuffer<T>) + count * sizeof(T)), 0, count * sizeof(intptr_t));
	}

	~RingBuffer() {}

public:
	// 发送数据到缓冲区
	intptr_t Send(const T& value) {
		// 原子操作当前的发送位置，保证不管多个进程还是多个线程都在缓存中找到唯一位置
		intptr_t i = send_index.fetch_add(1, std::memory_order_seq_cst);
		((T*)((unsigned char*)this + this->pbuffer))[i & this->capacity_mask] = value;
		//原子操作将全1写入pflags[i]
#ifdef _WIN32
#ifdef _WIN64
		_interlockedor64((intptr_t*)&((unsigned char*)this + this->pflags_)[i & capacity_mask], ~0LL);
#else
		_interlockedOr((intptr_t*)&((unsigned char*)this + this->pflags_)[i & capacity_mask], ~0);
#endif
#else
		__sync_lock_test_and_set(&((unsigned char*)this + this->pflags_)[i & capacity_mask], ~0);
#endif
		return i;
	}

public:
	// 等待并从缓冲区中获取数据
	intptr_t Wait(T& value) {
		int ic = 0;
		intptr_t ir = receive_index.load(std::memory_order_seq_cst);
#ifdef _WIN32
#ifdef _WIN64
		while (0 != _InterlockedCompareExchange64(&(((volatile __int64*)((unsigned char*)this + pflags_))[ir & this->capacity_mask]), ~0, 0))
#else
		while (0 != _InterlockedCompareExchange(&((unsigned char*)this + pflags_)[ir & this->capacity_mask], ~0, 0))
#endif
#else
		while (!__sync_bool_compare_and_swap(&((unsigned char*)this + pflags_)[ir & this->capacity_mask], ~0, 0))
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
		return ir;
	}

protected:
	intptr_t used;
	intptr_t pbuffer;
	intptr_t pflags_;
	intptr_t idle_count;
	std::atomic_intptr_t send_index;
	std::atomic_intptr_t receive_index;
	const intptr_t capacity_mask;
};

struct Reference {
public:
	void* Ptr = nullptr;
	intptr_t _Size = 0;
	intptr_t Count = 0;
public:
	Reference(
		void* Ptr = nullptr,
		intptr_t _Size = 0,
		intptr_t Count = 1)
	{
		this->Ptr = Ptr;
		this->_Size = _Size;
		this->Count = Count;
	}

	void Dispose() {
		if (this->Ptr != nullptr) {
			free(this->Ptr);
			this->Ptr = nullptr;
		}
		this->Count = 0;
		this->_Size = 0;
	}
};

class ReferenceAllocator
	: public RingBuffer<Reference> {
protected:
	class Storage {
	public:
		Storage(ReferenceAllocator* allocator = nullptr) 
			: allocator(allocator) {}
		ReferenceAllocator* bind(ReferenceAllocator* allocator) { return this->allocator = allocator;}
		ReferenceAllocator* get() const { return this->allocator; }
	public:
		~Storage() {
			if (this->allocator != nullptr) {
				delete this->allocator;
				this->allocator = nullptr;
			}
		}
	protected:
		ReferenceAllocator* allocator;
	};
protected:
	static Storage storage;
public:
	static const intptr_t DefaultAllocatingSize = 4096;
	static ReferenceAllocator* Default;
public:
	static ReferenceAllocator* Create(intptr_t count, intptr_t idle_count = DefaultIdleCount) {
		unsigned char* address = (unsigned char*)
			malloc(sizeof(ReferenceAllocator) + count * sizeof(Reference) + count * sizeof(intptr_t));
		if (address != nullptr) {
			address = (unsigned char*)(new ((ReferenceAllocator*)address)
				ReferenceAllocator(address, count, idle_count));
		}
		return (ReferenceAllocator*)address;
	}
public:
	ReferenceAllocator(unsigned char* address, intptr_t count, intptr_t idle_count = DefaultIdleCount)
		: RingBuffer<Reference>(address, count, idle_count) {}
public:
	intptr_t GetUsedCount() const { return this->used; }
public:
	intptr_t Allocate(intptr_t size) {
		if (this->used > this->capacity_mask) return -1LL;
		void* ptr = malloc(size);
		intptr_t p = -1LL;
		if (ptr != nullptr) {
			p = this->Send(Reference(ptr, size));
		}
		return p;
	}
public:
	intptr_t AddRef(void* ptr) {
		if (this->used > this->capacity_mask) return -1LL;
		intptr_t p = -1LL;
		if (ptr != nullptr) {
			p = this->Send(Reference(ptr));
		}
		return p;
	}
	intptr_t AddRef(intptr_t handle) {
		if (handle >= 0 && handle < this->used) {
			Reference& reference = ((Reference*)((unsigned char*)this + this->pbuffer))[handle & this->capacity_mask];
#ifdef _WIN32
#ifdef _WIN64
			intptr_t count = _InterlockedIncrement64(&reference.Count);
			_interlockedincrement64(&this->used);
#else
			intptr_t count = _InterlockedIncrement(&reference.Count);
			_interlockedincrement(&this->used);
#endif
#else
			intptr_t count = __sync_add_and_fetch(&reference.Count, 1);
			__sync_add_and_fetch(&this->used, 1);
#endif
			return count;
		}
		return -1LL;
	}
public:
	intptr_t Release(intptr_t handle) {
		Reference& reference = ((Reference*)((unsigned char*)this + this->pbuffer))[handle & this->capacity_mask];
		if (reference.Count == 0) return -1LL;

#ifdef _WIN32
#ifdef _WIN64
		intptr_t count = _interlockeddecrement64(&reference.Count);
		_interlockeddecrement64(&this->used);
#else
		intptr_t count = _interlockeddecrement(&reference.Count);
		_interlockeddecrement(&this->used);
#endif
#else
		intptr_t count = __sync_add_and_fetch(&reference.Count, -1);
		__sync_add_and_fetch(&this->used, -1);
#endif
		return count;
	}

public:
	intptr_t TryCollectOnce() {
		Reference reference;
		intptr_t handle = this->Wait(reference);
		if (reference.Count == 0) {
			reference.Dispose();
#ifdef _WIN32
#ifdef _WIN64
			InterlockedAnd64((intptr_t*)&((unsigned char*)this + this->pflags_)[handle & this->capacity_mask], 0);
#else
			InterlockedAnd((intptr_t*)&((unsigned char*)this + this->pflags_)[handle & this->capacity_mask], 0);
#endif
#else
			__sync_lock_test_and_set(&((unsigned char*)this + this->pflags_)[i & this->capacity_mask], 0);
#endif
			return handle;
		}
		return -1LL;
	}
};
#endif
