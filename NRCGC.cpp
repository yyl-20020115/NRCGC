#include "NRCGC.h"

int main()
{
	RingBuffer<void*>* buffer = RingBuffer<void*>::Create(256,8);
	if (buffer != nullptr) {

		delete buffer;
	}
	return 0;
}
