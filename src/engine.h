#pragma once

#include <stdlib.h>

#include <nn/types.h>
#include <nn/gx.h>
#include <nn/gd.h>

class noncopyable
{
protected:
	noncopyable() {}
	~noncopyable() {}
private:  // emphasize the following members are private
	noncopyable( const noncopyable& );
	const noncopyable& operator=( const noncopyable& );
};

#define VERTEXSHADER 0
#define GEOMETRYSHADER 1

class DisplayBuffer
{
public:
	void Initialize(GLenum which, int width, int height,  nn::gd::CTR::Resource::NativeFormat format = nn::gd::CTR::Resource::NATIVE_FORMAT_RGB_888, nn::gd::Memory::MemoryLocation loc = nn::gd::Memory::VRAMB);
	
	void Finalize()
	{
		//bind no display buffer so we can safely delete them
		nngxBindDisplaybuffer(0);
		for(int i=0;i<2;i++)
		{
			nngxDeleteDisplaybuffers(2, mBufferIds);
		}
	}

	void BindBackBuffer()
	{
		nngxActiveDisplay(mTargetDisplay);
		nngxBindDisplaybuffer(mBufferIds[mCurBackBufferIndex]);
	}

	void Increment()
	{
		mCurBackBufferIndex++;
		if(mCurBackBufferIndex==2) mCurBackBufferIndex=0;
	}

	void Clear();

	void* mBufferPointers[2];
	GLenum mRequestedMemoryLocation;
	GLenum mFormat;
	int mFormatWidth;
	GLuint mBufferIds[2];
	GLenum mTargetDisplay;
	int mCurBackBufferIndex;
	nn::gd::CTR::Resource::NativeFormat mGDFormat;
	int mHeight, mWidth;
};

class FrameBuffer
{
public:

	FrameBuffer()
			: mColorBuffer(NULL)
			, mDepthBuffer(NULL)
			, mRenderTarget(NULL)
			, mDepthTarget(NULL)
	{
	}

	void ClearColor(u8 r, u8 g, u8 b, u8 a)
	{
		const u8 clearColor[] = {r,g,b,a};
		nn::gd::Memory::ClearTargets(mRenderTarget, NULL, clearColor, 0, 0);
		nngxFlush3DCommandNoCacheFlush();
	}

	void ClearDepth(float depth, u8 stencil)
	{
		static const u8 clearColor[] = {0,0,0,0};
		nn::gd::Memory::ClearTargets(NULL, mDepthTarget, clearColor, depth, stencil);
		nngxFlush3DCommandNoCacheFlush();
	}
	
	void ClearColorAndDepth(u8 r, u8 g, u8 b, u8 a, float depth, u8 stencil)
	{
		const u8 clearColor[] = {r,g,b,a};
		nn::gd::Memory::ClearTargets(mRenderTarget, mDepthTarget, clearColor, depth, stencil);
		nngxFlush3DCommandNoCacheFlush();
	}

	//if we ever need this idea
	//void ShareDepthBufferFrom(FrameBuffer* shareTarget)
	void InitializeDepthBuffer(int width, int height, nn::gd::Resource::NativeFormat format = nn::gd::Resource::NATIVE_FORMAT_DEPTH_24_STENCIL_8, nn::gd::Memory::MemoryLayout layout = nn::gd::Memory::LAYOUT_BLOCK_8, void* addr = NULL);
	
	void SharedRTInitialization();
	void Initialize(int width, int height, nn::gd::Resource::NativeFormat format = nn::gd::Resource::NATIVE_FORMAT_RGBA_8888, nn::gd::Memory::MemoryLayout layout = nn::gd::Memory::LAYOUT_BLOCK_8, nn::gd::Memory::MemoryLocation memloc = nn::gd::Memory::VRAMA, void* addr = NULL);
	void InitializeCastFrom(FrameBuffer* source, nn::gd::Resource::NativeFormat format, nn::gd::Memory::MemoryLayout layout);

	void Finalize()
	{
		nn::gd::OutputStage::ReleaseRenderTarget(mRenderTarget);
	}

	void Bind()
	{
		nn::Result res = nn::gd::OutputStage::SetRenderTarget(mRenderTarget);
		if(!res.IsSuccess())
		{
			NN_LOG("failed set render target\n");
		}

		if(mDepthTarget)
		{
			res = nn::gd::OutputStage::SetDepthStencilTarget(mDepthTarget);
			if(!res.IsSuccess())
			{
				NN_LOG("failed set render target\n");
			}
		}
	}

	nn::gd::Texture2DResource* mColorBuffer, *mDepthBuffer;
	nn::gd::RenderTarget* mRenderTarget;
	nn::gd::DepthStencilTarget* mDepthTarget;
	nn::gd::CTR::Texture2DResourceProperties mColorBufferProperties, mDepthBufferProperties;

	void TransferTo(DisplayBuffer* dispBuf, nn::gd::Memory::DownScalingMode downscaleMode = nn::gd::Memory::DOWNSCALING_NONE);
};

//encapsulates the CTR command list system, which is the main method of streaming commands to the GPU
class CommandList
{
public:
	CommandList()
		: mExecutingListIndex(-1)
	{
	}

	void Initialize()
	{
		//generate two command lists for double buffering
		nngxGenCmdlists(2, mCommandListId);

		for(int i=0;i<2;i++)
		{
			//bind a list so we can configure it
			nngxBindCmdlist(mCommandListId[i]);
			//not very sure what the 128 "requestcount" is all about yet. 
			//think its the number of 'blocks'?? (each split creates a block, or a 'command request' so thats probably what this is
			nngxCmdlistStorage(1024*1024, 128); 
			//continue configuring. this is just boilerplate stuff
			nngxSetCmdlistParameteri(NN_GX_CMDLIST_RUN_MODE, NN_GX_CMDLIST_SERIAL_RUN);
		}

		//start the first commandlist. it wont have anything to do, though.
		mExecutingListIndex = 1;
		SwitchCommandLists();
	}

	int mExecutingListIndex;

	//switches the current commandlist to be the executing one.
	//that way, you can wait for the commandlist to be finished and be sure that a command youre interested in finished executing (i.e. for resource loading)
	void BeginImmediateExecution()
	{
		nngxBindCmdlist(mCommandListId[mExecutingListIndex]);
	}

	//waits for the executing commandlist to finish, and switches context back to the buffered commandlist
	void EndImmediateExecution()
	{
		WaitForExecutingCommandList();
		BeginBufferedExecution();
	}

	//switches the current commandlist to be the buffered one. (normal mode)
	void BeginBufferedExecution()
	{
		int bufferingCommandList = mExecutingListIndex ^ 1;
		nngxBindCmdlist(mCommandListId[bufferingCommandList]);
	}

	void WaitForExecutingCommandList()
	{
		nngxBindCmdlist(mCommandListId[mExecutingListIndex]);
		nngxWaitCmdlistDone();
		nngxClearCmdlist();
	}

	void WaitForExecutingCommandListAndStop()
	{
		WaitForExecutingCommandList();
		nngxStopCmdlist();
	}

	void SwitchCommandLists()
	{
		//the next commandlist shall now be executing
		mExecutingListIndex ^= 1;
		
		//make sure its `flushed` (displaylist terminated) before running
		nngxBindCmdlist(mCommandListId[mExecutingListIndex]);
		nngxFlush3DCommandNoCacheFlush();
		//and then run it:
		nngxRunCmdlist();

		//the other commandlist shall now be buffering
		BeginBufferedExecution();
	}

	void Finalize()
	{
		//TODO
	}

	//gets the used buffer size
	int GetUsedBufSize()
	{
		int x;
		nngxGetCmdlistParameteri(NN_GX_CMDLIST_USED_BUFSIZE,&x);
		return x;
	}

	GLuint mCommandListId[2];
};


class TextureHandle
{
public:
	TextureHandle()
		: mBufferToFree(NULL)
		, mResource(NULL)
		, mTexture(NULL)
	{}

	void CreateResource(void* buffer, bool allocate, const nn::gd::Texture2DResourceDescription& resDescr);

	//creates a texture handle from the specified resource description.
	//if allocate is true, this texturehandle will perform an allocation and copy from your supplied buffer
	//if allocate is false, this texturehandle will retain the supplied buffer and take charge of freeing it.
	//if allocate is false and buffer is false, then you better fix this code not to assert on it
	//TODO - wish this didnt have to do a heap operation
	static TextureHandle* CreateTextureHandle(void* buffer, bool allocate, const nn::gd::Texture2DResourceDescription& resDescr)
	{
		NN_ASSERT(allocate || buffer);

		TextureHandle* ret = new TextureHandle();
		ret->CreateResource(buffer,allocate,resDescr);
		ret->mWidth = (int)resDescr.m_Width;
		ret->mHeight = (int)resDescr.m_Height;

		return ret;
	}

	int mMipLevels;

	//release ownership of the buffer
	void* DetachBuffer()
	{
		void* ret = mBufferToFree;
		mBufferToFree = NULL;
		return ret;
	}

	void CreateTexture()
	{
		//im not sure what at all this is, but you need it to bind a texture to a sampler.
		//i think its just caching the register configuration for that texture resource
		nn::gd::Texture2DDescription texDescr = { 0, -1 }; //0,-1 appears to automatically use all available mipmap levels, with 0 being smallest, and -1 being a magicnumber for the largest
		nn::gd::TextureStage::CreateTexture2D(mResource,&texDescr,&mTexture);
	}

	~TextureHandle()
	{
		if(mTexture != NULL) nn::gd::TextureStage::ReleaseTexture2D(mTexture);
		mTexture = NULL;

		if(mResource != NULL) nn::gd::Resource::ReleaseTexture2DResource(mResource);
		mResource = NULL;
		
		if(mBufferToFree != NULL) free(mBufferToFree);
		mBufferToFree = NULL;
	}

	//TODO PUBLIC
	//Buffer GetResourceBuffer() const;

	void* mBufferToFree;
	nn::gd::Texture2DResource* mResource;
	nn::gd::Texture2D* mTexture;

	nn::gd::Texture2DResource* Resource() { return mResource; }
	nn::gd::Texture2D* Texture() { return mTexture; }

	int mWidth, mHeight;
};

	//encapsulates a single shbin and provides access to the shaders (and uniform tables) within
template<int NSHADERS>
class Shader
{
public:

	static const int kMaxShaders = NSHADERS;

	void Initialize(void* ptr, size_t size)
	{
		//create shader binary from supplied data
		nn::gd::ShaderStage::CreateShaderBinary(ptr, size, &mShaderBinary);
	}

	nn::gd::Shader* mShaders[kMaxShaders];
	nn::gd::ShaderBinary* mShaderBinary;
	int mNumShaders;

	nn::gd::Shader* GetShader(int index)
	{
		NN_ASSERT(index >= 0 && index < kMaxShaders);
		return mShaders[index];
	}

	nn::gd::ShaderPipeline* CreatePipeline(int vertexShaderIndex, int geometryShaderIndex, const nn::gd::CTR::ShaderPipelineUnmanagedRegisters* unmanagedRegs = NULL)
	{
		NN_ASSERT(vertexShaderIndex >= 0 && vertexShaderIndex < kMaxShaders);
		NN_ASSERT(geometryShaderIndex >= 0 && geometryShaderIndex < kMaxShaders  || geometryShaderIndex == -1);

		nn::gd::Shader **vs, **gs = NULL;
		vs = &mShaders[vertexShaderIndex];
		if(geometryShaderIndex != -1)
			gs = &mShaders[geometryShaderIndex];

		//create shaders if necessary
		if(*vs == NULL)
			nn::gd::ShaderStage::CreateShader(mShaderBinary, vertexShaderIndex, vs);
		if(gs != NULL && *gs == NULL)
			nn::gd::ShaderStage::CreateShader(mShaderBinary, geometryShaderIndex, gs);

		//create the pipeline
		nn::gd::ShaderPipeline* ret;
		nn::Result result = nn::gd::ShaderStage::CreateShaderPipeline(*vs, gs?*gs:NULL, &ret, (nn::gd::CTR::ShaderPipelineUnmanagedRegisters*)unmanagedRegs);
		NN_UTIL_PANIC_IF_FAILED(result);
		return ret;
	}

	void Finalize()
	{
		//first release ShaderPipeline
		//then Shader
		//then ShaderBinary
		//but finalization is overrated
	}

	nn::gd::ShaderPipeline*  mCurrPipeline;

	void SetPipeline(nn::gd::ShaderPipeline* nextPipeline)
	{
		if(mCurrPipeline == nextPipeline) return;
		mCurrPipeline = nextPipeline;
		nn::gd::ShaderStage::SetShaderPipeline(mCurrPipeline);
	}
	
	//prints a declaration for a hardcoded uniform location (check it later with CheckUniform)
	void PrintUniformLocationDeclaration(const char* name)
	{
		#ifndef NN_SWITCH_DISABLE_DEBUG_PRINT
			nn::gd::UniformLocation temp = UniformLocation(name);
			u32 bleh = *(u32*)&temp;
			static const char* registerTypes[] = {
					"Undefined", //= 0
					"Boolean", //= 1
					"Integer3", //= 2
					"x3x","x4x","x5x","x6x","x7x",
					"Float", //= 8,
					"Float2", //= 9,
					"Float3", //= 10,
					"Float4", //= 11,
					"FloatMatrix2", //= 12,
					"FloatMatrix3", //= 13,
					"FloatMatrix4", //= 14,
					"x15x" //= 15
			};
			const char* shaderTypes[] = {
				"VERTEXSHADER", "GEOMETRYSHADER"
			};
			NN_LOG("nn::gd::UniformLocation XXX(%s, %d, %d, %d, nn::gd::UniformLocation::%s); //%s ( 0x%08X )\n",
				shaderTypes[temp.GetShaderType()],
				(bleh>>16)&0x1FF,
				temp.GetRegister(),
				temp.GetSize(),
				registerTypes[temp.GetUniformType()],
				name,
				bleh
				);
		#endif
	}

	//just a convenient wrapper for getting uniform locations
	nn::gd::UniformLocation UniformLocation(const char* name)
	{
		nn::gd::UniformLocation location = nn::gd::ShaderStage::GetShaderUniformLocation(mCurrPipeline, name);
		NN_ASSERT(location.IsValid());
		return location;
	}

	void SetFloat2(const nn::gd::UniformLocation loc, const float x, const float y)
	{
		float buffer[] = {x,y};
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, buffer);
	}

	void SetFloat3(const nn::gd::UniformLocation loc, const float x, const float y, const float z)
	{
		float buffer[] = {x,y,z};
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, buffer);
	}

	void SetMatrix(const nn::gd::UniformLocation loc, const nn::math::Matrix34& mtx)
	{
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, (f32*)&mtx);
	}

	void SetMatrix(const nn::gd::UniformLocation loc, const nn::math::Matrix44& mtx)
	{
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, (f32*)&mtx);
	}

	void SetBoolean(const nn::gd::UniformLocation loc, bool value)
	{
		nn::gd::ShaderStage::SetShaderPipelineConstantBoolean(mCurrPipeline,loc,value?1:0,1);
	}

	void SetFloat12(const nn::gd::UniformLocation loc, float x, float y, float z, float w, float x1, float y1, float z1, float w1, float x2, float y2, float z2, float w2)
	{
		const float vals[] = {x,y,z,w,x1,y1,z1,w1,x2,y2,z2,w2};
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, (f32*)vals);
	}

	void SetFloat8(const nn::gd::UniformLocation loc, float x, float y, float z, float w, float x1, float y1, float z1, float w1)
	{
		const float vals[] = {x,y,z,w,x1,y1,z1,w1};
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, (f32*)vals);
	}

	void SetFloat4(const nn::gd::UniformLocation loc, float x, float y, float z, float w)
	{
		const float vals[] = {x,y,z,w};
		SetFloat4(loc,vals);
	}

	void SetFloat4(const nn::gd::UniformLocation loc, const float* vals)
	{
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, (f32*)vals);
	}

	void SetFloat(const nn::gd::UniformLocation loc, const float val)
	{
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, (f32*)&val);
	}


	void SetFloatv(const nn::gd::UniformLocation loc, const float* vals)
	{
		nn::gd::ShaderStage::SetShaderPipelineConstantFloat(mCurrPipeline, loc, (f32*)vals);
	}
};

void app_Exit();
void app_Duties();

//graphics utilities
void gdBindVB(int slot,nn::gd::VertexBufferResource* vb, int vertexOffset);
u32* ZigZag32(int Width, int Height, void* Pixels);
u8* ZigZag8(int Width, int Height, void* Pixels);
extern CommandList g_CommandList;
void EndFrame();

bool IsEshopAvailable();
void JumpToEShopTitlePage(u32 uniqueId);

int GetDiscreteUserStereoLevel(int levels);

void ErrEula_DisplayErrorCode(s32 _nErrorCode, bool _bCheckNetSettings);
void ErrEula_DisplayErrorText(const wchar_t* _errorStr);
void ErrEula_DisplayEulaAgreement();
