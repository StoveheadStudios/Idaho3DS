#pragma once

#pragma pack(push, 4)
template<class T, u32 N>
class LazySizer
{
public:
	LazySizer() : mConstructed(false) {}

	inline bool isConstructed() const { return mConstructed; }

	inline T* get()
	{
		return (T*)mBuf;
	}

	inline const T* get() const
	{
		return (const T*)mBuf;
	}

	inline T& operator*()
	{
		return *(T*)mBuf;
	}

	inline const T& operator*() const
	{
		return *(const T*)mBuf;
	}

	inline T* operator->()
	{
		return (T*)mBuf;
	}

	inline const T* operator->() const
	{
		return (const T*)mBuf;
	}

	void construct()
	{
		new(get()) T();
		mConstructed = true;
	}

	template<typename A> void construct(const A a)
	{
		destruct();
		new(get()) T(a);
		mConstructed = true;
		#ifdef NN_DEBUG
		count++;
		#endif
	}

	void destruct()
	{
		if(!mConstructed) return;
		get()->~T();
		mConstructed = false;
	}
	

private:
	union
	{
		//this is to make sure we dont ruin the alignment of the interior type
		__attribute__ ((aligned (32)))
		u8 mBuf[N];
	};
	#ifdef NN_DEBUG
	u64 count;
	#endif
	bool mConstructed;
	u8 pad[3];
};
#pragma pack(pop)

// Use this to make a raw buffer that's large enough to hold a T instance.
// Like creating a u8 array and casting it, but LazySizer handles all the casts
// and it's strongly typed.
template<class T>
class Lazy : public LazySizer<T,sizeof(T)>
{
};
