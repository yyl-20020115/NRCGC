#ifndef __GCRC_H__
#define __GCRC_H__
#include "Allocator.h"

template<class PTR>
class gcrc_ptr
{
public:
	gcrc_ptr(const gcrc_ptr& src) :gcrc_ptr(src._ptr) { }
	gcrc_ptr(const PTR* ptr = nullptr) :_ptr(ptr), _handle(-1)
	{
		this->_handle = ReferenceAllocator::Default->AddRef(ptr);
	}
public:
	~gcrc_ptr() {
		if (this->_handle >= 0) ReferenceAllocator::Default->Release(this->_handle);
		this->_handle = ~0;
		this->_ptr = nullptr;
	}
public:
	gcrc_ptr& operator = (gcrc_ptr<PTR>& src) {
		if (this->_ptr == src._ptr) {
		}
		else if (src._ptr != nullptr) {
			if (this->_handle >= 0) ReferenceAllocator::Default->Release(this->_handle);
			this->_handle = ReferenceAllocator::Default->AddRef(this->_ptr = src._ptr);
		}
		return *this;
	}
public:
	operator bool() const {
		return this->_ptr != nullptr;
	}
	PTR& operator*() const {
		return *this->_ptr;
	}
	PTR* operator->() const {
		return this->_ptr;
	}
	PTR* get() const {
		return this->_ptr;
	}
protected:
	PTR* _ptr = nullptr;
	intptr_t _handle = ~0;
};




#endif