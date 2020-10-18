#pragma once

#include <stdlib.h>

void mem_flushEntireDeviceMemory();

void mem_init();

//allocate graphics memory (could be VRAM or FCRAM)
void* mem_AllocateGraphicsMemory(unsigned int area, unsigned int aim, unsigned int id, int size);
void mem_DeallocateGraphicsMemory(unsigned int area, unsigned int aim, unsigned int id, void* addr);

//frees device memory
void mem_deviceFree(void* p);
//allocate device memory
void* mem_deviceMallocAligned(size_t amount, size_t align = 8);

//frees system memory
void mem_systemFree(void* p);
//allocate system memory
void* mem_systemMallocAligned(size_t amount, size_t align = 8);

void mem_printGraphicsAllocReport();
void mem_flagAllocatingRenderBuffer(bool flag);

//the 3ds ARMCC compiler doesn't include this, for some reason
char* mem_strdup(const char* str);
