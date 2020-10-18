//really needed?
#define __STDLIB_NO_EXPORTS
#define __cstdlib

#include <stdlib.h>
#include <string.h>

#include <nn/gx.h>
#include <nn/gx/CTR/gx_CommandAccess.h> //for nngxUpdateBuffer
#include <nn/fnd.h>
#include <nn/init.h>

#include "VRamAllocator.h"
#include "mem.h"
#include "lazy.h"

static uptr _device_memory_address;
static size_t _device_memory_size;

Lazy<nn::fnd::ThreadSafeExpHeap> heap;
Lazy<VRamAllocator> heap_vramA, heap_vramB;
inline nn::fnd::ThreadSafeExpHeap& g_Heap() { return *heap; }

const int ALIGNMENT_MALLOC     = 8;

class DemoAllocator : public nn::fnd::IAllocator
{
public:
	virtual void * Allocate(size_t size, s32 alignment)
	{
		return g_Heap().Allocate(size,alignment);
	}

	virtual void Free(void * p)
	{
		g_Heap().Free(p);
	}

	//TODO - this isnt supporting different alignments right now.. we may need to stash that in our own block footer, if it becomes important
	void* Reallocate(void* p, size_t size)
	{
      if( p != NULL )
      {
          // If the size is 0, it only frees
          if( size == 0 )
          {
              std::free(p);
              return NULL;
          }

          // If can be handled by changing the size of the memory block, do so
          // 
          size_t newSize = g_Heap().ResizeBlock(p, size);

          if( newSize != 0 )
          {
              return p;
          }

          // If it could not be handled, allocate and copy in addition
          void* newMem = Allocate(size, ALIGNMENT_MALLOC);

          if( newMem != NULL )
          {
              size_t oldSize =  g_Heap().GetSizeOf(p);
              std::memcpy(newMem, p, std::min(size, oldSize));
              std::free(p);
          }

          return newMem;
      }
      else
      {
          // If p is NULL, only do malloc
          return Allocate(size,ALIGNMENT_MALLOC);
      }
	}
};
DemoAllocator g_Allocator;
inline DemoAllocator& GetAllocator() { return g_Allocator; }

extern "C" void _print_free_memory()
{
	NN_LOG("total free mem: %d\n",g_Heap().GetTotalFreeSize());
	NN_LOG("allocatable mem: %d\n",g_Heap().GetAllocatableSize());
	NN_LOG("total mem: %d\n",g_Heap().GetTotalSize());
}
extern "C"{
	void* mymalloc(size_t size, size_t alignment = ALIGNMENT_MALLOC)
	{
		u32 memory = (u32)GetAllocator().Allocate(size,alignment);
		NN_ASSERT(memory);
		return (void*)memory;
	}

	// ABI ????????
	void myfree(void* p)
	{
		if (p)
		{
			GetAllocator().Free(p);
		}
	}


	void* malloc(size_t size)
	{
		return mymalloc(size);
	}

	void free(void* p)
	{
		//we know that myfree checks p for NULL so its safe to skip here
		myfree(p);
	}

	void *realloc(void *ptr, size_t size)
	{
		if(size == 0 && ptr == NULL)
		{
			myfree(ptr);
			return NULL;
		}
		
		if(ptr == NULL)
			return malloc(size);

		if(size == 0)
			return NULL;

		return GetAllocator().Reallocate(ptr,size);
	}

} // extern "C"

//multiengine requirements
void* multi_malloc(size_t size, size_t align)
{
	return mymalloc(size,align);
}
void multi_free(void* ptr)
{
	free(ptr);
}

void* operator new (size_t size, const ::std::nothrow_t&) throw()
{
	void* memory = malloc(size);
	NN_ASSERTMSG(memory, "operator new failed to allocate anything");
	return memory;
}

void* operator new[] (size_t size, const ::std::nothrow_t&) throw()
{
	return operator new(size, ::std::nothrow_t());
}

void operator delete (void* p) throw()
{
	free(p);
}

void operator delete[] (void* p) throw()
{
	operator delete(p);
}

void mem_flushEntireDeviceMemory()
{
	//is this smart enough to flush everything without touching the entire range? not sure how we could know or tell. NULL doesnt work.
	nngxUpdateBuffer((void*)_device_memory_address, _device_memory_size);
}

extern "C" void nninitStartUp()
{
	const size_t assignment   = nn::os::GetAppMemorySize();
	const size_t extraStack   = 64 * 1024; // this is needed for stack memory block allocation
	const size_t currentUsing = nn::os::GetUsingMemorySize();

	//here's how much system memory to allocate:
	const size_t system_memory_size = 4 * 1024 * 1024; 
	//for some reason, a few things need system memory.
	//all remaining memory will be device memory, which is required for many more things,		and works just fine for general game logic allocations

	const size_t total_available = assignment - currentUsing - extraStack;
	const size_t device_memory_size = total_available - system_memory_size;

	nn::os::SetupHeapForMemoryBlock(system_memory_size + extraStack);

	nn::os::SetDeviceMemorySize(device_memory_size);

	_device_memory_address = nn::os::GetDeviceMemoryAddress();
	_device_memory_size = device_memory_size;

	//setup main heap (in device memory)
	g_Heap().Initialize(_device_memory_address, _device_memory_size,  nn::os::ALLOCATE_OPTION_LINEAR);

	nn::init::InitializeAllocator(system_memory_size);

	NN_ASSERT(system_memory_size + extraStack + device_memory_size + currentUsing == assignment);

	//we didnt intend for g_Allocator to need constructing, but it seems it might, or else the vtable wont be set up
	//construct it once here, and its benign if it gets constructed later, since it doesnt have any state to clobber
	//new (&g_Allocator) DemoAllocator();
}

static void* TryVramAllocate(VRamAllocator& allocator, GLenum aim, GLsizei size, int addrAlign)
{
	allocator.m_currAim = aim;
	
	if(g_allocCounters.isAllocatingRenderBuffer)
		allocator.m_currAim = NN_GX_MEM_RENDERBUFFER;

	void* ret = allocator.Allocate(size,addrAlign);
	u32 offset = allocator.GetOffset(ret);
	u32 end = offset + size;

	//prohibit display buffers from last half of a vram bank
	if(aim == NN_GX_MEM_DISPLAYBUFFER && end >= 3*1024*1024/2)
	{
		allocator.Free(ret);
		ret = NULL;
	}

	allocator.m_currAim = NN_GX_MEM_SYSTEM;

	return ret;
}

const int ALIGNMENT_SYSTEM_BUFFER     = 4;
const int ALIGNMENT_VERTEX            = 32; //was originally 16; thats the legal minimum. supposedly 64 can be faster in some circumstances
const int ALIGNMENT_TEXTURE           = 128;
const int ALIGNMENT_RENDER_BUFFER     = 64;   // 24-bit format (RGB8, D24) is required 96Byte aligned buffers
const int ALIGNMENT_DISPLAY_BUFFER    = 16;
const int ALIGNMENT_3D_COMMAND_BUFFER = 16;
void* mem_AllocateGraphicsMemory(GLenum area, GLenum aim, GLuint id, GLsizei size)
{
	(void)id;
	int addrAlign = 8;
	void* resultAddr = NULL;

	switch (aim)
	{
	case NN_GX_MEM_SYSTEM:
		addrAlign = ALIGNMENT_SYSTEM_BUFFER;
		break;
	case NN_GX_MEM_TEXTURE:
		addrAlign = ALIGNMENT_TEXTURE;
		break;
	case NN_GX_MEM_VERTEXBUFFER:
		addrAlign = ALIGNMENT_VERTEX;
		break;
	case NN_GX_MEM_RENDERBUFFER:
		addrAlign = ALIGNMENT_RENDER_BUFFER;
		break;
	case NN_GX_MEM_DISPLAYBUFFER:
		addrAlign = ALIGNMENT_DISPLAY_BUFFER;
		break;
	case NN_GX_MEM_COMMANDBUFFER:
		addrAlign = ALIGNMENT_3D_COMMAND_BUFFER;
		break;
	default:
		return 0;
	}


	//if the requestor specifies VRAMA or VRAMB and that bank is empty, we'll make an effort to allocate from the other VRAM bank.

//RETRY:
	switch (area)
	{
	case NN_GX_MEM_VRAMA:
		resultAddr = TryVramAllocate(*heap_vramA, aim, size, addrAlign);
		if (!resultAddr) {
			area = NN_GX_MEM_VRAMB;
			resultAddr = TryVramAllocate(*heap_vramB, aim, size, addrAlign);
		}
		break;
	case NN_GX_MEM_VRAMB:
		resultAddr = TryVramAllocate(*heap_vramB, aim, size, addrAlign);
		if (!resultAddr) {
			area = NN_GX_MEM_VRAMA;
			resultAddr = TryVramAllocate(*heap_vramA, aim, size, addrAlign);
		}
		break;
	case NN_GX_MEM_FCRAM:
		g_allocCounters.add(NN_GX_MEM_FCRAM,aim,size);
		resultAddr = mem_deviceMallocAligned(size,addrAlign);
		//NN_LOG("FCRAM allocating 0x%08X of %d with aim %d\n",resultAddr,size,aim);
		break;
	}
	
	//if we're allocating textures or vertex buffers, and couldnt allocate any ram, try again with fcram.
	//we should probably have a way to control this...
	//OOPS: GD internals get upset if asked for vram, and fcram is returned
	//we'll have to allocate texture storage ourselves and then also return which arena it ended up getting allocated from.
	//much bigger job, not needed now.
	//if(!resultAddr && (aim==NN_GX_MEM_VERTEXBUFFER || aim==NN_GX_MEM_TEXTURE) && area != NN_GX_MEM_FCRAM)
	//{
	//	area = NN_GX_MEM_FCRAM;
	//	goto RETRY;
	//}

	return resultAddr;
}

void mem_DeallocateGraphicsMemory(GLenum area, GLenum aim, GLuint id, void* addr)
{
	(void)id;
	if(aim == 0) NN_LOG("aim not set in mem_DeallocateGraphicsMemory\n");
	switch (area)
	{
	case 0:
		if(heap_vramA->BelongsToThisAllocator(addr)) heap_vramA->TryFree(addr);
		else if(heap_vramB->BelongsToThisAllocator(addr)) heap_vramB->TryFree(addr);
		else {
			g_allocCounters.remove(NN_GX_MEM_FCRAM,aim,g_Heap().GetSizeOf(addr));
			free(addr);
		}
		break;
	//this is coded this way because we allowed one vram memory area request to allocate from another
	case NN_GX_MEM_VRAMA:
		heap_vramA->TryFree(addr) || heap_vramB->TryFree(addr);
		break;
	case NN_GX_MEM_VRAMB:
		heap_vramB->TryFree(addr) || heap_vramA->TryFree(addr);
		break;
	case NN_GX_MEM_FCRAM:
		//NN_LOG("FCRAM ~deallocating 0x%08X of %d with aim %d\n",addr,g_Heap().GetSizeOf(addr),aim);
		g_allocCounters.remove(NN_GX_MEM_FCRAM,aim,g_Heap().GetSizeOf(addr));
		free(addr);
		break;
	}
}

void mem_init()
{
	//these allocators will be used by opengl and the rendering systems for its allocations
	heap_vramA.construct();
	heap_vramA->Initialize(nn::gx::CTR::MEM_VRAMA,VRamAllocator::eMethodBestFit);
	heap_vramB.construct();
	heap_vramB->Initialize(nn::gx::CTR::MEM_VRAMB,VRamAllocator::eMethodBestFit);
}

void mem_systemFree(void* p)
{
	nn::init::GetAllocator()->Free(p);
}
void* mem_systemMallocAligned(size_t amount, size_t align)
{
	return nn::init::GetAllocator()->Allocate(amount,align);
}

void mem_deviceFree(void* p)
{
	//in earlier versions of this demo framework, this was calling C's free(), but I changed it to myfree() here.
	//this hasn't been tested much, but... it looked fishy to me. and anyway, around that time, I was testing the finalization codepath in the demo, and it passed checks there OK
	//so maybe it's OK here.
	myfree(p);
}
void* mem_deviceMallocAligned(size_t amount, size_t align)
{
	return mymalloc(amount,align);
}

void mem_printGraphicsAllocReport()
{
	g_allocCounters.PrintReport();
	NN_LOG("\nVramA\n");
	heap_vramA->Dump();
	NN_LOG("\nVramB\n");
	heap_vramB->Dump();
}

void mem_flagAllocatingRenderBuffer(bool flag)
{
	g_allocCounters.flagAllocatingRenderBuffer(flag);
}


char *mem_strdup(const char *str)
{
	if (!str) return NULL;

	size_t size = strlen(str) + 1;
	char *dup = (char*)malloc(size);

	if(!dup) return NULL;

	memcpy(dup, str, size);
	dup[size] = 0;

	return dup;
}