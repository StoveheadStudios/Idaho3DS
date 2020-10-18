#include <string.h>
#include <stdlib.h>

#include <nn/types.h>
#include <nn/snd.h>
#include <nn/dsp.h>
#include <nw/snd.h>

#include "mem.h"

#include "nwsound.h"

//1.
void* pBuffer_SoundSystem;

//2a. (MemorySoundArchive)
static void* pBuffer_MemoryArchive;
nw::snd::MemorySoundArchive nw_MemoryArchive;
nw::snd::SoundArchive *nw_SelectedArchive;
//2b. (FsRomArchive)
nw::snd::RomSoundArchive nw_RomArchive;
void* pBuffer_RomArchiveInfoBlock;

//3.
nw::snd::SoundDataManager nw_DataManager;
void* pBuffer_SoundDataManager;

//4.
nw::snd::SoundArchivePlayer m_ArchivePlayer;
void* pBuffer_SoundArchivePlayer;
void* pBuffer_StreamBuffer;

//5. (only needed if using FsSoundArchive)
nw::snd::SoundHeap nw_Heap;
void* pBuffer_SoundHeap;

void nwsound_init(const char* bcsarPath, size_t heapSize)
{
	nn::Result result;

	//0a. DSP must be initialized before nn::snd
	result = nn::dsp::Initialize();
	NN_UTIL_PANIC_IF_FAILED(result);
	result = nn::dsp::LoadDefaultComponent();
	NN_UTIL_PANIC_IF_FAILED(result);

	//0b. nn::snd must be initialized before nw::snd
	result = nn::snd::Initialize();
	NN_UTIL_PANIC_IF_FAILED(result);

	//1. nw SoundSystem:
	//run on secondary thread (always works fine for me)
	{
		nw::snd::SoundSystem::SoundSystemParam param;
		param.soundThreadCoreNo = 1;
		size_t workMemSize = nw::snd::SoundSystem::GetRequiredMemSize(param);
		pBuffer_SoundSystem = mem_deviceMallocAligned(workMemSize, 32);
		nw::snd::SoundSystem::Initialize(
			param,
			(uptr)pBuffer_SoundSystem,
			workMemSize );
		nw::snd::SoundSystem::SetOutputMode(nw::snd::OUTPUT_MODE_STEREO);
		nw::snd::SoundSystem::SetMasterVolume(1.0f,0);
	}

	#ifndef USE_FS
	{
		//2a.
		//read sound archive into an nw MemorySoundArchive
		//most games using my code will be small enough to do this
		//games with more/larger sounds are obliged to load the pieces of data before they're used.
		nn::fs::FileInputStream fis;
		fis.Initialize(bcsarPath);
		s64 size = fis.GetSize();
		pBuffer_MemoryArchive = mem_deviceMallocAligned(size, 32);
		fis.Read(pBuffer_MemoryArchive,size);
		nw_MemoryArchive.Initialize(pBuffer_MemoryArchive);
		nw_SelectedArchive = &nw_MemoryArchive;

		//in order for nw to open streams, it needs to know how to build paths to filenames.
		//this should be the directory containing the bcsar, probably
		char* path = mem_strdup(bcsarPath);
		*strrchr(path,'/') = 0;
		nw_MemoryArchive.SetExternalFileRoot(path);
		free(path);
	}
	#else
	{
		//2b. FsRomArchive
		if(!nw_RomArchive.Open(bcsarPath))
			NN_ASSERTMSG(false, "cannot open bcsar: %s\n", bcsarPath);
		nw_SelectedArchive = &nw_RomArchive;
		
		//the FsRomArchive needs the header loaded into memory for random access (its the index to all the other resources)
		size_t infoBlockSize = nw_RomArchive.GetHeaderSize();
		pBuffer_RomArchiveInfoBlock = mem_deviceMallocAligned(infoBlockSize, 32);
		if(!nw_RomArchive.LoadHeader(pBuffer_RomArchiveInfoBlock, infoBlockSize))
			NN_ASSERTMSG(false, "cannot load infoBlock (%s)", bcsarPath);
	}
	#endif

	//3. setup the "data manager", kind of used for loading and tracking bits of the sound archive
	//I'm not sure why MemorySoundArchive needs this, but it does.
	{
		size_t setupSize = nw_DataManager.GetRequiredMemSize(nw_SelectedArchive);
		pBuffer_SoundDataManager = mem_deviceMallocAligned(setupSize, 32);
		nw_DataManager.Initialize(nw_SelectedArchive, pBuffer_SoundDataManager, setupSize);
	}


	//4. setup the "archive player", the interface used for playing sounds
	{
		size_t setupSize = m_ArchivePlayer.GetRequiredMemSize(nw_SelectedArchive);
		pBuffer_SoundArchivePlayer = mem_deviceMallocAligned(setupSize, 32);
		size_t setupStrmBufferSize = m_ArchivePlayer.GetRequiredStreamBufferSize(nw_SelectedArchive);
		pBuffer_StreamBuffer = mem_deviceMallocAligned(setupStrmBufferSize, 32);
		bool ok = m_ArchivePlayer.Initialize(
			nw_SelectedArchive,
			&nw_DataManager,
			pBuffer_SoundArchivePlayer, setupSize,
			pBuffer_StreamBuffer, setupStrmBufferSize, 
			0 //allow 0 bytes of user parameter per sound
			);
		NN_ASSERT(ok);
	}

	#ifdef USE_FS
		//5. memory and setup for the sound heap
		//this is only needed for a FsSoundArchive; it's used to contain the resources that are loaded (we give it to the SoundDataManager to use for storage)
		{
			pBuffer_SoundHeap = mem_deviceMallocAligned(heapSize, 32);
			bool ok = nw_Heap.Create(pBuffer_SoundHeap, heapSize);
			NN_ASSERT(ok);
		}
	#endif
}

void nwsound_load(u32 id)
{
	if(!nw_DataManager.LoadData(id, &nw_Heap))
	{
		NN_ASSERTMSG(false, "nw snd LoadData failed");
	}
}

void nwsound_finalize()
{
	nw_Heap.Destroy();
	m_ArchivePlayer.Finalize();
	nw_DataManager.Finalize();
	
	if(nw_RomArchive.IsAvailable())
		nw_RomArchive.Close();

	if(nw_MemoryArchive.IsAvailable())
		nw_MemoryArchive.Finalize();

	nw::snd::SoundSystem::Finalize();

	mem_deviceFree(pBuffer_MemoryArchive);
	mem_deviceFree(pBuffer_RomArchiveInfoBlock);
	mem_deviceFree(pBuffer_SoundDataManager);
	mem_deviceFree(pBuffer_SoundArchivePlayer);
	mem_deviceFree(pBuffer_SoundHeap);
	mem_deviceFree(pBuffer_StreamBuffer);
}

void nwsound_update()
{
	m_ArchivePlayer.Update();
}

//this is dumb stuff for manual memory management
int nwsound_pushHeapState() { return nw_Heap.SaveState(); }
void nwsound_popHeapState() { int nLevel = nw_Heap.GetCurrentLevel(); nw_Heap.LoadState(nLevel-1); }

Voice::Voice()
	: mVolume2(1.0f)
{
}

void Voice::Stop(int fadeFrames) { mHandle.Stop(fadeFrames); }
void Voice::Pause(bool flag, int fadeFrames) { mHandle.Pause(flag,fadeFrames); }
void Voice::SetPitch(float pitchRatio) { mHandle.SetPitch(pitchRatio); }
void Voice::SetVolume(float volume) { mHandle.SetVolume(volume * mVolume2); }
void Voice::SetVolume2(float volume2)
{
	mVolume2 = volume2;
	//uhhhh this doesn't completely make sense. oh well.
	SetVolume(1.0f);
}
void Voice::FadeIn(int fadeFrames) { mHandle.FadeIn(fadeFrames);}

float Voice::GetVolume() const
{
	//an old note I had for what to do in case it's a sequence sound, but I didn't test it
	//const nw::snd::internal::SequenceSound* pAttachedSeq = mSeqHandle.detail_GetAttachedSound();
	//if (pAttachedSeq)
	//	pAttachedSeq->GetVolume();

	const nw::snd::internal::BasicSound* pAttachedSound = mHandle.detail_GetAttachedSound();
	if (pAttachedSound)
		return pAttachedSound->GetVolume();

	return 0.0f;
}

void Voice::SetPan(float pan) { mHandle.SetPan(pan); }
void Voice::SetSurroundPan(float surroundPan) { mHandle.SetSurroundPan(surroundPan); }

//note: NW detaches the sound once it's no longer playing
bool Voice::IsPlaying() const { return mHandle.IsAttachedSound(); }

nw::snd::SequenceSoundHandle* Voice::SequenceHandle()
{
	if(!mSeqHandle.IsAttachedSound())
	{
		//make the sequence handle from the regular sound handle
		//it's weird but that's how it works. it's kind of a view over the basic handle with additional APIs
		mSeqHandle.~SequenceSoundHandle();
		new(&mSeqHandle) nw::snd::SequenceSoundHandle(&mHandle);
	}

	return &mSeqHandle;
}

void Voice::SetSequenceTempo(float tempo) { SequenceHandle()->SetTempoRatio(tempo); }
s16 Voice::GetSequenceVariable(int nVar) 
{
	s16 val = -1;
	if (nVar<16)
		mSeqHandle.ReadVariable(nVar, &val);
	else if (nVar<32)
		nw::snd::SequenceSoundHandle::ReadGlobalVariable(nVar-16, &val);
	//I had this commented out for some reason, so I don't know if it works
// 	else if (nVar<48)
// 		mSeqHandle.ReadTrackVariable(nVar-32, &val);
	return val;
}
void Voice::SetSequenceVariable(int var, s16 val)
{
	if (var<16)
		mSeqHandle.WriteVariable(var, val);
	else if (var<32)
		nw::snd::SequenceSoundHandle::WriteGlobalVariable(var-16, val);
}

void nwsound(Voice* voice, u32 soundId)
{
	nw::snd::SoundStartable::StartInfo si;
	nw::snd::SoundStartable::StartResult result = m_ArchivePlayer.StartSound(&voice->mHandle,soundId,&si);
}

void nwsound(u32 soundId)
{
	nw::snd::SoundHandle tempHandle;
	nw::snd::SoundStartable::StartInfo si;
	nw::snd::SoundStartable::StartResult result = m_ArchivePlayer.StartSound(&tempHandle,soundId,&si);
}

	//else
	//{
	//	if (!m_RomArchive.Open(bcsarPath))
	//	{
	//		NN_ASSERTMSG( 0, "cannot open bcsar(%s)\n", bcsarPath );
	//	}

	//	nw_SelectedArchive = &m_RomArchive;

	//	// Allocate space and load the archive header into memory
	//	{
	//		size_t infoBlockSize = m_RomArchive.GetHeaderSize();
	//		m_pMemoryForInfoBlock = mem_deviceMallocAligned( infoBlockSize );
	//		if ( ! m_RomArchive.LoadHeader( m_pMemoryForInfoBlock, infoBlockSize ) )
	//		{
	//			NN_ASSERTMSG( 0, "cannot load infoBlock(%s)", bcsarPath );
	//		}
	//	}
	//}


