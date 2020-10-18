#pragma once

#include <nn/fnd.h>
#include <nn/gx/CTR/gx_Vram.h>
#include <nn/gx/CTR/gx_CTR.h>

#include <vector>

#pragma pack(push, 4)
class VRamAllocator : public nn::fnd::IAllocator
{

public:
	enum eMethod {
		eMethodFirstFound,
		eMethodBestFit,
		eNumMethods
	};

private:

	struct TableEntry
	{
		enum {
			eNumShifts = 6,   // this forces a minimum of 64 byte alignment
			eBlockSize = 1<<eNumShifts
		};
		u16 pos;
		u16 size;
#ifndef PUBLIC_BUILD
		u32 aim; //a big waste of space, but might as well keep it padded
#ifndef NDEBUG
		u64 key; // bigger waste of space, but good for texture tracking
#endif
#endif
		inline u32 getEnd() const { return (u32)pos+size; }
	}; // struct TableEntry

	typedef std::vector<TableEntry> Table;
	enum {eDefaultCapacity = 256};

	static bool CmpLargestEntries(const TableEntry& a, const TableEntry& b);
	
private:
	Table m_Table;
	u32 m_Start;
	u32 m_Size;
	u32 m_FreeSize;
	eMethod m_Method;
	u8 pad[3];
#ifndef PUBLIC_BUILD
	nn::gx::CTR::VramArea m_vramArea;
#endif

	inline int findEntry(void* memory) const
	{
		u32 mem = u32(memory) - m_Start;
		const int size = m_Table.size();
		for (int i=size-1; i>=0; i--)
			if ((u32(m_Table[i].pos)<<TableEntry::eNumShifts) == mem) 
				return i;
		return -1;
	}
public:

	VRamAllocator()
		: m_Start(0)
		, m_Size(0)
		, m_Method(eMethodFirstFound)
		, m_currAim(NN_GX_MEM_SYSTEM)
	{
	}

	void Initialize(nn::gx::CTR::VramArea vramArea, eMethod method=eMethodFirstFound);
	void Finalize();
	bool TryFree(void* memory);
	bool DefragTable();
	void Dump();
	virtual void* Allocate(size_t size, s32 alignment);
	virtual void Free(void* memory);
	u32 GetFree() { return m_FreeSize; }
	u32 GetSize() { return m_Size; }
	bool BelongsToThisAllocator(void* addr) { return (u32)addr >= m_Start && (u32)addr < (m_Start + m_Size); }
	u32 GetOffset(void* ptr) { return (u32)ptr - m_Start; }

	int m_currAim;
}; // VRamAllocator
#pragma pack(pop, 4)

class GraphicsAllocationCounters
{
private:
	int counters[3][6];
public:
	bool isAllocatingRenderBuffer;
private:
	u8 pad[7];


public:
	GraphicsAllocationCounters()
	{
		for(int a=0;a<3;a++) for(int i=0;i<6;i++) counters[a][i] = 0;
		isAllocatingRenderBuffer = false;
	}

	void add(int area, int aim, int amount)
	{
		getRef(area,aim) += amount;
	}

	void remove(int area, int aim, int amount)
	{
		getRef(area,aim) -= amount;
	}

	int get(int area, int aim)
	{
		return getRef(area,aim);
	}

	void PrintReport()
	{
#ifdef PUBLIC_BUILD
#define REPORTPRINT NN_LOG
#else
#define REPORTPRINT nn::dbg::detail::Printf
#endif
		REPORTPRINT("AREA    SYS   TEX   VTX   REND  DISP  CMD   TOT   (OF)\n");
		for(int i=0;i<3;i++)
		{
			static const char* areanames[] = {"FCRAM","VRAMA","VRAMB"};
			static const char* of[] = {"~INF~","3072","3072"};
			int sizes[7];
			sizes[6] = 0;
			for(int j=0;j<6;j++)
			{
				sizes[j] = counters[i][j]/1024;
				sizes[6] += sizes[j];
			}
			REPORTPRINT("%s %5d %5d %5d %5d %5d %5d %5d   %s\n",areanames[i],sizes[0],sizes[1],sizes[2],sizes[3],sizes[4],sizes[5],sizes[6],of[i]);
		}
	}

	void flagAllocatingRenderBuffer(bool flag)
	{
		isAllocatingRenderBuffer = flag;
	}

private:
	int aimToIndex(int aim)
	{
		return aim-0x100;
	}

	int areaToIndex(int area)
	{
		return (area >> 16) - 1;
	}

	int& getRef(int area, int aim)
	{
		return counters[areaToIndex(area)][aimToIndex(aim)];
	}

};

extern GraphicsAllocationCounters g_allocCounters;
