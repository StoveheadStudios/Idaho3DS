#include "SaveManager.h"

#include <nn/fs/fs_FileInputStream.h>
#include <nn/fs/fs_FileOutputStream.h>
#include <nn/fs/fs_Directory.h>
#include <nn/fs/fs_Result.h>
#include <nn/fs/fs_FileSystem.h>
#include <nn/fs/fs_FileSystemBase.h>
#include <nn/Result.h>

#include "engine.h"
#include <wchar.h>

#define g_SavePath L"data:/"
#define g_SavePathAscii "data:/"

#define NFILESMAX 512// ensure that we do not have more files than this

#define BUFFERSIZE (nn::fs::MAX_FILE_PATH_LENGTH+1+1)


enum eSaveError {
	eSaveError_ReadFailed,
	eSaveError_VerifyFailed,
	eNumSaveErrors
};

static const wchar_t* error_strings_EN[eNumSaveErrors] = {
	L"Could not read save data. Turn off the\npower and try again.",
	L"The save data has been corrupted and\nwill be deleted. Press OK to continue."
};

//TODO: if you support multiple languages, you may need to localize these
//(strangely, I can't figure out how we've done that in the past)
void DisplaySaveError(eSaveError _nError)
{
	ErrEula_DisplayErrorText(error_strings_EN[_nError]);
}

static void generateBackupAccessError()
{
	DisplaySaveError(eSaveError_ReadFailed);
	
	//may 2015 - it seems this doesnt work anymore? is it because the user/os hasnt requested an exit?
	//we get asserts from an applet function.
	//app_Exit();

	//lets go into an infinite loop instead:
	//(yeah, believe it or not, more than one company's lotchecks say this kind of thing is OK in cases of fatal errors)
	for(;;)
	{
		app_Duties();
		DisplaySaveError(eSaveError_VerifyFailed);
	}
}

static void handleFileAccessError(nn::Result _result)
{
	if ( _result <= nn::fs::ResultMediaAccessError() 
		|| _result <= nn::fs::ResultMediaNotFound() 
		|| _result <= nn::fs::ResultOperationDenied() )
	{
		generateBackupAccessError();
	}
	else
	{
		// Unexpected error 
		NN_ERR_THROW_FATAL_ALL( _result );
	}
}

static bool VerifyFile(const wchar_t* _name, bool bTryRead, nn::Result* pResultError)
{
	bool ret = false;
	nn::fs::FileInputStream fis;
	nn::Result result = fis.TryInitialize(_name);
	if (result.IsSuccess()) {
		s64 nBytesNeeded = 0;
		result = fis.TryGetSize(&nBytesNeeded);
		if (result.IsSuccess()) {
			if (!bTryRead)
				ret=true;
			else {
				u8* pBuffer = new u8[nBytesNeeded];
				if (pBuffer) {
					s32 bytesRead=0;
					result = fis.TryRead(&bytesRead, pBuffer, nBytesNeeded);
					if (result.IsSuccess() && bytesRead>=nBytesNeeded) {
						ret = true;
					}
					delete[] pBuffer;
				}
			}
		}
		fis.Finalize();
	}
	
	if (pResultError && !ret)
		*pResultError = result;
	return ret;
}

//this is public because it's useful for AOC (which is way too complex to demo here... way... way... too complex)
bool VerifyDir(const wchar_t* _name, bool bTryRead, nn::Result* pResultError)
{
	bool ret = false;
	nn::fs::Directory dir;
	nn::Result result = dir.TryInitialize(_name);
	if (result.IsSuccess()) {
		static nn::fs::DirectoryEntry entries[NFILESMAX];
		s32 nEntries = NFILESMAX;
		result = dir.TryRead(&nEntries, entries, nEntries);
		if (result.IsSuccess()) {
			ret = true;
			for (s32 i=0; i<nEntries; i++) {
				bool (*fp)(const wchar_t*, bool, nn::Result*) = entries[i].attributes.isDirectory? VerifyDir : VerifyFile;
				wchar_t next[BUFFERSIZE]; 
				swprintf(next,BUFFERSIZE,L"%ls/%ls",_name, entries[i].entryName);
				if (!fp(next, bTryRead, &result)) {
					ret = false; 
					break;
				}
			}
		}
		dir.Finalize();
	}
	if (pResultError && !ret)
		*pResultError = result;
	return ret;
}

static nn::Result TryFormat(int maxFiles=1, int maxDirectories=0)
{
	return nn::fs::FormatSaveData(maxFiles,maxDirectories,true);
}

static bool VerifyFormat(int maxFiles=1, int maxDirectories=0)
{
	size_t fmtMaxFiles, fmtMaxDirectories;
	bool fmtIsDuplicateAll;
	nn::Result result = nn::fs::GetSaveDataFormatInfo(&fmtMaxFiles, &fmtMaxDirectories, &fmtIsDuplicateAll);
	return result.IsSuccess() && fmtMaxFiles==maxFiles && fmtMaxDirectories==maxDirectories && fmtIsDuplicateAll==true && VerifyDir(g_SavePath, true, NULL);
}

SaveManager::SaveManager(int maxFiles)
{
	nn::Result result = nn::fs::MountSaveData(g_SavePathAscii);
	
	NN_ASSERT(maxFiles<NFILESMAX);

	if (result.IsSuccess() && !VerifyFormat(maxFiles)) {
		nn::fs::Unmount(g_SavePathAscii);
		result = TryFormat(maxFiles);
	}
	else if (!result.IsFailure())
		return;
	else if (result <= nn::fs::ResultNotFormatted())
	{
		NN_LOG("Disk not formatted.\n");
		result = TryFormat(maxFiles);
	}
	else if (result <= nn::fs::ResultBadFormat())
	{
		NN_LOG("Disk bad format.\n");
		DisplaySaveError(eSaveError_VerifyFailed);
		result = TryFormat(maxFiles);
	}
	else if (result <= nn::fs::ResultVerificationFailed())
	{
		NN_LOG("Disk verification failed.\n");
		DisplaySaveError(eSaveError_VerifyFailed);
		result = TryFormat(maxFiles);
	}
	else if(result <= nn::fs::ResultOperationDenied())
	{
		// As of CTR-SDK 2.1, this error is handled by the system. Treat it as an unexpected error.
		NN_LOG("Denied access to backup data.\n");

		generateBackupAccessError();
		return;
	}
	else if ( result <= nn::fs::ResultNotFound() )
	{
		NN_LOG("Unable to access backup data.\n");

		generateBackupAccessError();
		return;
	}
	else
	{
		NN_LOG("Unhandled mounting error.\n");
		NN_ERR_THROW_FATAL_ALL(result);
	}

	if(result.IsFailure())
	{
		// Here, the act of formatting save data never fails.
		// NOTE: When developing your application, confirm that the maxFiles and maxDirectories arguments are not too big.
		//       Also confirm that the content of the RSF file is correct.
		NN_ERR_THROW_FATAL_ALL(
			result);
	}
	else
	{
		// Remount
		result = nn::fs::MountSaveData(g_SavePathAscii);
		if(result.IsFailure())
		{
			// The same as when making the first call.
			// (Supplement) A mount operation never fails immediately after data has been successfully formatted.
			NN_ERR_THROW_FATAL_ALL(result);
		}
	}
}

SaveManager::~SaveManager()
{
	nn::Result result = nn::fs::Unmount(g_SavePathAscii);
	NN_ASSERT(result.IsSuccess());
}

bool SaveManager::Commit()
{
	nn::Result result = nn::fs::CommitSaveData( g_SavePathAscii );
	if (!result.IsSuccess()) {
		handleFileAccessError(result);
		return false;
	}
	return true;
}

bool SaveManager::Save(const char* _filename, const void* _buffer, u32 _size)
{
	bool bResult = false;
	nn::fs::FileOutputStream fos; 

	wchar_t path[BUFFERSIZE]; 
	swprintf(path,BUFFERSIZE,L"%ls%s",g_SavePath, _filename);
	nn::Result result = fos.TryInitialize(path, true); 
	
	if (!result.IsSuccess()) {
		NN_LOG("Failed to initialize the file stream.\n");
		handleFileAccessError( result );
	}
	else {
		fos.SetSize(0);

		s32 bytesWritten;
		result = fos.TryWrite(&bytesWritten, _buffer, _size, true); 

		if (result.IsSuccess()) {
			bResult = true;
		}
		else {
			NN_LOG("Failed to write to the file stream.\n");
			handleFileAccessError(result);
		}

		fos.Finalize();
	}

	return bResult;
}

bool SaveManager::Load(const char* _filename, void* _buffer, u32 _size)
{
	bool bResult = false;
	nn::fs::FileInputStream fis; 

	wchar_t path[BUFFERSIZE]; 
	swprintf(path,BUFFERSIZE,L"%ls%s",g_SavePath, _filename);
	nn::Result result = fis.TryInitialize(path);

	if (!result.IsSuccess()) {
		if (result <= nn::fs::ResultNotFound()) {	// file doesn't exist
			return false;
		}
		else {
			NN_LOG("Failed to initialize the file stream.\n");
			handleFileAccessError( result );
		}
	}
	else {
		s64 fileSize = fis.GetSize(); 
		if (fileSize != _size) {
			NN_LOG("File size is not as expected. Not loading up the file.\n");
		}
		else {
			s32 bytesRead;
			result = fis.TryRead(&bytesRead, _buffer, fileSize);

			if (result.IsSuccess()) {
				bResult = true;
			}
			else {
				NN_LOG("Failed to read from the file stream.\n");
				handleFileAccessError(result);
			}
		}
		fis.Finalize();
	}
	return bResult;	
}

bool SaveManager::Delete(const char* _filename)
{
	wchar_t path[BUFFERSIZE]; 
	swprintf(path,BUFFERSIZE,L"%ls%s",g_SavePath, _filename);
	nn::Result result = nn::fs::TryDeleteFile(path);

	if (!result.IsSuccess()) {
		if (result <= nn::fs::ResultNotFound()) {
			NN_LOG("Unable to delete file. It does not exist.\n");
			return false;
		}
		else {
			NN_LOG("Failed to delete the file.\n");
			handleFileAccessError(result);
			return false;
		}
	}

	return true;
}


