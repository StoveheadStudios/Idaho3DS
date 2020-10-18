#include <stdio.h>

#include "engine.h"
#include "QR.h"

QR qr;

static const int BUFFER_COMMANDS_SIZE = (1<<14); //16k commands (will turn into 5 floats per command)

void QR::Initialize()
{
	CreateCombiners();

	mIsActive = false;

	//initialize shaders
	//1. load
	FILE* shaderfile = fopen("rom:/shaders_CTR.shbin","rb");
	fseek(shaderfile,0,SEEK_END);
	size_t shaderfile_len = ftell(shaderfile);
	fseek(shaderfile,0,SEEK_SET);
	u8 *shaderbuf = new u8[shaderfile_len];
	fread(shaderbuf,1,shaderfile_len,shaderfile);
	fclose(shaderfile);
	shader.Initialize(shaderbuf,shaderfile_len);
	delete[] shaderbuf;

	mShaderPipeline = shader.CreatePipeline(SHADER_INDEX_QUADRENDER_VSHADER,SHADER_INDEX_QUADRENDER_GSHADER);
	shader.SetPipeline(mShaderPipeline);
	uProjection = shader.UniformLocation("uProjection");
	uExtraTexcoords = shader.UniformLocation("uExtraTexcoords");
	
	{
		nn::gd::InputElementDescription descr[] = {
			{ 0, "aCommand", nn::gd::VertexInputStage::STREAM_TYPE_FLOAT, 1, 0},
			{ 0, "aParameters", nn::gd::VertexInputStage::STREAM_TYPE_FLOAT, 4, 4},
		};
		static const u32 strides[] = { 20, 20 };
		nn::gd::VertexInputStage::CreateInputLayout(descr, 2, (u32*)strides, shader.GetShader(SHADER_INDEX_QUADRENDER_VSHADER), &mInputLayout);
	}

	nn::gd::VertexBufferResourceDescription vbdescr = {
		BUFFER_COMMANDS_SIZE*5*4,
		nn::gd::Memory::FCRAM
	};

	for(int i=0;i<2;i++)
	{
		//alas, these can't be statically allocated because they need to be in device memory, and the memory map for that can't be known til the app boots up.
		//it'd be cool if we could dynamically modify code!!!
		//we dont allocate with the main VRAM allocator because it would muck up our metrics
		//allocate 10 extra for safety
		buffers[i] = new float[BUFFER_COMMANDS_SIZE*5 + 10];
		nn::gd::Resource::CreateVertexBufferResource(&vbdescr,buffers[i],&vb[i],GD_FALSE);
	}

	//initialize buffer management
	mBufferFlip = 0;
	bContainsDraw = false;
	NewFrame();
}

void QR::NewFrame()
{
	mBufferFlip ^= 1;
	mCurrPtrFlushed = mCurrPtr = mCurrPtrStart = buffers[mBufferFlip];
	mCurrPtrEnd = buffers[mBufferFlip] + BUFFER_COMMANDS_SIZE*5;
}

void QR::Begin()
{
	//prep shader pipeline
	shader.SetPipeline(mShaderPipeline);
	nn::gd::VertexInputStage::SetInputLayout(mInputLayout);
	nn::gd::VertexInputStage::SetPrimitiveTopology(nn::gd::VertexInputStage::PRIMITIVE_TOPOLOGY_GEOMETRY);
	nn::gd::RasterizerStage::SetCulling(nn::gd::RasterizerStage::CULLING_NONE); 

	gdBindVB(0,vb[mBufferFlip],0);

	//bootstrap combiner states
	SetMasterBrightnessf(0);
	ClearAddColor();

	//bootstrap own logical state
	ms.resetIdentity();

	//bootstap shader
	EmitCommand(0x02,1.0f,1.0f,1.0f,1.0f); //modulate color white
	EmitCommand(0x03,0.0f,0.0f,0.0f,0.0f); //misc: stereo adjust

	//make sure shader tracks our logical state
	SyncRenderState(true);

	mIsActive = true;
}

void QR::End()
{
	if(NeedsFlush()) Flush();
	mIsActive = false;
}

void QR::Flush()
{
	//find out how many commands are buffered
	int lastFlushedIndex = (mCurrPtrFlushed - mCurrPtrStart)/5;
	int lastBufferedIndex = (mCurrPtr - mCurrPtrStart)/5;
	int todo = lastBufferedIndex - lastFlushedIndex;

	//draw the newly buffered portion
	if(todo != 0)
		nn::gd::System::Draw(todo,lastFlushedIndex);

	mCurrPtrFlushed = mCurrPtr;
	bContainsDraw = false;
}

void QR::SetMasterBrightnessf(float value)
{
	if(NeedsFlush()) Flush();
	if(value>0)
	{
		nn::gd::CTR::CombinerStage::SetTextureCombinerUnitConstantColor((nn::gd::CombinerStage::UnitId)4, 255, 255, 255, (int)(value*255));
	}
	else
	{
		nn::gd::CTR::CombinerStage::SetTextureCombinerUnitConstantColor((nn::gd::CombinerStage::UnitId)4, 0, 0, 0, (int)(-value*255));
	}
}

void QR::ClearAddColor()
{
	if(NeedsFlush()) Flush();
	SetAddColor(0,0,0,0);
}

void QR::SetAddColor(u8 r, u8 g, u8 b, u8 a)
{
	if(NeedsFlush()) Flush();
	nn::gd::CTR::CombinerStage::SetTextureCombinerUnitConstantColor((nn::gd::CombinerStage::UnitId)3, r,g,b,a);
}

void QR::CreateCombiners()
{
	//textured:
	{
		nn::gd::CombinerDescription descr;
		
		//modulate texture and primary color
		static const nn::gd::CombinerUnitDescription stage0 = {
			{ nn::gd::CombinerStage::SOURCE_TEXTURE0, nn::gd::CombinerStage::SOURCE_PRIMARY_COLOR, nn::gd::CombinerStage::SOURCE_TEXTURE0 }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_TEXTURE0, nn::gd::CombinerStage::SOURCE_PRIMARY_COLOR, nn::gd::CombinerStage::SOURCE_TEXTURE0 }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_MODULATE,
			nn::gd::CombinerStage::COMBINE_ALPHA_MODULATE,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_1 };
		descr.m_CombinerUnit[0] = stage0;
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT0, GD_TRUE);

		//these stages just pass through
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT1, GD_TRUE);
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT2, GD_TRUE);

		//add color
		static const nn::gd::CombinerUnitDescription stage3 = {
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_FRAGMENT_SECONDARY_COLOR }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_ADD,
			nn::gd::CombinerStage::COMBINE_ALPHA_ADD,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_1 };
		descr.m_CombinerUnit[3] = stage3;
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT3, GD_TRUE);


		//but we do need masterbrightness
		static const nn::gd::CombinerUnitDescription stage4_MB = {
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_ONE_MINUS_SRC_ALPHA },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_INTERPOLATE,
			nn::gd::CombinerStage::COMBINE_ALPHA_REPLACE,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_1 };
		descr.m_CombinerUnit[4] = stage4_MB;
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT4, GD_TRUE);

		nn::gd::CombinerStage::CreateTextureCombinerState(&descr,&combiner_Textured);
	}

	//paletted:
	{
		nn::gd::CombinerDescription descr;

		//Rout = Asecondary*0.0 + RSecondary (which is D0) = RSecondary
		//Gout = Asecondary*0.0 + GSecondary (which is D1) = GSecondary
		//Bout = ASecondary*1.0 + BSecondary (which is 0) = ASecondary (which was FR)
		//Aout = (Rtexture+Rtexture)*4 = Rtexture*8
		static const nn::gd::CombinerUnitDescription stage0 = {
			{ nn::gd::CombinerStage::SOURCE_FRAGMENT_SECONDARY_COLOR, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_FRAGMENT_SECONDARY_COLOR }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_TEXTURE0, nn::gd::CombinerStage::SOURCE_TEXTURE0, nn::gd::CombinerStage::SOURCE_TEXTURE0 }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_R, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_R, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_MULT_ADD,
			nn::gd::CombinerStage::COMBINE_ALPHA_ADD,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_4 };
		descr.m_CombinerUnit[0] = stage0;
		//nn::gd::CombinerStage::SetTextureCombinerUnitConstantColor(nn::gd::CombinerStage::UNIT0,0,0,255,0); //is a prerequisite, but do later
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT0, GD_TRUE);

		//RGBout = RGBprev
		//Aout = (Aprev+Aprev)*4 = Aprev*8 = Rtexture*8*8 = Rtexture*64
		static const nn::gd::CombinerUnitDescription stage1 = {
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_FRAGMENT_SECONDARY_COLOR }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_TEXTURE0 }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_REPLACE,
			nn::gd::CombinerStage::COMBINE_ALPHA_ADD,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_4 };
		descr.m_CombinerUnit[1] = stage1;
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT1, GD_TRUE);

		//RGBout = RGBprev
		//Aout = (Aprev+Aprev)*4 = Aprev*8 = Rtexture*8*8*8 = Rtexture*512 - finally, an Rtexture of 0 would be 0 now, and an Rtexture component of >=1 would be saturated to 255
		static const nn::gd::CombinerUnitDescription stage2 = {
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_FRAGMENT_SECONDARY_COLOR }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_TEXTURE0 }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_REPLACE,
			nn::gd::CombinerStage::COMBINE_ALPHA_ADD,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_4 };
		descr.m_CombinerUnit[2] = stage2;
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT2, GD_TRUE);

		//passthrough
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT3, GD_TRUE);

		//masterbrightness
		static const nn::gd::CombinerUnitDescription stage4_MB = {
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_ONE_MINUS_SRC_ALPHA },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_INTERPOLATE,
			nn::gd::CombinerStage::COMBINE_ALPHA_REPLACE,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_1 };
		descr.m_CombinerUnit[4] = stage4_MB;
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT4, GD_TRUE);

		nn::gd::CombinerStage::CreateTextureCombinerState(&descr,&combiner_Paletted);
	}

	//paletted: (ALTERNATE, SMALLER, IN CASE YOU NEED COMBINER STATES BUT NO TRANSPARENCY [i didnt test this])
	//{
	//	nn::gd::CombinerDescription descr;

	//	static const nn::gd::CombinerUnitDescription stage0 = {
	//		{ nn::gd::CombinerStage::SOURCE_FRAGMENT_SECONDARY_COLOR, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_FRAGMENT_SECONDARY_COLOR }, // Color sources
	//		{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_PREVIOUS }, // Alpha sources
	//		{ nn::gd::CombinerStage::OPERAND_RGB_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR },
	//		{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_R, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_R, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
	//		nn::gd::CombinerStage::COMBINE_RGB_MULT_ADD,
	//		nn::gd::CombinerStage::COMBINE_ALPHA_REPLACE, //doesnt really matter, you shouldn't be plending anyway
	//		nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_1 };
	//	descr.m_CombinerUnit[0] = stage0;
	//	descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT0, GD_TRUE);

	//	//passthrough - you see, stages 1 and 2 were only used to multiply the opaque alphas out to 255
	//	descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT1, GD_TRUE);
	//	descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT2, GD_TRUE);
	//	descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT3, GD_TRUE);

	//	//masterbrightness
	//	static const nn::gd::CombinerUnitDescription stage4_MB = {
	//		{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Color sources
	//		{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Alpha sources
	//		{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_ONE_MINUS_SRC_ALPHA },
	//		{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
	//		nn::gd::CombinerStage::COMBINE_RGB_INTERPOLATE,
	//		nn::gd::CombinerStage::COMBINE_ALPHA_REPLACE,
	//		nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_1 };
	//	descr.m_CombinerUnit[4] = stage4_MB;
	//	descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT4, GD_TRUE);

	//	nn::gd::CombinerStage::CreateTextureCombinerState(&descr,&combiner_Paletted);
	//}

	//untextured:	
	{
		nn::gd::CombinerDescription descr;
		
		static const nn::gd::CombinerUnitDescription stage0 = {
			{ nn::gd::CombinerStage::SOURCE_PRIMARY_COLOR, nn::gd::CombinerStage::SOURCE_PRIMARY_COLOR, nn::gd::CombinerStage::SOURCE_FRAGMENT_SECONDARY_COLOR }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_PRIMARY_COLOR, nn::gd::CombinerStage::SOURCE_PRIMARY_COLOR, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_REPLACE,
			nn::gd::CombinerStage::COMBINE_ALPHA_REPLACE,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_1 };
		descr.m_CombinerUnit[0] = stage0;
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT0, GD_TRUE);

		//these stages just pass through
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT1, GD_TRUE);
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT2, GD_TRUE);
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT3, GD_TRUE);

		static const nn::gd::CombinerUnitDescription stage4_MB = {
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Color sources
			{ nn::gd::CombinerStage::SOURCE_PREVIOUS, nn::gd::CombinerStage::SOURCE_CONSTANT, nn::gd::CombinerStage::SOURCE_CONSTANT }, // Alpha sources
			{ nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_SRC_COLOR, nn::gd::CombinerStage::OPERAND_RGB_ONE_MINUS_SRC_ALPHA },
			{ nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA, nn::gd::CombinerStage::OPERAND_ALPHA_SRC_ALPHA },
			nn::gd::CombinerStage::COMBINE_RGB_INTERPOLATE,
			nn::gd::CombinerStage::COMBINE_ALPHA_REPLACE,
			nn::gd::CombinerStage::SCALE_1, nn::gd::CombinerStage::SCALE_1 };
		descr.m_CombinerUnit[4] = stage4_MB;
		descr.SetCombinerInUse(nn::gd::CombinerStage::UNIT4, GD_TRUE);

		nn::gd::CombinerStage::CreateTextureCombinerState(&descr,&combiner_Untextured);
	}
}