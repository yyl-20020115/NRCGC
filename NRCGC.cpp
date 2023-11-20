﻿#include "NRCGC.h"

ReferenceRingBuffer* buffer = nullptr;

bool testing = true;
bool collecting = true;

static void collector_function() {
	if (buffer != nullptr) {
		while (collecting) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			buffer->TryCollectOnce();
		}
	}
}
static void tester_function() {
	if (buffer != nullptr) {
		while (testing) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			intptr_t handle = buffer->Allocate(256);
			if (handle != ~0) {
				std::this_thread::sleep_for(std::chrono::seconds(1));
				buffer->Release(handle);
			}
		}
	}
}
const int MaxTesters = 4;
const int MaxCollectors = 4;
std::vector<std::thread*> threads;

int main(int argc, char* argv[])
{
	buffer = ReferenceRingBuffer::Create(256,8);
	if (buffer != nullptr) {
		//intptr_t handle = buffer->Allocate(256);
		//if (handle != ~0) {
		//	buffer->Release(handle);
		//}
		for (int i = 0; i < MaxCollectors; i++) {
			std::thread* thread = new std::thread(collector_function);
			threads.push_back(thread);
		}
		for (int i = 0; i < MaxTesters; i++) {
			std::thread* thread = new std::thread(tester_function);
			threads.push_back(thread);
		}
		testing = true;
		collecting = true;

		std::this_thread::sleep_for(std::chrono::seconds(1000));

		testing = false;
		collecting = false;

		for (auto& thread : threads) {
			thread->join();
			delete thread;
		}
		threads.clear();

		delete buffer;
	}
	return 0;
}
