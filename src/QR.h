#pragma once

#include <nn/math/math_Matrix44.h>

#include "engine.h"
#include "MatrixStack.h"


//once upon a time I got more FPS by inline-defining all this junk

class QR
{
public:
	void NewFrame();

private:
	nn::gd::ShaderPipeline* mShaderPipeline;
	nn::gd::InputLayout *mInputLayout;
	nn::gd::UniformLocation uProjection, uExtraTexcoords;
	bool mIsActive;

public:
	MatrixStack34<16> ms;

	void Initialize();

	bool IsActive() const { return mIsActive; }

	void Begin();
	void End();	

	void SetMasterBrightnessf(float value);
	void SetAddColor(u8 r, u8 g, u8 b, u8 a);
	void ClearAddColor();

	void Flush();


	__forceinline void SetModulateColorWhite() { SetModulateColor(255,255,255,255); }
	__forceinline void FillRectangle(int x, int y, int w, int h) { FillRectangle((float)x,(float)y,(float)w,(float)h); }
	void Begin(const nn::math::Matrix44& matProj)
	{
		Begin();
		SetProjectionMatrix(matProj);
	}

	__forceinline void SetModulateColor(u8 r, u8 g, u8 b, u8 a)
	{
		EmitCommand(0x02,
			r/255.0f,
			g/255.0f,
			b/255.0f,
			a/255.0f
			);
	}

	void SetStereoAdjust(float x)
	{
		//not a mistake. we want to actually adjust it in Y, since our framebuffers are rotated
		EmitCommand(0x03,
			x,
			0,
			0,
			0
			);
	}

	void FillRectangle(float x, float y, float w, float h)
	{
		if(NeedsFlush()) Flush();
		nn::gd::CombinerStage::SetTextureCombinerState(combiner_Untextured);
		EmitQuad(x,y,w,h);
	}

	void SetProjectionMatrix(const nn::math::Matrix44& matProj)
	{
		if(NeedsFlush()) Flush();
		shader.SetMatrix(uProjection, matProj);
	}

	void PredrawPalettedTexture()
	{
		if(NeedsFlush()) Flush();
		nn::gd::CombinerStage::SetTextureCombinerState(combiner_Paletted);
	}

	void PredrawTexture()
	{
		if(NeedsFlush()) Flush();
		nn::gd::CombinerStage::SetTextureCombinerState(combiner_Textured);
	}

	void DrawSprite(TextureHandle *th, int x, int y, int sx, int sy, int sw, int sh)
	{
		PredrawTexture();
		nn::gd::TextureStage::SetTexture(nn::gd::TextureStage::TEXTURE_UNIT_0, *th->Texture());
		float umin = sx / (float)th->mWidth;
		float umax = (sx + sw) / (float)th->mWidth;
		float vmin = sy / (float)th->mHeight;
		float vmax = (sy + sh) / (float)th->mHeight;
		EmitRectangle<false>((float)x,(float)y,(float)sw,(float)sh,umin,umax,vmin,vmax,false,false);
	}

	template<bool FLIPPABLE>
	void EmitRectangle(float x, float y, float width, float height, float umin, float umax, float vmin, float vmax, bool xflip, bool yflip)
	{
		float u0,u1;
		if(FLIPPABLE && xflip) u0 = umax, u1 = umin;
		else u0 = umin, u1 = umax;

		float v0,v1;
		if(FLIPPABLE && yflip) v0 = vmax, v1 = vmin;
		else v0 = vmin, v1 = vmax;

		EmitTexParam(u0,v0,u1,v1);
		EmitQuad(x,y,width,height);
	}

private:

	void CreateCombiners();
	nn::gd::CombinerState* combiner_Textured;
	nn::gd::CombinerState* combiner_Paletted;
	nn::gd::CombinerState* combiner_Untextured;

	//buffer management
	nn::gd::VertexBufferResource* vb[2];
	float* buffers[2];
	int mBufferFlip;
	bool bContainsDraw;
	float *mCurrPtr, *mCurrPtrStart, *mCurrPtrEnd, *mCurrPtrFlushed;
	bool NeedsFlush() const { return bContainsDraw; }

	void EmitCommand(float cmd, float a, float b, float c, float d)
	{
		//it would be really bad if this buffer overflowed, so bounds-check it here

		float *ptr = mCurrPtr;
		if(ptr >= mCurrPtrEnd) return;

		//do this first, while we still have a copy of mCurrPtr around (just a ~1 instruction optimization)
		mCurrPtr += 5;

		//this block can get implemented as a fstmias, in one instruction. cool!
		ptr[0] = cmd;
		ptr[1] = a;
		ptr[2] = b;
		ptr[3] = c;
		ptr[4] = d;
	}

	void EmitModelviewMatrix(const nn::math::Matrix34& mat)
	{
		const float* fp = (const float*)&mat;
		for(int i=0,j=0;i<3;i++,j+=4)
		{
			EmitCommand(0x04 + i, fp[j+0], fp[j+1], fp[j+2], fp[j+3]);
		}
	}

	void EmitQuad(float x, float y, float w, float h)
	{
		SyncRenderState();

		EmitCommand(0x00,x,y,w,h);

		bContainsDraw = true;
	}

	void EmitTexParam(float u0, float v0, float u1, float v1)
	{
		EmitCommand(0x01,u0,v0,u1,v1);
	}

	void SyncRenderState(bool force=false)
	{
		if(ms.isDirty || force)
		{
			EmitModelviewMatrix( ms.top() );
			ms.clearDirty();
		}
	}


	static const int SHADER_INDEX_NONE = -1;
	static const int SHADER_INDEX_QUADRENDER_GSHADER = 0;
	static const int SHADER_INDEX_QUADRENDER_VSHADER = 1;
	static const int SHADER_NUM = 2;

	Shader<SHADER_NUM> shader;

};

extern QR qr;
