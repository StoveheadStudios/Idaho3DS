#pragma once

#include <nn/math/math_Matrix44.h>
#include <nn/math/math_Matrix43.h>
#include <nn/math/math_Vector2.h>
#include <nn/math/math_Vector3.h>
#include <nn/math/math_Vector4.h>

class MatrixStackBase
{
public:

	void setDirty() { isDirty = true; }
	void clearDirty() { isDirty = false; }
	bool isDirty;


	void pop()
	{
		cursor--;
	}

	void reset()
	{
		isDirty = true;
		cursor = -1;
	}

	int cursor;

	//pushes garbage (actually just increments the cursor)
	void pushGarbage()
	{
		cursor++;
	}
};

template<typename MAT_TYPE, int ENTRIES>
class MatrixStack : public MatrixStackBase
{
	static const int BYTESIZE = sizeof(MAT_TYPE)*ENTRIES;

public:

	MAT_TYPE buf[ENTRIES];

	MAT_TYPE& top()
	{
		NN_ASSERT(cursor>=0);
		return buf[cursor];
	}

	//resets the modelstack and leaves an identity matrix pushed
	void resetIdentity()
	{
		reset();
		static const MAT_TYPE identity(MAT_TYPE::Identity());
		push(identity);
	}

	void push(const MAT_TYPE& mat)
	{
		NN_ASSERT(cursor<ENTRIES-1);
		buf[cursor+1] = mat;
		increment();
	}

	void push() { push(top()); }

	void setDirty() { MatrixStackBase::setDirty(); }

	void pop()
	{
		cursor--;
		isDirty = true;
	}

	void reset(const MAT_TYPE& mat)
	{
		reset();
		push(mat);
	}

	void reset()
	{
		isDirty = true;
		cursor = -1;
	}

protected:
	void increment()
	{
		cursor++;
		if(cursor==ENTRIES) cursor=ENTRIES-1;
	}
};

template<int ENTRIES>
class MatrixStack34 : public MatrixStack<nn::math::Matrix34,ENTRIES>
{
public:
	nn::math::Matrix34& top()
	{
		return BaseType::top();
	}

	typedef MatrixStack<nn::math::Matrix34,ENTRIES> BaseType;
	
	void setDirty() { BaseType::setDirty(); }

	void Identity()
	{
		nn::math::MTX34Identity(&top());
	}

	void Translate(float x, float y)
	{
		nn::math::MTX34MultTranslate(&top(),top(),nn::math::Vector3(x,y,0));
		setDirty();
	}

	void Translate(float x, float y, float z)
	{
		nn::math::MTX34MultTranslate(&top(),top(),nn::math::Vector3(x,y,z));
		setDirty();
	}

	void Scale(nn::math::Vector2 s) { Scale(nn::math::Vector3(s.x,s.y,1)); }
	void Scale(float x, float y) { Scale(nn::math::Vector3(x,y,1)); }
	void Scale(float s) { Scale(nn::math::Vector3(s,s,s)); }
	void Scale(nn::math::Vector3 s)
	{
		nn::math::MTX34MultScale(&top(),top(),s);
		setDirty();
	}

	//rotates Z by given degrees
	void RotateD(float deg)
	{
		float temp[] = {
			0, 0, 0, 0,
			-0,0, 0, 0,
			0, 0, 1, 0
		};

		//	c, s, 0, 0,
		//	-s,c, 0, 0,
		//	0, 0, 1, 0,
		//	0, 0, 0, 1
	
		//set top row
		nn::math::SinCosDeg(&temp[1],&temp[0],-deg);
		//set next row
		temp[5] = temp[0];
		temp[4] = -temp[1];

		nn::math::MTX34Mult(&top(),&top(),(nn::math::Matrix34*)temp);
		setDirty();
	}

	//rotates Z by given rads
	void RotateR(float rads)
	{
		float temp[] = {
			0, 0, 0, 0,
			-0,0, 0, 0,
			0, 0, 1, 0
		};

		//	c, s, 0, 0,
		//	-s,c, 0, 0,
		//	0, 0, 1, 0,
		//	0, 0, 0, 1
	
		//set top row
		nn::math::SinCosRad(&temp[1],&temp[0],-rads);
		//set next row
		temp[5] = temp[0];
		temp[4] = -temp[1];

		nn::math::MTX34Mult(&top(),&top(),(nn::math::Matrix34*)temp);
		setDirty();
	}

	//rotates Z by given U16
	void RotateI(u16 angle)
	{
		float temp[] = {
			0, 0, 0, 0,
			-0,0, 0, 0,
			0, 0, 1, 0
		};

		//	c, s, 0, 0,
		//	-s,c, 0, 0,
		//	0, 0, 1, 0,
		//	0, 0, 0, 1
	
		//set top row
		nn::math::SinCosIdx(&temp[1],&temp[0],-angle);
		//set next row
		temp[5] = temp[0];
		temp[4] = -temp[1];

		nn::math::MTX34Mult(&top(),&top(),(nn::math::Matrix34*)temp);
		setDirty();
	}

	void RotateEulerD(float x, float y, float z, float deg)
	{
		nn::math::Matrix34 temp;
		nn::math::MTX34RotAxisDeg(&temp,nn::math::Vector3(x,y,z),deg);
		nn::math::MTX34Mult(&top(),&top(),&temp);
		setDirty();
	}

	void RotateEulerR(float x, float y, float z, float rads)
	{
		nn::math::Matrix34 temp;
		nn::math::MTX34RotAxisRad(&temp,nn::math::Vector3(x,y,z),rads);
		nn::math::MTX34Mult(&top(),&top(),&temp);
		setDirty();
	}

	void RotateEulerI(float x, float y, float z, u16 idx)
	{
		nn::math::Matrix34 temp;
		nn::math::MTX34RotAxisRad(&temp,nn::math::Vector3(x,y,z),NN_MATH_FIDX_TO_RAD(idx/256.0f));
		nn::math::MTX34Mult(&top(),&top(),&temp);
		setDirty();
	}

};