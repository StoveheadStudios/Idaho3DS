#include <algorithm>
#include <stdio.h>
#include <nn/dbg.h>
#include <nn/hid.h>
#include "engine.h"
#include "QR.h"
#include "SaveManager.h"

#ifdef USE_NW
#include "nwsound.h"
#include "simple.csid"
#endif


#define SCREEN_LEFT 0
#define SCREEN_RIGHT 1
#define SCREEN_SUB 2

//we can reuse the framebuffer inbetween drawing each screen
//at the conclusion of drawing the scene, a transfer from framebuffer->displaybuffer is queued
//3d rendering does not proceed until that transfer is complete
FrameBuffer sFrameBuffers[1];

//one for each eye, and one for the sub screen
DisplayBuffer sDisplayBuffers[3];

nn::gd::SamplerState* sampler;
nn::gd::BlendState *blendNone, *blendNormal;

nn::hid::CTR::PadReader* padReader;

SaveManager* saveManager;

u8 test0[512];

struct {
	int xscroll;
} gameplay;

void CreateBuffers()
{
	sDisplayBuffers[SCREEN_LEFT].Initialize(NN_GX_DISPLAY0, 400, 240);
	sDisplayBuffers[SCREEN_RIGHT].Initialize(NN_GX_DISPLAY0_EXT, 400, 240);
	sDisplayBuffers[SCREEN_SUB].Initialize(NN_GX_DISPLAY1, 320, 240);

	sFrameBuffers[0].Initialize(240,400);
	sFrameBuffers[0].InitializeDepthBuffer(240,400,nn::gd::Resource::NATIVE_FORMAT_DEPTH_24_STENCIL_8,nn::gd::Memory::LAYOUT_BLOCK_8);
}

void CreateSamplers()
{
	nn::gd::SamplerStateDescription descr;
	descr.SetMinFilter(nn::gd::TextureStage::SAMPLER_MIN_FILTER_NEAREST);
	descr.SetMagFilter(nn::gd::TextureStage::SAMPLER_MAG_FILTER_NEAREST);
	nn::gd::TextureStage::CreateSamplerState(&descr,&sampler);
}

void CreateBlendStates()
{
	{
		nn::gd::BlendStateDescription blendDescr;
		blendDescr.SetBlendMode_NoBlend();
		nn::gd::OutputStage::CreateBlendState(&blendDescr, &blendNone);
	}

	{
		nn::gd::BlendStateDescription blendDescr;
		blendDescr.SetBlendMode_DefaultBlending();
		nn::gd::OutputStage::CreateBlendState(&blendDescr, &blendNormal);
	}
}

TextureHandle* LoadTexture32(const char* fname, int width, int height)
{
	FILE* bmpf = fopen(fname,"rb");
	fseek(bmpf,0x36,SEEK_SET);
	u8 *rawtexbuf = new u8[width*height*4];
	fread(rawtexbuf,1,width*height*4,bmpf);
	fclose(bmpf);

	//oops, I had the wrong color format in the BMP
	for(int y=0,i=0;y<height;y++)
	{
		for(int x=0;x<width;x++,i++)
		{
			u32* u32ptr = &((u32*)rawtexbuf)[i];
			u8* ptr = (u8*)u32ptr;
			*u32ptr = ptr[3]|(ptr[0]<<8)|(ptr[1]<<16)|(ptr[2]<<24);
		}
	}

	//in case your bitmaps are y-flipped
	//(I finally found a program `Pixelformer` which can save flipped bmps with alpha channels.. believe it or not!)
	//for(int y=0;y<height/2;y++)
	//{
	//	for(int x=0;x<width;x++)
	//	{
	//		u32* srcptr = &((u32*)rawtexbuf)[y*width+x];
	//		u32* dstptr = &((u32*)rawtexbuf)[(height-y-1)*width+x];
	//		u32 src = *srcptr, dst = *dstptr;
	//		*srcptr = dst;
	//		*dstptr = src;
	//	}
	//}

	u32* texbuf = ZigZag32(width,height,rawtexbuf);
	delete[] rawtexbuf;

	nn::gd::Texture2DResourceDescription descr = 
	{
		width,
		height,
		1,
		nn::gd::Resource::NATIVE_FORMAT_RGBA_8888,
		nn::gd::Memory::LAYOUT_BLOCK_8,
		nn::gd::Memory::VRAMA
	};

	//when placing a texture in vram, it must be DMAed in through a command list
	g_CommandList.BeginImmediateExecution();
	TextureHandle *th =	TextureHandle::CreateTextureHandle(texbuf,true,descr);
	g_CommandList.EndImmediateExecution();

	th->CreateTexture();
	delete[] texbuf;
	return th;
}

//------------------------
//resources used for render demos
TextureHandle *th32, *th8;
u8* palette;
struct {
	TextureHandle  *pxbg, *bg, *spritesback, *action, *hud;
} stereo;

//-------------------------
//render demos

u8* LoadPalette()
{
	FILE* palf = fopen("rom:/rainbow8bpp.pal","rb");
	u8* palbuf = new u8[256*4];
	fread(palbuf,1,256*4,palf);
	fclose(palf);
	return palbuf;
}

TextureHandle* LoadTexture8()
{
	FILE* rawf = fopen("rom:/rainbow8bpp.bmp8","rb");
	u8 *rawtexbuf = new u8[512*512];
	fread(rawtexbuf,1,512*512,rawf);
	fclose(rawf);

	u8* texbuf = ZigZag8(512,512,rawtexbuf);
	delete[] rawtexbuf;

	nn::gd::Texture2DResourceDescription descr = 
	{
		512,
		512,
		1,
		nn::gd::Resource::NATIVE_FORMAT_LUMINANCE_8,
		nn::gd::Memory::LAYOUT_BLOCK_8,
		nn::gd::Memory::VRAMA
	};

	//when placing a texture in vram, it must be DMAed in through a command list
	g_CommandList.BeginImmediateExecution();
	TextureHandle *th =	TextureHandle::CreateTextureHandle(texbuf,true,descr);
	g_CommandList.EndImmediateExecution();

	th->CreateTexture();
	delete[] texbuf;
	return th;
}

void SetPalette(u8* paletteRGBA256)
{
	//generate 12bits RGB into 3 tables from the 8bits palette
	//I put some comments in here but they're pretty sketchy. The principles are sound but the details are baffling. This stuff Just Kind Of Works Somehow.
	
	//Warning: the LUT in our case is a "sampling value for input between -1.0 and 1.0) (see Figure 12-3 in BasicGraphics doc)
	//This is ordered a bit unexpectedly.
	//It's -1.0 to 1.0 because that's the GPU's intepretation of the bump map sampling
	
	u32 lut[3][256]; //kind of big for a stack, watch out
	for(int i=128,j=0;i<256+128;i++,j+=4)
	{
		u8 idx = i;
		
		//Note: the LUTs are basically [0..FFF] 0.12 fixed point.
		//this exact formulation is necessary to get the exactly-desired output (Uhhh maybe off by no more than +/-1, I can't remember my results)
		//it depends on the LUT outputs being scaled by 2 later.
		//without that, index values >= 128 will be one too low on the 3ds due to the LUT not being able to represent a full 1.0
		//using the below logic we can trick it into representing the values we need by letting the scale bump us up to 1.0
		//Maybe using the delta values part of the LUTs (assumed to be initialized to 0 here; maybe that isn't safe?) could help us out.
		lut[0][idx] = (paletteRGBA256[j+0]<<3)+(paletteRGBA256[j+0]>>5)+4;
		lut[1][idx] = (paletteRGBA256[j+1]<<3)+(paletteRGBA256[j+1]>>5)+4;
		lut[2][idx] = (paletteRGBA256[j+2]<<3)+(paletteRGBA256[j+2]>>5)+4;
	}

	nn::gd::LightingStage::UploadLookUpTableNative(nn::gd::LightingStage::LOOKUP_TABLE_UPLOAD_ID_D0,0,&lut[0][0],256);
	nn::gd::LightingStage::UploadLookUpTableNative(nn::gd::LightingStage::LOOKUP_TABLE_UPLOAD_ID_D1,0,&lut[1][0],256);
	nn::gd::LightingStage::UploadLookUpTableNative(nn::gd::LightingStage::LOOKUP_TABLE_UPLOAD_ID_FR,0,&lut[2][0],256);
}

void PalettedRenderOncePerFrame()
{
	//Once per-frame setup (protip: re-initialize all GD registers each frame unless you're advanced and able to cache it all somehow)
	//Naturally, if you reuse any of these features elsewhere in your engine, you may have to move setup to the per-object handling

	//select layer configuration 3: D0,D1,FR
	//rationale: D0 gets combined with Specular0, and D1 gets combined with Specular1
	//By choosing these two LUTs, we can use Specular0 and Specular1 as a mask to select each LUT into the color channel we want
	//For the remaining channel, we could choose Layer configuration 2 (and get RR) or Layer configuration 3.
	//FR is more convenient than RR, as you can actually output it directly to the combiners (albeit into the A component, where you'll need to put it back into one of R,G,B)
	//What's more, layer configuration 3 costs only 1 cycle, the mininmum cost for lighting.
	nn::gd::LightingStage::SetLayerConfiguration(nn::gd::LightingStage::LAYER_CONFIGURATION_3);

	//output of FR lut is put into the combiner as A. We'll actually want it to be B, though, so we'll move it in the combiner.
	nn::gd::LightingStage::SetFresnelSelector(nn::gd::LightingStage::FRESNEL_SELECTOR_TYPE_SECONDARY_ALPHA_FRESNEL);

	//Set specular colors as 'masks' to direct the corresponding D0 or D1 LUT value to the desired color channel in combiners
	nn::gd::LightingStage::light[0].SetColorSpecular0(255,0,0);
	nn::gd::LightingStage::light[0].SetColorSpecular1(0,255,0);

	//these features would just make things more complicated
	nn::gd::LightingStage::light[0].EnableShadowed(GD_FALSE);
	nn::gd::LightingStage::light[0].EnableSpotLight(GD_FALSE);
	nn::gd::LightingStage::light[0].EnableDistanceAttenuation(GD_FALSE);
	nn::gd::LightingStage::EnableLookUpTableReflection(GD_FALSE);
	nn::gd::LightingStage::SetClampHightlight(GD_FALSE); //disable `f` factor in specular calculation

	//enable the critical LUTs that we need
	nn::gd::LightingStage::EnableLookUpTableD0(GD_TRUE);
	nn::gd::LightingStage::EnableLookUpTableD1(GD_TRUE);

	//due to the way we're converting palette colors, we need this setup (see SetPalette for details)
	nn::gd::LightingStage::SetLookUpTableOutputScaling(nn::gd::LightingStage::LOOKUP_TABLE_ID_D0,nn::gd::LightingStage::OUTPUT_SCALE_VALUE_2); //will be used for R
	nn::gd::LightingStage::SetLookUpTableOutputScaling(nn::gd::LightingStage::LOOKUP_TABLE_ID_D1,nn::gd::LightingStage::OUTPUT_SCALE_VALUE_2); //will be used for G
	nn::gd::LightingStage::SetLookUpTableOutputScaling(nn::gd::LightingStage::LOOKUP_TABLE_ID_FR,nn::gd::LightingStage::OUTPUT_SCALE_VALUE_2); //will be used for B
}

void RenderMainScreenStereoTest(int eye)
{
	//exercises:
	//1. Try taking the semitransparent glass out of the windows! It kills your eyes
	//2. Beware tiled backdrops! These things look awful in stereo.
	//3. Notice how there's ragged edge of the world at the left before you start scrolling. 
	//   Always make 20px extra content! (if your maximum parallax is 20 pixels)
	//4. With this "negative hud" it's crucially important not to run content off the edge. 
	//   Try scrolling the bitmap into the edge of the screen and see how lousy it looks
	//   But the negative HUD is nice to keep it from conflicting with your action layers.
	//   Just don't bring the action layers thruogh the screen (It's awful)

	int stereoLevel = GetDiscreteUserStereoLevel(4);
	
	//don't do this test if the stereo level is 0 (it would work just fine, but I wanted an easy way to switch tests)
	if(stereoLevel == 0) return;

	nn::gd::TextureStage::SetSamplerState(nn::gd::TextureStage::TEXTURE_UNIT_0, sampler);
	nn::gd::OutputStage::SetBlendState(blendNormal);

	//concept: a naive approach is to simply set ratios here and then multiply it into a basic ~20px scaler.
	//for instance, have the parallax BG at 100% distance, the BG at 50% distance, the players at 40%, etc.
	//That's what we did originally in Mutant Mudds.
	//However, there's a big problem with this:
	//It isn't a problem with soft filtered art (e.g. steamworld),
	//but with pixel art, since you can't shift it by fractional pixels, stereo planes will collapse onto each other unattractively.
	//My approach is to turn the user's stereo slider level into 'discrete' levels with handcrafted pixel-perfect depths
	//Here you will see I allow the parallax BG to go way back, while keeping the main playfield close together
	//Additionally, I keep the player exactly on the surface of the screen (with a value of 0)
	//And I keep the candles always exactly one pixel in front of the BG. 
	//I generally reserve high levels for "The player wants STEREO, so lets give it to him"
	//and moderate levels for a more classy effect.
	//Finally, it's important to have non-parallaxing BG planes with different stereo levels be very very similar in stereo level
	//Otherwise your eyes sees them at a different depth and expects them to parallax (and they don't, so it's ugly)
	static const s8 stereotable[][5] = {
		{0,  0, 0, 0, 0},
		{-1, 0, 1, 2, 3}, //the minimum amount of stereo that can be recognized
		{-1, 0, 1, 2, 10}, //a large amount of stereo
		{-2, 0, 2, 3, 14}, //the most amount of stereo I ever use (well, I use 20, but this backdrop can't handle it)
	};

	int eyea = eye==0?-1:1;
	int pxscroll = (int)(0.75f*gameplay.xscroll);
	
	qr.SetModulateColor(0,42,136,255);
	qr.FillRectangle(0,0,400,240);
	qr.SetModulateColorWhite();

	//Oh yes, one final thing.
	//I set the stereo adjustment in the shader so I don't have to move every object.
	int xs = -pxscroll;
	qr.SetStereoAdjust(eyea*stereotable[stereoLevel][4]);
	qr.DrawSprite(stereo.pxbg,xs,0,0,0,256,256);
	qr.DrawSprite(stereo.pxbg,xs+256,0,0,0,256,256);
	qr.DrawSprite(stereo.pxbg,xs+512,0,0,0,256,256);
	
	xs = -gameplay.xscroll;
	qr.SetStereoAdjust(eyea*stereotable[stereoLevel][3]);
	qr.DrawSprite(stereo.bg,xs,0,0,0,256,256);
	qr.DrawSprite(stereo.bg,xs+256,0,0,0,256,256);
	qr.DrawSprite(stereo.bg,xs+512,0,0,0,256,256);
	
	xs = -gameplay.xscroll;
	qr.SetStereoAdjust(eyea*stereotable[stereoLevel][2]);
	qr.DrawSprite(stereo.spritesback,xs,0,0,0,256,256);
	qr.DrawSprite(stereo.spritesback,xs+256,0,0,0,256,256);
	qr.DrawSprite(stereo.spritesback,xs+512,0,0,0,256,256);

	xs = -gameplay.xscroll;
	qr.SetStereoAdjust(eyea*stereotable[stereoLevel][1]);
	qr.DrawSprite(stereo.action,xs,0,0,0,256,256);
	qr.DrawSprite(stereo.action,xs+256,0,0,0,256,256);
	qr.DrawSprite(stereo.action,xs+512,0,0,0,256,256);

	xs = 0;
	qr.SetStereoAdjust(eyea*stereotable[stereoLevel][0]);
	qr.DrawSprite(stereo.hud,xs,40,0,0,256,256);

	qr.SetStereoAdjust(0);
}


void RenderMainScreen(int eye)
{
	//we'll demo the paletted mode on this screen
	//note: the combiner doesn't 

	PalettedRenderOncePerFrame();

	//---------------
	//Once per palette/object setup:
	//this is the key magic: a bump map is sampled before the lighting, so we can effect a dependent texture read by considering the LUTs to be the 2nd texture
	//but defer enabling this until paletted images need rendering--otherwise it causes a texture to get sampled when you dont expect it (drawing solid colors, for example)
	nn::gd::LightingStage::SetBumpMode(nn::gd::LightingStage::BUMPMODE_AS_BUMP, GD_FALSE);
	
	//this too is needed in order to make things work
	nn::gd::CombinerStage::SetTextureCombinerUnitConstantColor(nn::gd::CombinerStage::UNIT0,0,0,255,0);

	SetPalette(palette);
	//---------------

	nn::gd::OutputStage::SetBlendState(blendNormal);

	nn::math::Matrix44 matMainScreenOrthoProj;
	nn::math::MTX44OrthoPivot(&matMainScreenOrthoProj,0,400,240,0,-256,256,nn::math::PIVOT_UPSIDE_TO_TOP);

	qr.Begin(matMainScreenOrthoProj);
	//do a tiny bit of transform just to prove it doesn't break the bumpmap hack approach
	qr.ms.push();
	qr.ms.Translate(8,0);
	qr.ms.Scale(0.96f);
	qr.ms.RotateD(1);
	nn::gd::TextureStage::SetTexture(nn::gd::TextureStage::TEXTURE_UNIT_0, *th8->Texture());
	nn::gd::TextureStage::SetSamplerState(nn::gd::TextureStage::TEXTURE_UNIT_0, sampler);
	qr.PredrawPalettedTexture();
	qr.EmitRectangle<true>(0,0,
		400,240,
		0,400.0f/512.f,
		0,240.0f/512.f,
		false,false);
	qr.ms.pop();

	RenderMainScreenStereoTest(eye);

	qr.End();

	//---------------
	//turn this off when done rendering paletted images to avoid aforementioned interference
	nn::gd::LightingStage::SetBumpMode(nn::gd::LightingStage::BUMPMODE_NOT_USED, GD_FALSE);
}

void RenderSubScreen()
{
	static u16 ang = 0;
	ang += 100;

	REDO:
	static int add = 0, addd = 4;
	add += addd;
	if(add==256) { addd=-4; goto REDO; }
	if(add==0) addd=4;

	static u16 mbidx = 0;
	mbidx += 512;
	float mb = nn::math::SinIdx(mbidx) * 0.85f;

	nn::gd::OutputStage::SetBlendState(blendNone);

	nn::math::Matrix44 matSubScreenOrthoProj;
	nn::math::MTX44OrthoPivot(&matSubScreenOrthoProj,0,320,240,0,-256,256,nn::math::PIVOT_UPSIDE_TO_TOP);
	qr.Begin(matSubScreenOrthoProj);

	qr.ms.push();
	qr.ms.Translate(128,128);
	qr.ms.RotateI(ang);
	qr.ms.Translate(-128,-128);
	qr.SetModulateColor(0,0,255,255);
	qr.FillRectangle(200,0,100,100);
	qr.Flush();
	qr.SetModulateColorWhite();
	nn::gd::TextureStage::SetTexture(nn::gd::TextureStage::TEXTURE_UNIT_0, *th32->Texture());
	nn::gd::TextureStage::SetSamplerState(nn::gd::TextureStage::TEXTURE_UNIT_0, sampler);
	qr.SetAddColor(0,0,add,0);
	qr.SetMasterBrightnessf(mb);
	qr.PredrawTexture();
	qr.EmitRectangle<true>(20,0,
		189,256,
		33/256.f,
		(33+189)/256.f,
		0.0f,
		1.0f
		,true,false); //h-flip it just for fun (in the source art, Mario is facing left)
	qr.ms.pop();

	qr.SetMasterBrightnessf(0);

	qr.End();
}

static void InitSaveData()
{
	//formatting takes place here if needed
	saveManager = new SaveManager(2);

	if(!saveManager->Load("test0",test0,sizeof(test0)))
	{
		//it's a fresh save file. write something
		memset(test0,0,sizeof(test0));
		saveManager->Save("test0",test0,sizeof(test0));
		saveManager->Commit();
	}

	NN_LOG("test0 now %d\n",test0[0]);
}

void game_main()
{
#ifdef USE_NW

	//must be large enough to contain all resources specified by nwsound_load simultaneously
	static const size_t kHeapSizeForFsSoundArchive = 512*1024;
	nwsound_init("rom:/sound/simple.bcsar",kHeapSizeForFsSoundArchive);
	
	#ifdef USE_FS
		//must load Wave Sound Sets
		nwsound_load(WSDSET_VOICE);
		nwsound_load(WSDSET_SE);

		//must load sequence sounds
		//somehow this loads the required bank and wave archives. I don't completely understand that. Seems like it could be more granular.
		nwsound_load(SEQ_MARIOKART);

		//(also check the grouping mechanism; could have used this instead)
		//nwsound_load(GROUP_BGM);

		//can load individual sequence sound set entries, if that's important
		//could load SEQSET_SE instead for all SFX (seems much more useful)
		//don't we need to load BANK_SE for the coin sound? I don't know, I don't use this loading method to be honest. Many games do though.
		nwsound_load(SEQ_COIN);
	#endif

	Voice vStream;

	Voice vSequence;
	nwsound(&vSequence, SEQ_MARIOKART);
	vSequence.SetSequenceTempo(0.5f); //sequences are cool because you can slow them down
	vSequence.SetVolume2(0.8f);


#endif

	qr.Initialize();

	CreateBlendStates();
	CreateBuffers();
	CreateSamplers();

	padReader = new nn::hid::PadReader();

	th32 = LoadTexture32("rom:/mario.bmp",256,256);
	th8 = LoadTexture8();
	palette = LoadPalette();

	stereo.pxbg = LoadTexture32("rom:/stereo_pxbg.bmp",256,256);
	stereo.bg = LoadTexture32("rom:/stereo_bg.bmp",256,256);
	stereo.spritesback = LoadTexture32("rom:/stereo_spritesback.bmp",256,256);
	stereo.action = LoadTexture32("rom:/stereo_action.bmp",256,256);
	stereo.hud = LoadTexture32("rom:/stereo_hud.bmp",256,256);

	//clear display buffers to black before turning on the screens!
	g_CommandList.BeginImmediateExecution();
	for(int i=0;i<3;i++) sDisplayBuffers[i].Clear();
	g_CommandList.EndImmediateExecution();
	//the above clear commands will be completed now; it's safe to turn on the screens
	nngxSetDisplayMode(NN_GX_DISPLAYMODE_STEREO); //or NN_GX_DISPLAYMODE_NORMAL if you don't care about stereo
	nngxStartLcdDisplay();

	//it's probably smart to make this happen after the LCD is setup, in case there are errors
	//I *think* that's been happening in my games, so that's why I put it here.
	InitSaveData();

	for(;;)
	{
		//tick
		nn::hid::CTR::PadStatus ps;
		padReader->ReadLatest(&ps);
		if(ps.hold & nn::hid::BUTTON_RIGHT) gameplay.xscroll++;
		if(ps.hold & nn::hid::BUTTON_LEFT) gameplay.xscroll--;
		if(gameplay.xscroll > 256) gameplay.xscroll = 256;
		if(gameplay.xscroll < 0) gameplay.xscroll = 0;

		//--------
		//sound demos 
		#ifdef USE_NW
			static bool streaming = false;
			if(ps.trigger & nn::hid::BUTTON_UP)
			{
				//2 ways to play; you can lose the voice anytime if you don't need it
				Voice v;
				nwsound(SE_YOSHI);
				nwsound(&v,SEQ_COIN);
			}
			if(ps.trigger & nn::hid::BUTTON_DOWN)
			{
				if(streaming)
				{
					vSequence.Pause(false,60);
					vStream.Stop(60);
					streaming = false;
				}
				else
				{
					vSequence.Pause(true,60);
					nwsound(&vStream,STRM_STARCHART);
					streaming = true;
				}
			}
			static u16 idx = 0;
			idx += 200;
			float pan = nn::math::SinIdx(idx) * 0.85f;
			vSequence.SetPan(pan);
		#endif

		if(ps.trigger & nn::hid::BUTTON_L)
		{
			test0[0]++;
			saveManager->Save("test0",test0,sizeof(test0));
			saveManager->Commit();
		}
		if(ps.trigger & nn::hid::BUTTON_R)
		{
			saveManager->Load("test0",test0,sizeof(test0));
			NN_LOG("test0 now %d\n",test0[0]);
		}
		if(ps.trigger & nn::hid::BUTTON_X)
		{
			static bool toggle = false;
			toggle ^= true;

			//warning: don't do this between rendering and EndFrame (or in the midst of rendering) as some small flashes of nonsense will happen
			//(that's because the smooth render-and-swapping flow is interrupted by these calls which take control of the system)
			if(toggle)
				ErrEula_DisplayErrorText(L"But that's what you wanted, right?\nBy the way, newline.");
			else
				ErrEula_DisplayEulaAgreement();
		}

		//main screen
		for(int i=0;i<2;i++)
		{
			sFrameBuffers[0].ClearColor(255,0,0,0);
			nn::gd::Viewport mainViewport(0,0,240,400);
			nn::gd::RasterizerStage::SetViewport(mainViewport);
			sFrameBuffers[0].Bind();
			RenderMainScreen(i);
			sFrameBuffers[0].TransferTo(&sDisplayBuffers[i==0?SCREEN_LEFT:SCREEN_RIGHT]);
		}
		
		//--------
		//sub screen
		sFrameBuffers[0].ClearColor(0,255,0,0);
		nn::gd::Viewport subViewport(0,0,240,320);
		nn::gd::RasterizerStage::SetViewport(subViewport);
		sFrameBuffers[0].Bind();
		RenderSubScreen();
		sFrameBuffers[0].TransferTo(&sDisplayBuffers[SCREEN_SUB]);

		//-----------
		#ifdef USE_NW
			nwsound_update();
		#endif
		EndFrame();
		qr.NewFrame();

	}
}
