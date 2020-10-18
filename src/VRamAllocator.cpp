#include "VRamAllocator.h"
#include <algorithm>

GraphicsAllocationCounters g_allocCounters;

//#define VRAMALLOCATOR_LOG
void VRamAllocator::Initialize(nn::gx::CTR::VramArea vramArea, eMethod method)
{
	m_Table = Table();
	m_Table.reserve(eDefaultCapacity);

	m_Start = u32(nn::gx::CTR::GetVramStartAddr(vramArea));
	m_Size = u32(nn::gx::CTR::GetVramSize(vramArea));
	m_FreeSize = m_Size;

	NN_ASSERT((m_Start&(TableEntry::eBlockSize-1))==0);
	NN_ASSERT((m_Size&(TableEntry::eBlockSize-1))==0);
	NN_ASSERT((m_Size>>TableEntry::eNumShifts)<=0xFFFF);

	m_Method = method;
#ifndef PUBLIC_BUILD
	m_vramArea = vramArea;
#endif
}

void VRamAllocator::Finalize()
{
	Table aTable;
	m_Table.swap(aTable);
	m_Start = 0;
	m_Size = 0;
	m_FreeSize = m_Size;
}

bool VRamAllocator::DefragTable()
{
	if (m_Table.empty()) {
		int tableCapacity = m_Table.capacity();
		m_Table.resize(0);
		m_Table.reserve(tableCapacity);
		return true;
	}
	return false;
}

inline u32 MathRoundup(u32 x, u32 base)
{
	return (x + (base-1)) & ~(base-1);
}

void* VRamAllocator::Allocate(size_t size, s32 alignment)
{
	// ensure that the table is large enough
	m_Table.reserve(m_Table.size()+1);

	TableEntry e;
	alignment = alignment<TableEntry::eBlockSize?  TableEntry::eBlockSize : MathRoundup(alignment, TableEntry::eBlockSize);
	NN_ASSERT((m_Start&(alignment-1))==0);

	// we assume here alignment is a power of 2
	e.size = MathRoundup(size, TableEntry::eBlockSize)>>TableEntry::eNumShifts;

	s32 idx = -1;
	// search from the back
	int regionEnd = int(m_Size>>TableEntry::eNumShifts);
	int least_waste=INT_MAX, waste, i=m_Table.size()-1;
	for (; i>=0; i--) {
		// consider from entry_end to regionEnd
		u32 pos = MathRoundup(m_Table[i].getEnd()<<TableEntry::eNumShifts, alignment)>>TableEntry::eNumShifts;
		waste = (regionEnd-pos)-e.size;
		if (waste>=0 && waste<least_waste) {
			idx = i+1;
			least_waste = waste;
			if (m_Method==eMethodFirstFound) {
				break;
			}
		}
		regionEnd = int(m_Table[i].pos);
	}

	// now consider inserting at 0
	if (i<0) {
		waste = regionEnd-e.size;
		if (waste>=0 && waste<least_waste) {
			idx = 0;
		}
	}

	if (idx>=0) {
		e.pos = idx>0? (MathRoundup(m_Table[idx-1].getEnd()<<TableEntry::eNumShifts, alignment)>>TableEntry::eNumShifts) : 0;
		m_Table.push_back(e);
		for (i=m_Table.size()-1; i>idx; i--) {
			m_Table[i] = m_Table[i-1];
		}
		m_Table[idx] = e;

		void* memory = (void *)(m_Start+(u32(e.pos)<<TableEntry::eNumShifts));
#ifdef VRAMALLOCATOR_LOG
		NN_LOG("VRamAllocator: Alloc(%d) @(%x)\n", size, memory);
#endif

		m_FreeSize -= size;

#ifndef PUBLIC_BUILD
		m_Table[idx].aim = m_currAim;

		g_allocCounters.add(m_vramArea, m_currAim, size);
#endif

		//NN_LOG("allocating vram %d bytes at 0x%08X now free %d bytes\n",size,memory,m_FreeSize);
		return memory;
	}

	return NULL;
}

bool VRamAllocator::CmpLargestEntries(const TableEntry& a, const TableEntry& b)
{
	return a.size>b.size;
}


void VRamAllocator::Dump()
{
	NN_LOG("VRamAllocator::DumpAllocs START\n");
	std::sort(m_Table.begin(), m_Table.end(), CmpLargestEntries); 
	for (int i=0; i<m_Table.size(); i++) {
		const TableEntry& e = m_Table[i];
		void* memory = (void *)(m_Start+(u32(e.pos)<<TableEntry::eNumShifts));
		u32 mem_size = m_Table[i].size<<TableEntry::eNumShifts;
		NN_LOG("Alloc %d (%x) - %d bytes", i, memory, mem_size);
	}
	NN_LOG("VRamAllocator::Dump END\n");
}

bool VRamAllocator::TryFree(void* memory)
{
	if (BelongsToThisAllocator(memory)) {		
		int nEntry = findEntry(memory);
		if (nEntry>=0) {
			u32 mem_size = m_Table[nEntry].size<<TableEntry::eNumShifts;
#ifndef PUBLIC_BUILD
			m_currAim = m_Table[nEntry].aim;
			g_allocCounters.remove(m_vramArea, m_currAim, mem_size);
#endif
			m_FreeSize += mem_size;
			//NN_LOG("freeing vram %d bytes at 0x%08X now free %d bytes\n",mem_size,memory,m_FreeSize);
			for (int i=nEntry+1; i<m_Table.size(); i++) {
				m_Table[i-1] = m_Table[i];
			}
			m_Table.pop_back();
#ifdef VRAMALLOCATOR_LOG
			NN_LOG("VRamAllocator: Free(%d) @(%x)\n", mem_size, memory);
#endif
			return true;
		}
	}
	return false;
}


void VRamAllocator::Free(void* memory)
{
	bool bFound = TryFree(memory);
	NN_ASSERT(bFound);
}
