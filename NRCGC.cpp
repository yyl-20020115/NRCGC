#include "Allocator.h"

ReferenceAllocator::Storage ReferenceAllocator::storage;
ReferenceAllocator* ReferenceAllocator::Default
	= ReferenceAllocator::storage.bind(Create(DefaultAllocatingSize, DefaultIdleCount));

