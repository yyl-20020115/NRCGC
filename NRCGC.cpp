#include "NRCGC.h"

int main()
{

	auto* buffer = ReferenceRingBuffer::Create(256,8);
	if (buffer != nullptr) {

		size_t handle = buffer->Allocate(256);
		buffer->Release(handle);

		delete buffer;
	}
	return 0;
}
