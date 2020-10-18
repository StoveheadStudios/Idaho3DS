#pragma once

//#define USE_FS

#include <nn/types.h>
#include <nw/snd/snd_SoundHandle.h>
#include <nw/snd/snd_SequenceSoundHandle.h>

#include "engine.h"

namespace nw { namespace snd { class SoundHandle; class SequenceSoundHandle; } }

void nwsound_init(const char* bcsarPath, size_t heapSize);
void nwsound_finalize();

//call once per frame
void nwsound_update();

//Only needed for FsSoundArchives: manually control what's loaded. It's a nuisance.
int nwsound_pushHeapState(); //1. store the state, so that later you can pop it
void nwsound_load(u32 id); //2. load the specified sound resource
void nwsound_popHeapState(); //3. pop it when you're done with that game mode (and any sounds using that resource are stopped...)

//a wrapper for nw::snd::SoundHandle and SequenceSoundHandle
//I use it so I can use an alternate backend, but it's also nice for extending functionality 
//I included an example of an extended functionality (Volume2) that I've used
class Voice : private noncopyable
{
private:
	Voice(u32 soundId);

	float mVolume2;
	nw::snd::SoundHandle mHandle;
	nw::snd::SequenceSoundHandle mSeqHandle;

	friend void nwsound(Voice* voice, u32 soundId);

	nw::snd::SequenceSoundHandle* SequenceHandle();

public:
	Voice();
	virtual ~Voice() { Detach(); }
	void Detach() { mSeqHandle.DetachSound(); mHandle.DetachSound(); }

	void Stop(int fadeFrames=0);
	void Pause(bool flag, int fadeFrames=0);
	void SetPitch(float pitchRatio);
	void SetVolume(float volume);
	void SetVolume2(float volume);

	void FadeIn(int fadeFrames);
	float GetVolume() const;
	
	void SetPan(float pan);
	void SetSurroundPan(float surroundPan);
	bool IsPlaying() const;
	
	void SetSequenceTempo(float tempo);
	s16 GetSequenceVariable(int nVar);
	void SetSequenceVariable(int var, s16 val);
};

//basic sound playing:
//1. for when you dont need the handle
void nwsound(u32 soundId);
//2. for when you do:
void nwsound(Voice* voice, u32 soundId);
