#pragma once

#include <nn/types.h>
#include <nn/Result.h>

//Sorry this is divided into two parts. That's just the way it evolved.
//You shouldn't use this file, use SaveManager instead.

class SaveDataManager
{
public:
  static void Initialize( );
  static void Format( bool _bUnmount=false );
  static void Finalize();
  static bool Commit();
  static bool SaveFile( const wchar_t* _filename, const void* _buffer, u32 _size );
  static bool LoadFile( const wchar_t* _filename, void* _buffer, u32 _size );

private:
  static void generateBackupAccessError();
  static void handleFileAccessError( nn::Result _result );
};
