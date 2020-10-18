#pragma once

#include <nn/Types.h>
#include <nn/Result.h>

//Sorry this is divided into two parts. That's just the way it evolved.
//This is the file you should use.
//The verification stuff.... it isn't necessary... and it's kind of messy...
//And I had to change it around a bit. If you smell trouble with it, just remove the call to VerifyFormat
//It may also make your game take longer to boot up... but it may save you some other kinds of trouble.
//So I decided to leave it.

class SaveManager
{
public:
	SaveManager(int maxFiles=1);
	~SaveManager();

	bool Commit(); // optional step, to flush cached writes to disk
	bool Save(const char* _filename, const void* _buffer, u32 _size); // note:1 or more Saves need to be followed by Commit
	bool Load(const char* _filename, void* _buffer, u32 _size);
	bool Delete(const char* _filename);
};

bool VerifyDir(const wchar_t* _name, bool bTryRead, nn::Result* pResultError);
