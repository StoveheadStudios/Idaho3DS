** HOW TO USE DEMO **

1. Use Release_NW/ctreq60_NW.cci or run MakeCIA.bat to get a CIA file of that
2. Use left/right to scroll the Castlevania scene
3. Use up to play SFX
4. Use down to toggle stream/sequence
5. Turn on and off the stereo slider to see completely different top screen demos.
6. Press L to save data and R to load it
7. Press X to launch ErrEula test

** HOW TO BUILD DEMO **
1. Run build_nwsound_project.bat
2. Choose Debug_NW (includes sound) or Debug configuration in visual studio

** AFTER THAT, HOW TO BUILD ROMS **
3. Choose Release_NW in visual studio (that's what I set up the build scripts for) and build it
4. Customize files in banner directory
5. Re-evaluate the latest e-manual requirements. You'll have to evaluate the e-manual cookbooks and manual tool's docs for help
6. Export to build\manual\export\Manual.bcma (case is important)
7. run build\MakeCIA.bat and you now have a .CIA

NOTE: It may be possible to build the CIA from visual studio, including manual and banner, but I don't do that.
I create a CIA file from the build output as a separate step, because building 3ds binaries in VS is already complex enough.

** HOW TO USE CODE **

remove this from the linker inputs (it conflicts with rvct_stdio.cpp):
librtport.$(CTROptimizeOption).a

Set save data options correctly in RSF file. 
(I'm suggesting to use MediaType: Card2 for downloadable games (it's more flexible), but at the time I wrote this, my first game to do that has not fully passed lotcheck)

1. nninitStartUp() in mem.cpp - memory is setup, before static initializers
2. nnmain() in ctreq60.cpp
3. game_main is called

now your game should do this each frame:

1. Bind framebuffers
2. Draw
3. Transfer to display buffers
4. Call EndFrame
5. Flip double-buffering on quad-renderer
6. call nwsound_update(), if it's using NW's sound

** Notable features: ** 

* Heap setup with my recommended almost-all-device-memory approach and vram allocators
* Demo of flushing all memory once a frame (this engine relies on it, and it works, and it's easy)
* Activate stdio FILE* (saves a lot of trouble while porting software!)
* Quad-renderering geometry shader (performance booster)
* Matrix stack and double-buffered quad renderer (just how I like to do things)
* Basic GD demo
* Battle-tested (until it was stripped down) commandlist doublebuffering (In other words, I can't fully guarantee it in this condition.)
* Block-8 texture swizzling algorithm (better placed on your toolchain than in c++)
* 8bpp Paletted texture rendering (under limited conditions)
* Exhibits "MasterBrightness" concept (mimics how the NDS does, which is a convenient way of fading screens by setting a parameter at the end of all combiners)
* Demos turning on screens (and clearing them before turning them on, which is important)
* Stereo slider support, "Discrete Stereo" approach, and implementing stereo adjustment in shaders
* Pad reading, not that it's very hard
* NW sound demos (reworked/cleaned considerably from my engine)
* Logic for dumping ORCS display lists integrated, if ORCS is installed into SDK as part of 3ds graphics debugger package (and define HAVE_ORCS)
* SaveData best practices, ErrEula utility wrappers

** Backgrounder on quad-rendering geometry shader **

* In order to reduce the load required to create vertex buffers on the CPU, more compact commands can be sent to the geometry shader for drawing quads.
  - For instance, simply x,y,w,h,umin,umax,vmin,vmax will suffice; this is half as much data as the naive (x,y,u,v)x4 approach would require.
* This may also reduce the GPU's vertex reading load on some games (for a simple 2d engine, the vertex processor will be underutilized since it's been optimized for more complex 3d calculations, so the vertex reading will be hit harder)

** Backgrounder on 8bpp paletted textures **

* 8bpp paletted images can be rendered with the following caveats
1. It takes over the GPU fragment lighting engine, pretty much
2. Changing a 256 entry palette requires 3KB of data put in the command queue. This is not small!
3. Requires a combiner slot it will be nearly impossible to merge with other techniques
4. Transparent color 0 is supported optionally, but requires 3 combiner slots instead of the one.
   Note: I don't use the alpha test for this.
   You could choose A as I, and discard A==0 (to effect a discard of I==0) but only if you were drawing the object without blending (for fading out the object).
   With blending enabled the alpha would be coming from the index channel, which is kind of nonsense.
   If this doesn't matter to you, you could save some trouble and use it instead)

In practice, it's useful for rendering large images that look better than RGBA5551 while using less RAM. Maybe it's also useful for speeding up rendering by fitting more in VRAM? That's hard to pull off on 2d stuff, in my experience.

Also of course it is useful for old 8 and 16bit palette effects in a 2d game. (I haven't used it this way yet)

Due to the constraints put on the lighting engine it would not be suitable for 3D.

However, if you were desperate, you could use it to render-to-texture to recolor a player skin, or somesuch.

THE BASIC PRINCIPLES:
* The texture is used as a bumpmap. This is because it's the only way the hardware lets us effectively perform a dependent texture read: the bumpmap is sampled first, and then LUTs are sampled afterwards.
* Naturally, the palette goes in the LUTs.
* By carefully configuring the lighting engine, we can make it almost in an 'identity transform' mode from the bumpmap up to the point where the values pass through the LUTs.
* You may be amazed that this works. But the lighting engine has just enough internal precision to pull it off (it's either exact, or within +/- 1 of the desired value, which is as good as it gets sometimes on the 3DS)
* From the LUTs, the values go into the combiner where it's ready to be used.
* One problem: the alpha channel can't get handled normally.
* What we have to do for that is sample the texture AGAIN in the combiner as a typical RGBA source, and take advantage of the fact that I=0 would be transparent (it will show up as either (0,0,0,0) or (0,0,0,255), I can't remember which. In any case we use the R value)
* Then, using 3 combiner stages' integer math and saturation characteristics, we can turn I=0 into 0 and I=X where X!=0 into 255 (all indices nonzero are now fully opaque)

* * * *
Warning: the 'quaternion' and 'view' vectors output by the vertex shader must be set correctly to maintain the 'identity transform' condition. 
You may discover it isn't necessary to set them away from the defaults* (indeed it isn't actually in this demo) but under other circumstances it might be.
See this implemented in QuadRender_GShader.vsh
The choice of vectors there is dependent on how a Luminance8 texture gets read by the bumpmap engine. I think it's as an X (-1,1) Y,Z=0 vector, but I can't remember. Regardless, the choice of vectors fixes it.
*It is not clear what the "defaults" are for the view and quaternion vectors in the GPU. It shouldn't be relied upon. This may even be one of the annoying cases where the devkit resets them every time the devmenu runs but a panda leaves them at garbage from the rendering of home menu stuff.
* * * *

* Future work: palettes could be turned into completely prebaked static display lists and be run as command jumps, to save some CPU processing time. It's probably still pretty relatively heavy on the GPU side though.
* Future work: demo of dynamic texture (in fcram)

** Backgrounder on NW sound **

You can use NW sound archives straight from the ROM, or loaded entirely into memory (streams always come straight from the rom).
If you load straight from ROM (FsSoundArchive) then you must manually manage (via nwsound_load calls) which resources are loaded into memory for playback.
If you load entirely ento memory (MemorySoundArchive) then you must have room to keep the sound archive loaded perpetually. 

I've defaulted this to use a MemorySoundArchive. 
Enable USE_FS in nwsound.h in order to use FsSoundArchive -- and then inspect the nwsound_load calls carefully in game_main.cpp.

PRO TIP: If you're going to load the entire bcsar into a MemorySoundArchive, why not compress it on your ROM FS?

** CHANGELOG ** 

1.8
* Add example banner project
* Add example CIA build script for consolidating binaries with manual and banner to get a final .CIA

1.7
* Add ORCS commandlist dumping when GX times out
* Add ErrEula utility module
* Add SaveManager utility module

1.6
* Fix NW finalizing when memory archives are used...

1.5
* Fix NW finalizing (and actually call it from the applet exit processing)
* Default it to use MemorySoundArchive like advertised
* Change included CCI to be in its build output spot

1.4
* Add NW sound demos. Use Debug_NW configuration to enable it; be sure to run build_nwsound_project.bat first

1.3
* Add stereo slider support, "Discrete Stereo" approach, and implementing stereo adjustment in shaders
* Demo pad reading, not that it's very hard
* Cleaned up test bitmap loading (no more need for y-flipping). Easier to make more test art.

1.2
* Fix bug: `rMisc` (unused shader functionality) could get clobbered by home menu and result in scrambled rendering
* Fix bugs: home menu / sleep graphics state restoring
* Fix bugs: forgot to turn on the screens! Also need to clear display buffers before turning them on.
* Add MakeCIA.bat, because I have bad luck getting CIA to build from VS sometimes.
* Docs and code cleanup

1.1 
* Added 8bpp paletted demo
* Used MTX44OrthoPivot for projection setup, because it's cooler.
* Add viewport commands which should have been there to begin with.
* Fix and demo add color in QR (not that it's an amazing demo)
* Add masterbrightness demo
* Clean up shaders and fix release build

1.0
* Initial version
