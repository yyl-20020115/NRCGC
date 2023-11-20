#include "Allocator.h"

ReferenceAllocator::Storage ReferenceAllocator::storage;
ReferenceAllocator* ReferenceAllocator::Default
	= ReferenceAllocator::storage.Bind(Create(DefaultAllocatingSize, DefaultIdleCount));

