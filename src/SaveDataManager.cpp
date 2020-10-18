#include "SaveDataManager.h"

#include <nn/fs/fs_FileInputStream.h>
#include <nn/fs/fs_FileOutputStream.h>
#include <nn/fs/fs_Result.h>
#include <nn/fs/fs_FileSystemBase.h>

#include "engine.h"

#define SAVE_DATA_ROOT "data:/"

void SaveDataManager::Initialize()
{
	nn::Result result = nn::fs::MountSaveData(SAVE_DATA_ROOT);

	if (!result.IsFailure() )
		return;

	if (result <= nn::fs::ResultNotFormatted())
	{
		NN_LOG("Disk not formatted.\n");
		result = nn::fs::FormatSaveData(1,0,true);
	}
	else if (result <= nn::fs::ResultBadFormat())
	{
		NN_LOG("Disk bad format.\n");
		ErrEula_DisplayErrorText(L"The save data has been corrupted and\nwill be deleted. Press OK to continue.");
		result = nn::fs::FormatSaveData(1,0,true);
	}
	else if (result <= nn::fs::ResultVerificationFailed())
	{
		NN_LOG("Disk verification failed.\n");
		ErrEula_DisplayErrorText(L"The save data has been corrupted and\nwill be deleted. Press OK to continue.");
		result = nn::fs::FormatSaveData(1,0,true);
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
		NN_ERR_THROW_FATAL_ALL(result);
	}
	else
	{
		// Remount
		result = nn::fs::MountSaveData(SAVE_DATA_ROOT);
		if(result.IsFailure())
		{
			// The same as when making the first call.
			// (Supplement) A mount operation never fails immediately after data has been successfully formatted.
			NN_ERR_THROW_FATAL_ALL(result);
		}
	}
}


bool SaveDataManager::Commit()
{      
	nn::Result result = nn::fs::CommitSaveData( SAVE_DATA_ROOT );
	if ( !result.IsSuccess() )
	{
		handleFileAccessError( result );
		return false;
	}
	return true;
}

void SaveDataManager::Finalize()
{
	nn::Result result = nn::fs::Unmount( SAVE_DATA_ROOT );
	NN_ASSERT( result.IsSuccess() );
}

bool SaveDataManager::SaveFile( const wchar_t* _filename, const void* _buffer, u32 _size )
{
	bool bResult = false;
	nn::fs::FileOutputStream fos; 
	nn::Result result = fos.TryInitialize(_filename, true); 
	fos.SetSize(0);

	if ( !result.IsSuccess() )
	{
		NN_LOG("Failed to initialize the file stream.\n");
		handleFileAccessError( result );
	}
	else
	{
		s32 bytesWritten;
		result = fos.TryWrite(&bytesWritten, _buffer, _size, true); 

		if ( result.IsSuccess() )
		{
			bResult = true;
		}
		else
		{
			NN_LOG( "Failed to write to the file stream.\n");
			handleFileAccessError( result );
		}

		fos.Finalize();
	}

	return bResult;
}

bool SaveDataManager::LoadFile( const wchar_t* _filename, void* _buffer, u32 _size )
{
	bool bResult = false;
	nn::fs::FileInputStream fis; 
	nn::Result result = fis.TryInitialize(_filename); 

	if ( !result.IsSuccess() )
	{
		if ( result <= nn::fs::ResultNotFound() )
		{
			// file doesn't exist
			return false;
		}
		else
		{
			NN_LOG("Failed to initialize the file stream.\n");
			handleFileAccessError( result );
		}
	}
	else
	{
		s64 fileSize = fis.GetSize(); 
		if (fileSize != _size)
		{
			NN_LOG("File size is not as expected. Not loading up the file.\n");
		}
		else
		{
			s32 bytesRead;
			result = fis.TryRead(&bytesRead, _buffer, fileSize);

			if ( result.IsSuccess() )
			{
				bResult = true;
			}
			else
			{
				NN_LOG( "Failed to read from the file stream.\n" );
				handleFileAccessError( result );
			}

		}
		fis.Finalize();
	}
	return bResult;
}

void SaveDataManager::handleFileAccessError( nn::Result _result )
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

void SaveDataManager::generateBackupAccessError()
{
	ErrEula_DisplayErrorText(L"Could not read save data. Turn off the\npower and try again.");
	app_Exit();
}
