#include <nn/snd.h>
#include <nn/dsp.h>
#include <nn/os.h>
#include <nn/dbg.h>
#include <nn/cfg.h>
#include <nn/applet.h>
#include <nn/fs.h>
#include <nn/gd.h>
#include <nn/ulcd.h>
#include <nn/erreula.h>

#include <wchar.h>

#include "VRamAllocator.h"
#include "mem.h"
#include "engine.h"

#ifdef USE_NW
#include "nwsound.h"
#endif

//----------------------------------------------------

static volatile int vbl_counter = 0;
static volatile int vbl_counter_miss = 0;
static nn::os::Thread vblankWaitThread;
static nn::os::LightEvent vblankPreProcessingStartEvent(false), vblankPreProcessingDoneEvent(false), vblankStartEvent(false);
static nn::os::LightEvent sleepAwakeEvent(false);
static bool s_eshop_available;
static int s_gfx_ctr_LastFrameTime = 1;
static bool vsync = true;
static int vsync_missed = 0;
static u32 m_FrameCount, m_LastFrameCount, m_LatestFPS;
static s64 m_LastFPSRenderTime, m_LastRenderTime, m_PendingSimulationTime;

static void gdCallbackErrorFunction(nnResult result, const char* funcname);
static void gxTimeoutCallback();

CommandList g_CommandList;

//----------------------------------------------------
//vsync stuff

//called by the OS whenever a vblank begins
static void vbl_Callback(GLenum)
{
	vblankStartEvent.Signal();
	vbl_counter++;
	vbl_counter_miss++;
}

//----------------------------------------------------
//applet handlers and sleep related stuff

static void app_Exit_Level0()
{
	//early app termination logic.
	//black magic recipes: don't touch code which has passed lotcheck dozens of times

	nn::hid::CTR::Finalize();
	nn::applet::CloseApplication();
}

void app_Exit()
{
	//normal app termination logic.
	//black magic recipes: don't touch code which has passed lotcheck dozens of times
	//but really, why do we need to do any of this stuff? If you're dangerously curious, I suggest commenting out everything and seeing what happens

	nn::applet::DisableSleep();

	//sound engine needs finalizing or else nn::dsp bombs

#ifdef USE_NW
	nwsound_finalize();
#endif

	//SIGNAL GAME THAT IT'S TERMINATING

	//FINALIZE EXTRA PAD, IF USED (for some reason, I don't know, maybe it's important)

	//FINALIZE OTHER THINGS
	nn::snd::CTR::Finalize();
	nn::dsp::CTR::Finalize();
	nn::cfg::CTR::Finalize();

	app_Exit_Level0();
}

static nn::applet::AppletQueryReply app_SleepQueryCallback( uptr arg )
{
	(void)arg;
	return nn::applet::IsActive() ? nn::applet::CTR::REPLY_LATER : nn::applet::CTR::REPLY_ACCEPT;
}

static void app_AwakeCallback( uptr arg )
{
	(void)arg;
	sleepAwakeEvent.Signal();
}

void ctrOnSleep()
{
	//SLEEP EXTRA PAD MANAGER
	//SLEEP MOBIPLAYER
}

void ctrRestoreGraphics()
{
	//IF USING GD:
	//make GD re-set any registers it is depending on, because the home menu or whatever couldve clobbered them
	nn::gd::System::ForceDirty(nn::gd::System::MODULE_ALL);

	//ALSO NOTE: THERE ARE MANY THINGS WHICH GD DOESNT STATE CACHE
	//For instance, combiner constant colors.
	//These things will be whacked by the home menu or sleep mode somehow so be sure to restore them, or re-set them every frame (a better idea)

	//IF USING OPENGL:
	//nngxUpdateState(NN_GX_STATE_ALL);
	//nngxValidateState(NN_GX_STATE_ALL,GL_TRUE);
}

void ctrOnWake()
{
	//WAKE EXTRA PAD MANAGER (can't remember why it has to go to sleep)
	//WAKE MOBIPLAYER
	//SIGNAL GAME THAT A WAKE HAPPENED

	ctrRestoreGraphics();
}


void app_Duties()
{
	nn::applet::CTR::AppletWakeupState wstate;

	if ( nn::applet::IsExpectedToReplySleepQuery() )
	{
		bool bRejectSleep = false;

		if (bRejectSleep)
		{
			// Notify rejection if in a state that the system cannot
			// transition to sleep mode
			nn::applet::ReplySleepQuery( nn::applet::CTR::REPLY_REJECT );
		}
		else
		{
			//go to sleep
			bool bWaitSleep = false;

			if(!bWaitSleep)
			{
				nngxWaitCmdlistDone();

				//this used to be required; it is no longer
				//nw::snd::SoundSystem::EnterSleep();

				ctrOnSleep();

				NN_LOG("entering sleep\n");
				sleepAwakeEvent.ClearSignal();
				nn::applet::ReplySleepQuery( nn::applet::CTR::REPLY_ACCEPT );
				//do not do any processing between these two calls
				sleepAwakeEvent.Wait();
				//(app is now sleeped)
				NN_LOG("leaving sleep\n");

				ctrOnWake();

				//this used to be required; it is no longer
				//nw::snd::SoundSystem::LeaveSleep();

				nngxStartLcdDisplay();
			}
		}
	}


	if(nn::applet::IsExpectedToProcessHomeButton())
	{
		nngxWaitCmdlistDone();

		ctrOnSleep();

		nn::applet::DisableSleep();
		NN_LOG("entering sleep (for home button)\n");
		nn::applet::ProcessHomeButton();
		nn::applet::WaitForStarting();
		sleepAwakeEvent.ClearSignal();
		nn::applet::EnableSleep();
		//(app is now sleeped)
		NN_LOG("leaving sleep (from home button)\n");
		nn::applet::ClearHomeButtonState();

		ctrOnWake();
	}

	if(nn::applet::IsExpectedToProcessPowerButton())
	{
		nngxWaitCmdlistDone();
		nn::applet::DisableSleep();
		NN_LOG("entering sleep (for power button)\n");
		nn::applet::ProcessPowerButton();
		nn::applet::WaitForStarting();
		sleepAwakeEvent.ClearSignal();
		nn::applet::EnableSleep();
		//(app is now sleeped)
		NN_LOG("leaving sleep (from power button)\n");

		ctrOnWake();
	}

	if(nn::applet::IsExpectedToCloseApplication() /*|| wstate == nn::applet::WAKEUP_BY_CANCEL*/ )
	{
		app_Exit();
	}
}

//----------------------------------------------------

void game_main();

extern "C" void nnMain()
{
	nn::Result result;

	//heap setup to be done before anything else to complete what nninitStartUp began
	mem_init();

	//applet setup
	nn::applet::SetSleepQueryCallback(app_SleepQueryCallback);
	nn::applet::SetAwakeCallback(app_AwakeCallback);
	nn::applet::Enable();
	if (nn::applet::IsExpectedToCloseApplication()) // Check for a close request (this came back from a pre-lotcheck, when you press the powerbutton a lot while the 3ds logo is displayed)
	{
		nn::applet::PrepareToCloseApplication();
		nn::applet::CloseApplication();
		return;
	}

	//various subsystem init
	nn::os::CTR::SetApplicationCpuTimeLimit(30); //magic number, seems to work
	nn::fs::Initialize();
	nn::cfg::Initialize();
	nn::hid::CTR::Initialize();
	nngxInitialize(mem_AllocateGraphicsMemory, mem_DeallocateGraphicsMemory);

	//setup rom filesystem (copied from TriangleSimple with a few more files added)
	static const size_t NUM_FILES = 16;
	static const size_t NUM_DIRS = 1;
	static const bool USE_CACHE = true;
	s32 rom_buffer_size = nn::fs::GetRomRequiredMemorySize(NUM_FILES,NUM_DIRS,USE_CACHE);
	u8 *rom_buffer = new u8[rom_buffer_size];
	NN_LOG("allocating %d bytes for rom FS buffer of %d files and %d dirs\n", rom_buffer_size, NUM_FILES, NUM_DIRS);
	NN_UTIL_PANIC_IF_FAILED(nn::fs::MountRom(NUM_FILES, NUM_DIRS, rom_buffer, rom_buffer_size, USE_CACHE));
	//nn::fs::SetAnalysisLog(true);

	//we wait for the sub screen because its vsync happens after the main screen
	//once upon a time, this fact saved us from some tearing, but I think that was due to other bugs.
	//it is unclear whether it is a smarter choice, since it doesnt give us more time to get any work done as compared to using the main screen
	nngxSetVSyncCallback(NN_GX_DISPLAY1,vbl_Callback);

	if(nn::applet::CTR::IsExpectedToCloseApplication())
	{
		app_Exit_Level0();
		return; //should never get here...
	}

	s_eshop_available = nn::applet::IsEShopAvailable();


	//-------
	//graphics init
	//Initialize the GD API. command lists must be initialized before this
	g_CommandList.Initialize();
	nn::gd::System::Initialize();
	nn::gd::System::SetCallbackFunctionError(gdCallbackErrorFunction);
#ifndef PUBLIC_BUILD
	nngxSetTimeout(NN_HW_TICKS_PER_SECOND,gxTimeoutCallback);
#endif
	//--------

	//-------------
	//INITIALIZE TOUCHREADER AND PADREADER AND EXTRAPADMANAGER HERE
	//-------------

	game_main();

}

//----------------------------------------------------
//graphics stuff

static void gdCallbackErrorFunction(nnResult result, const char* funcname)
{
	char* str = 0;
	NN_UNUSED_VAR(str);
	NN_UNUSED_VAR(funcname);

	if (funcname != NULL) {NN_LOG("Return from \"%s\"\n",  funcname);}
	if ((str = nn::gd::System::GetErrorStringFromResult(result)) != NULL) {NN_LOG("Error: %s\n", str);}

	//if the error was out of (vram) memory, dump some vram info
	nn::Result res = result;
	if(res == nn::gd::ResultOutOfMemoryExt())
	{
		mem_printGraphicsAllocReport();
	}

	NN_ASSERTMSG( 0, "callBack Error function called");
}

#ifdef HAVE_ORCS
#include "OrcsRecorder.h"
#include "OrcsFormat.h"

// This function is nearly identical to the one in the documentation, the only difference being it does not actually call nngxRunCmdlist()
// Instead, it only exports the current list.
static void ExportCommandList(const char* fname)
{
	// Allocate a buffer for OrcsRecorder to use
	size_t orcsSize = OrcsCalcRequiredStorageSize(
		ORCS_DEFAULT_SCRATCH_BUFFER_SIZE_IN_BYTES,
		ORCS_DEFAULT_RECORD_LIMIT);
	unsigned char * orcsMem = (unsigned char *)malloc(orcsSize);
	if (orcsMem != 0)
	{
		s32 top, curr;
		nngxGetCmdlistParameteri(NN_GX_CMDLIST_TOP_BUFADDR,&top);
		nngxGetCmdlistParameteri(NN_GX_CMDLIST_CURRENT_BUFADDR,&curr);
		NN_LOG("TOP: %08X ; CURR: %08X\n",top,curr);


		// Desired cmdlist is bound, since we are about to run it
		unsigned currentCmdlist;
		nngxGetCmdlistParameteri(
			NN_GX_CMDLIST_BINDING,
			reinterpret_cast<int *>(&currentCmdlist));
		// Fill struction with params to use
		OrcsRecorderInfo ri;
		ri.scratchBufferSize = ORCS_DEFAULT_SCRATCH_BUFFER_SIZE_IN_BYTES;
		ri.recordLimit = ORCS_DEFAULT_RECORD_LIMIT;
		ri.totalBufferSize = orcsSize;
		ri.passInStorageBuffer = orcsMem;
		ri.optional.cmdlistCount = 1;
		ri.optional.cmdlistIds = &currentCmdlist;
		ri.optional.cmdlistCountRaw = 0;
		ri.optional.cmdlistPtrsRaw = 0;
		ri.optional.display0.displayX = 0;
		ri.optional.display0.displayY = 8;
		ri.optional.display0Ext.displayX = 12;
		ri.optional.display0Ext.displayY = 0;
		ri.optional.display1.displayX = 16;
		ri.optional.display1.displayY = 32;
		// Replace with your own allocator
		void * hostIOMemory = malloc(nn::hio::CTR::WORKMEMORY_SIZE);
		nn::Result initRes = nn::hio::CTR::Initialize(hostIOMemory);
		if (initRes != nn::ResultSuccess())
		{
			NN_LOG("Couldn't init host file!\n");
		}
		const size_t BUFFER_SIZE = 1024;
		char buffer[BUFFER_SIZE];
		NN_LOG("GetCurrentDirectory\n");
		nn::hio::CTR::GetCurrentDirectory(buffer, sizeof(buffer));
		NN_LOG(" current directory: %s\n", buffer);
		nn::hio::CTR::HostFile hf;
		nn::Result hfResult = hf.Open(
			fname,
			nn::hio::CTR::HostFile::ACCESS_MODE_READ_WRITE,
			nn::hio::HostFile::OPEN_DISP_CREATE_ALWAYS);
		if (hfResult == nn::ResultSuccess())
		{
			// Creates an OrcsRecorder inside the buffer
			// Populate file with command list
			// Add memory snapshots, so list may be played back
			OrcsCreateAndExportDefault(
				OrcsDefaultErrorHandler,
				&ri,
				&hf);
			// Cmdlist and memory make up a minimal playable orcs file
			// other Orcs records exist to add meta data and context
			// Close file when finished
			hf.Close();
		}
		else
		{
			NN_LOG("Problem with host file!\n");
		}
		nn::hio::CTR::Finalize();
		// Replace with your own deallocator
		free(hostIOMemory);
		// Replace with your own deallocator
		free(orcsMem);
	}
}
#endif //HAVE_ORCS


#ifndef PUBLIC_BUILD
static void gxTimeoutCallback()
{
	//dump orcs here so you can debug why it hung
	//warning: the 3ds GPU debugger tool is a last resort. It's strange and hard to use.

#ifdef HAVE_ORCS
	g_CommandList.BeginImmediateExecution();
	NN_LOG("GX TIMEOUT! DUMPING ORCS FILE TO DEBUG\n");
	ExportCommandList("nngx_timeout.orcs");
	NN_ASSERTMSG(0,"DONE! TERMINATING\n");
#endif

}
#endif


//main vblank wait function. takes a flag whether it really waits for vsync
int vbl_WaitForVblank(bool vsync)
{
	if(vsync)
	{
		vblankStartEvent.ClearSignal();
		vblankStartEvent.Wait();
	}
	else
	{
	}
	int ret = vbl_counter_miss;
	vbl_counter_miss = 0;
	return ret;
}

int GetLastFrameTime() { return s_gfx_ctr_LastFrameTime; }

static void AnalyzeFPS()
{
	nn::fnd::TimeSpan currentTime = nn::os::Tick::GetSystemCurrent().ToTimeSpan();
	s64 currentTimeNS = currentTime.GetNanoSeconds();
	m_PendingSimulationTime += currentTimeNS-m_LastRenderTime;
	u32 nSecondsElapsed = (currentTime-nn::fnd::TimeSpan::FromNanoSeconds(m_LastFPSRenderTime)).GetSeconds();

	m_LastRenderTime = currentTimeNS;
	if (nSecondsElapsed) {
		m_LatestFPS = m_FrameCount-m_LastFrameCount;
		m_LastFrameCount = m_FrameCount;
		m_LastFPSRenderTime = m_LastRenderTime;
		if (nSecondsElapsed>1)
			m_LatestFPS /= nSecondsElapsed;
	}

	vsync_missed = vbl_counter;
	vbl_counter = 0;
}

void EndFrame()
{
	//use this to debug the commandlist (you can look at it in-memory in the debugger)
	//GLint addr=0,size=0;
	//nngxGetCmdlistParameteri(NN_GX_CMDLIST_TOP_BUFADDR, &addr);
	//nngxGetCmdlistParameteri(NN_GX_CMDLIST_USED_BUFSIZE, &size);
	//NN_LOG("cmdlist: %08X, %d\n",addr,size);

	//MAKE SURE ALL YOUR RENDERERS ARE FLUSHED

	//lets define 'E' executing commandlist and 'B' buffering commandlist (we just finished generating commands into it)
	//now, lets say we just finished generating render commands on commandlist 'B'.
	//commandlist 'E' is currently rendering, or finished rendering. in either case, we must consider framebuffer 'E' to be in use.
	//framebuffer 'B' is definitely being scanned out.

	//so, before we can do anything else, we need to wait for commandlist 'E' to finish.
	g_CommandList.WaitForExecutingCommandListAndStop();

	//now commandlist 'E' is finished rendering and the associated framebuffer 'E' is free for presenting.
	//framebuffer 'B' might still being scanned out. however, we can go ahead and schedule the framebuffers to be swapped on the next vsync
	//NOTE: reportedly, if commandlists are running or getting started between now and the next vsync, there will be tearing. that isnt a problem for us (knock on wood)
	nngxSwapBuffers(NN_GX_DISPLAY_BOTH);

	//TODO - is this a timing hazard? if the vsync happens right about now, we could miss a whole frame. we might should test issuing the swapbuffers from vblank, when a flag requesting it has been set

	//now, wait for that vsync. after then, 'E' framebuffer will be scanned out, and 'B' framebuffer will be free.
	// [[ according to some of my older study (i couldnt prove it now), nngxWaitVSync() can return even if it is in the middle of vblank (although maybe that was only in the period between screen0 and screen1 vsync)
	//    so we're using my own function instead. ]]
	//nngxWaitVSync(NN_GX_DISPLAY_BOTH);
	int frames = vbl_WaitForVblank(vsync);
	s_gfx_ctr_LastFrameTime = frames;
	//if((m_FrameCount&31)==0) nn::dbg::detail::Printf("%d\n",frames);
	//this variable tells us how many frames we ended up waiting (it might be large in the case of loads between levels... we need a way to clear that out)
	//it could be used to speed up the simulation


	//instead of flushing everything all the time, we flush the entire device memory here, before we can possibly begin rendering using any unflished data
	//nintendo advises us that due to the fixed cost (syscall, likely) for a flush operation, it is faster to flush 100% of memory than flush pieces of it twice.
	//( performance tips 2.4 sec 2.4.10 "Avoiding Multiple Calls to the nngxUpdateBuffer Function [IMPORTANT] )
	//well, it makes my life easier.
	mem_flushEntireDeviceMemory();

	//so, now that theres no unflushed data, and framebuffer 'E' is being scanned out, we can begin rendering commandlist 'B' to framebuffer 'B'
	g_CommandList.SwitchCommandLists();

	//and at this time, we will begin accumulating new commands to commandlist 'E'

	//TODO: SWAP BUFFERS IN DOUBLE-BUFFERED RENDERERS IF YOU WANT TO DO THAT AUTOMATICALLY

	//and finally, here is some random crap we do here once per frame for no good reason
	app_Duties();
	++m_FrameCount;
	AnalyzeFPS();
}

void DisplayBuffer::Initialize(GLenum which, int width, int height,  nn::gd::CTR::Resource::NativeFormat format, nn::gd::Memory::MemoryLocation loc)
{
	mTargetDisplay = which;
	mGDFormat = format;
	mWidth = width;
	mHeight = height;

	//set which as active
	nngxActiveDisplay(which);
	//make two display buffers for double buffering
	nngxGenDisplaybuffers(2, mBufferIds);

	GLenum glFormat = GL_RGB8_OES;
	mFormatWidth = 32;
	if(format == nn::gd::CTR::Resource::NATIVE_FORMAT_RGB_565)
		glFormat = GL_RGB565, mFormatWidth = 16;
	if(format == nn::gd::CTR::Resource::NATIVE_FORMAT_RGBA_5551)
		glFormat = GL_RGB5_A1, mFormatWidth = 16;
	if(format == nn::gd::CTR::Resource::NATIVE_FORMAT_RGBA_4444)
		glFormat = GL_RGBA4, mFormatWidth = 16;

	mFormat = glFormat;

	GLenum glLoc = NN_GX_MEM_VRAMB;
	if(loc == nn::gd::Memory::VRAMA)
		glLoc = NN_GX_MEM_VRAMA;
	if(loc == nn::gd::Memory::FCRAM)
		glLoc = NN_GX_MEM_FCRAM;

	//might not actually end up being here, if there wasnt room
	mRequestedMemoryLocation = glLoc;

	//configure each display buffer
	for(int i=0;i<2;i++)
	{
		//bind it
		nngxBindDisplaybuffer(mBufferIds[i]);
		//yes, height and width are backwards due to the screen rotation
		nngxDisplaybufferStorage(glFormat, height, width, glLoc);
		//display it from the corner
		nngxDisplayEnv(0, 0);
		nngxGetDisplaybufferParameteri(NN_GX_DISPLAYBUFFER_ADDRESS,(GLint*)&mBufferPointers[i]);
	}

	//setup the double buffering
	mCurBackBufferIndex = 0;
}

void DisplayBuffer::Clear()
{
	if(mRequestedMemoryLocation == NN_GX_MEM_FCRAM)
		NN_ASSERT(false); 
	int size = mWidth*mHeight*mFormatWidth/8;
	nngxAddMemoryFillCommand(mBufferPointers[0],size,0,mFormatWidth,mBufferPointers[1],size,0,mFormatWidth);
}

//this is lame, no way to specify memory location. all this is lame.
void FrameBuffer::InitializeDepthBuffer(int width, int height, nn::gd::Resource::NativeFormat format, nn::gd::Memory::MemoryLayout layout, void* addr)
{
	mem_flagAllocatingRenderBuffer(true);

	//allocate a depth buffer
	nn::gd::Texture2DResourceDescription Text2DResDesc_DepthBuffer = 
	{
		width, //width
		height, //height
		1, //miplevels
		format , //depth format
		layout, //boilerplate
		nn::gd::Memory::VRAMB //memory location
	};
	gdBool retain = (addr == NULL) ? GD_TRUE : GD_FALSE;
	nn::Result res = nn::gd::Resource::CreateTexture2DResource(&Text2DResDesc_DepthBuffer, addr, GD_FALSE, &mDepthBuffer, retain);
	if(!res.IsSuccess())
	{
		NN_LOG("failed create texture2d depthbuffer\n");
	}

	//create depth stencil target
	nn::gd::DepthStencilTargetDescription descDepthTarget = {0};
	res = nn::gd::OutputStage::CreateDepthStencilTarget(mDepthBuffer, &descDepthTarget, &mDepthTarget);
	if(!res.IsSuccess())
	{
		NN_LOG("failed create depthtarget\n");
	}

	nn::gd::CTR::Resource::GetTexture2DResourceProperties(mDepthBuffer,&mDepthBufferProperties);
	mem_flagAllocatingRenderBuffer(false);
}

void FrameBuffer::InitializeCastFrom(FrameBuffer* source, nn::gd::Resource::NativeFormat format, nn::gd::Memory::MemoryLayout layout)
{
	nn::Result res = nn::gd::Resource::CreateTexture2DResourceCastFrom(source->mColorBuffer, format, layout, &mColorBuffer);
	if(!res.IsSuccess())
	{
		NN_LOG("failed create texture2d framebuffer in InitializeCastFrom\n");
	}

	SharedRTInitialization();
}

//read "workaround #2" in advanced programming manual graphical glitches with large framebuffers
void FrameBuffer::Initialize(int width, int height, nn::gd::Resource::NativeFormat format, nn::gd::Memory::MemoryLayout layout, nn::gd::Memory::MemoryLocation memloc, void* addr)
{
	mem_flagAllocatingRenderBuffer(true);

	//allocate a color buffer
	nn::gd::Texture2DResourceDescription Text2DResDesc_ColorBuffer = 
	{
		width, //width
		height, //height
		1, //miplevels
		format, //color format
		layout, 
		memloc //memory location
	};

	nn::Result res = nn::gd::Resource::CreateTexture2DResource(&Text2DResDesc_ColorBuffer, addr, GD_FALSE, &mColorBuffer, GD_FALSE);
	if(!res.IsSuccess())
	{
		NN_LOG("failed create texture2d framebuffer\n");
	}
	//ResultInvalidMemoryLayout 	Memory layout is not correct. This result is also returned if initialization data has been specified but the memory layout is not Memory::LAYOUT_BLOCK_8.

	SharedRTInitialization();

	mem_flagAllocatingRenderBuffer(false);
}

void FrameBuffer::SharedRTInitialization()
{
	//create a render target and attach the color buffer to it
	nn::gd::RenderTargetDescription descRenderTarget = {0};
	nn::Result res = nn::gd::OutputStage::CreateRenderTarget(mColorBuffer, &descRenderTarget, &mRenderTarget);
	if(!res.IsSuccess())
	{
		NN_LOG("failed create rendertarget\n");
	}

	nn::gd::CTR::Resource::GetTexture2DResourceProperties(mColorBuffer,&mColorBufferProperties);
}

void FrameBuffer::TransferTo(DisplayBuffer* dispBuf, nn::gd::Memory::DownScalingMode downscaleMode)
{
	dispBuf->BindBackBuffer();

	int yofs = 0;

	const nn::gd::CTR::Resource::NativeFormat format = dispBuf->mGDFormat;

	//find out where the display buffer resides (could be cached)
	int dstAddr;
	nngxGetDisplaybufferParameteri(NN_GX_DISPLAYBUFFER_ADDRESS, &dstAddr);
	nn::Result result = nn::gd::Memory::CopyTexture2DResourceBlockToLinear(
		mColorBuffer, //source texture
		0, //source mip level
		yofs, //source y offset
		(u8*)dstAddr, //destination address
		dispBuf->mHeight, //yeah, its backwards. you know how it goes
		dispBuf->mWidth,
		format, //destination pixel format
		downscaleMode, //antialiasing
		GD_FALSE //y flip flag
		);

	NN_ASSERT(result.IsSuccess());

	//the rules governing when to flush are weird. after certain commands its necessary. this is one.
	nngxFlush3DCommandNoCacheFlush();

	//for now, we're considering this operation to signify that the target displaybuffer is complete for a frame.
	//therefore, we're responsible for doing the buffer cycling
	//(this architecture isnt the greatest. we should have a separate present process for a displaybuffer, in case we ever make this buffer transferring logic more generally powerful)
	dispBuf->Increment();
	//furthermore, we need to bind again: because the buffer that was just active while generating the transfer command is definitely not the one we should present next
	dispBuf->BindBackBuffer();
}

void gdBindVB(int slot,nn::gd::VertexBufferResource* vb, int vertexOffset)
{
	if(!vb) return; //can't unbind, or at least, couldn't when I wrote this code
	nn::gd::VertexBufferResource* vbs[] = { vb };
	u32 offsets[] = { vertexOffset };
	nn::gd::VertexInputStage::SetVertexBuffers(slot,1,vbs,offsets);
}


//-------------------- you can use this code to avoid nintendo's hardware-accelerated function (CopyTexture2DResourceLinearToBlock)  -------------
//-------------------- which inserts commands and thus requires buffer management ----------------------------------------------------------------
//-------------------- and also is just generally hard to use.  but this should hopefully be done during asset compilation stage -----------------
//to determine the zig zag table, jumble the bits like this:
//or maybe the opposite. just fiddle with it til it works.
//s[0] + s[3] + s[1] + s[4] + s[2] + s[5];
static const u8 ZigZagTable[] = { 0x00, 0x01, 0x08, 0x09, 0x02, 0x03, 0x0A, 0x0B, 0x10, 0x11, 0x18, 0x19, 0x12, 0x13, 0x1A, 0x1B, 0x04, 0x05, 0x0C, 0x0D, 0x06, 0x07, 0x0E, 0x0F, 0x14, 0x15, 0x1C, 0x1D, 0x16, 0x17, 0x1E, 0x1F, 0x20, 0x21, 0x28, 0x29, 0x22, 0x23, 0x2A, 0x2B, 0x30, 0x31, 0x38, 0x39, 0x32, 0x33, 0x3A, 0x3B, 0x24, 0x25, 0x2C, 0x2D, 0x26, 0x27, 0x2E, 0x2F, 0x34, 0x35, 0x3C, 0x3D, 0x36, 0x37, 0x3E, 0x3F };
template<typename DT> DT* ZigZag(int Width, int Height, void* Pixels)
{
	int bw = Width / 8;
	int bh = Height / 8;
	DT *newPixels = new DT[Width*Height];
	DT* dPtr = &newPixels[0];
	int dIndex = 0;
	DT* sPtr = (DT*)Pixels;
	{
		for (int by = 0; by < bh; by++)
			for (int bx = 0; bx < bw; bx++)
			{
				for(int y=0;y<8;y++)
					for (int x = 0; x < 8; x++)
					{
						int blockSubAddr = y * 8 + x;
						int linearSubAddr = ZigZagTable[blockSubAddr];
						int linear_x = linearSubAddr & 7;
						int linear_y = linearSubAddr >> 3;
						int total_linear_y = (by * 8 + linear_y);
						total_linear_y = Height - 1 - total_linear_y;
						int sAddr =  total_linear_y * Width + (bx * 8 + linear_x);
						DT sPixel = sPtr[sAddr];
						DT dPixel = sPixel;
						dPtr[dIndex++] = dPixel;
					}
			}
	}

	return newPixels;
}

u32* ZigZag32(int Width, int Height, void* Pixels) { return ZigZag<u32>(Width, Height, Pixels); }
u8* ZigZag8(int Width, int Height, void* Pixels) { return ZigZag<u8>(Width, Height, Pixels); }

void TextureHandle::CreateResource(void* buffer, bool allocate, const nn::gd::Texture2DResourceDescription& resDescr)
{
	NN_ASSERT(allocate || buffer);

	gdBool retain;
	if(allocate)
	{
		retain = GD_TRUE;
		mBufferToFree = NULL;
	}
	else
	{
		retain = GD_FALSE;
		mBufferToFree = buffer;
	}

	gdBool autogenMipmaps = GD_FALSE;
	//gdBool autogenMipmaps = GD_TRUE;

	//this is the actual big texture object
	nn::Result result = nn::gd::Resource::CreateTexture2DResource(&resDescr, buffer, autogenMipmaps, &mResource, retain);
	NN_ERR_THROW_FATAL_ALL(result);

	mMipLevels = resDescr.m_CountMipLevels;
}

int GetDiscreteUserStereoLevel(int levels)
{
	int user_parallax_factor;

	//fun fact: this function didn't always exist.
	//Originally we had to use a function to get a matrix and derive the stereo level from that. This was explicitly forbidden.
	//Then we got this function, but had to ask permission to use it.
	//Finally we don't have to ask permission any more.
	//Note: this doesn't count as "coordination with stereoscopic display status" in the checksheets.
	float temp = nn::ulcd::Get3DVolume();

	if(temp == 0)
		user_parallax_factor = 0;
	else
	{
		temp *= (levels-2);
		temp++;
		user_parallax_factor = (int)temp;
	}
	return user_parallax_factor;
}

//--------------------------------

//----------------------------------------------------
//ErrEula services
//WARNING: you may need to change the `upperScreenFlag` and otherwise customize this for your game
//however, we didn't customize this for dozens of submissions

struct ErrorDisplayParams
{
	ErrorDisplayParams( nn::applet::AppletWakeupState* _wstate, nn::erreula::Parameter* _param, nn::os::Event* _completeEvent )
		: wstate(_wstate)
		, param(_param)
		, completeEvent(_completeEvent)
	{
	}

	nn::applet::AppletWakeupState* wstate;
	nn::erreula::Parameter* param;
	nn::os::Event* completeEvent;
};

static void ErrEula_ErrorDisplayThreadFunc(uptr _param)
{
	ErrorDisplayParams* errorDisplayParams = reinterpret_cast<ErrorDisplayParams*>( _param );

	nn::erreula::StartErrEulaApplet( errorDisplayParams->wstate, errorDisplayParams->param );

	//full disclosure: I used to have this here, but it's fulfilling the same role as ctrRestoreGraphics and should be done there
	//I left it here just in case it was important
	//nngxUpdateState(NN_GX_STATE_ALL);
	//nngxValidateState(NN_GX_STATE_ALL,GL_TRUE);

	errorDisplayParams->completeEvent->Signal();
}


static nn::applet::AppletWakeupState ErrEula_StartErrorDisplayThread(nn::erreula::Parameter& _param)
{
	nn::os::Event errorComplete;
	errorComplete.Initialize(true);
	nn::os::Thread errorThread;

	nn::applet::AppletWakeupState wstate; 

	ErrorDisplayParams displayParams( &wstate, &_param, &errorComplete);

	//WARNING: starting threads on the 3ds is kind of needlessly complicated in the memory management department
	//whether this works depends on several details of how youve setup your memory management
	//Also, there's always the concern of how to manage your thread priorities
	nn::Result result = errorThread.TryStartUsingAutoStack( 
		&ErrEula_ErrorDisplayThreadFunc, 
		reinterpret_cast<uptr>( &displayParams ), 
		1024,
		nn::os::Thread::GetCurrentPriority()+1 
		);

	NN_ASSERT( result.IsSuccess() );

	//Waits for the applet to finish.
	while (!errorComplete.Wait( nn::fnd::TimeSpan::FromMilliSeconds(100) ) )
	{
		//Tick anything that needs to happen every frame in here
		//For me, that happens to be nothing
	}

	errorThread.Detach();
	errorComplete.Finalize();

	return wstate;
}

void ErrEula_DisplayErrorCode(s32 _nErrorCode, bool _bCheckNetSettings)
{
	//WARNING: I added this while putting this in ctreq60. I think it should be here.
	nngxWaitCmdlistDone();

	nn::erreula::Parameter param;

	//clear the parameter fields
	memset(&param, 0, sizeof(nn::erreula::Parameter));
	nn::erreula::CTR::InitializeConfig(&param.config);

	param.config.useLanguage = nn::erreula::USE_LANGUAGE_DEFAULT;
	param.config.errorType = nn::erreula::ERROR_TYPE_ERROR_CODE;
	param.config.errorCode = _nErrorCode;
	param.config.upperScreenFlag = nn::erreula::UPPER_SCREEN_STEREO;
	param.config.homeButton = true;
	param.config.softwareReset = true;
	param.config.appJump = _bCheckNetSettings;

	nn::applet::AppletWakeupState wstate = ErrEula_StartErrorDisplayThread( param );

	//Todo: process return code
	//(but we never did this)
	NN_LOG( "ERREULA: Return Code : %d\n", param.config.returnCode );

	ctrRestoreGraphics();
}

void ErrEula_DisplayErrorText(const wchar_t* _errorStr)
{
	//WARNING: I added this while putting this in ctreq60. I think it should be here.
	nngxWaitCmdlistDone();

	nn::erreula::Parameter param;

	//clear the parameter fields
	memset( &param, 0, sizeof(nn::erreula::Parameter));
	nn::erreula::CTR::InitializeConfig(&param.config);

	param.config.useLanguage = nn::erreula::USE_LANGUAGE_DEFAULT;
	param.config.errorType = nn::erreula::ERROR_TYPE_ERROR_TEXT;
	//I changed this for ctreq60 to not require NW
	//nw::ut::wcsncpy( param.config.errorText, 1900, _errorStr, _strCount );
	wcsncpy(param.config.errorText, _errorStr, nn::erreula::ERROR_TEXT_LENGTH_MAX);
	param.config.upperScreenFlag = nn::erreula::UPPER_SCREEN_STEREO;
	param.config.homeButton = true;
	param.config.softwareReset = true;
	param.config.appJump = false;

	nn::applet::AppletWakeupState wstate = ErrEula_StartErrorDisplayThread( param );

	//Todo: process return code
	//(but we never did this)
	NN_LOG( "ERREULA: Return Code : %d\n", param.config.returnCode );

	ctrRestoreGraphics();
}

void ErrEula_DisplayEulaAgreement()
{
	//WARNING: I added this while putting this in ctreq60. I think it should be here.
	nngxWaitCmdlistDone();

	nn::erreula::Parameter param;

	//clear the parameter fields
	memset(&param, 0, sizeof(nn::erreula::Parameter));

	param.config.errorType = nn::erreula::ERROR_TYPE_EULA;
	param.config.upperScreenFlag = nn::erreula::UPPER_SCREEN_STEREO;
	param.config.homeButton = false;
	param.config.softwareReset = false;
	param.config.appJump = false;

	nn::applet::AppletWakeupState wstate = ErrEula_StartErrorDisplayThread( param );

	//Todo: process return code
	//(but we never did this)
	//In fact, I check the EULA status after returning from this to see if it was accepted
	NN_LOG( "ERREULA: Return Code : %d\n", param.config.returnCode );

	ctrRestoreGraphics();
}

//----------------------------------------------------
//other services

bool IsEshopAvailable()
{
	return s_eshop_available;
}

void JumpToEShopTitlePage(u32 uniqueId)
{
	nn::applet::JumpToEShopTitlePage(uniqueId);
}
