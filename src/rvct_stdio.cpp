/*---------------------------------------------------------------------------*
  Project:  Horizon
  File:     rvct_stdio.cpp

  Copyright (C)2009 Nintendo Co., Ltd.  All rights reserved.

  These coded instructions, statements, and computer programs contain
  proprietary information of Nintendo of America Inc. and/or Nintendo
  Company Ltd., and are protected by Federal copyright law.  They may
  not be disclosed to third parties or copied or duplicated in any form,
  in whole or in part, without the prior written consent of Nintendo.

  $Rev: 12372 $
 *---------------------------------------------------------------------------*/

//modified to make stdio work

#include <nn.h>
#include <nn/types.h>
#include <nn/config.h>

#include <rt_sys.h>
#include <rt_misc.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifndef NN_DEBUGGER_ARM_REALVIEW

#pragma import(__use_no_semihosting_swi)
#pragma import(__use_c99_matherr)

class MyFileStream : public nn::fs::FileStream
{
public:
	MyFileStream()
		: opened(false)
	{}
	bool opened;
};

static void strtolower(char* str)
{
	char* cp = str;
	while(*cp)
		*cp++ = tolower(*cp);
}

static MyFileStream s_fileStreams[_SYS_OPEN];

extern "C"
{
    // rt_sys.h
    const char __stdin_name[]  = "";
    const char __stdout_name[] = "";
    const char __stderr_name[] = "";

    FILEHANDLE _sys_open(const char * name, int openmode)
	{
		for(int i=0;i<_SYS_OPEN;i++)
		{
			if(!s_fileStreams[i].opened)
			{
				static char mypath[256];
				strcpy(mypath,name);
				strtolower(mypath);

				MyFileStream &fs = s_fileStreams[i];
				int om = nn::fs::OPEN_MODE_READ;
				if(openmode&OPEN_W) om |= nn::fs::OPEN_MODE_WRITE;
				nn::Result r = fs.TryInitialize(name,om);

				//formerly we re-tried with rom:/compiled/ prefixed but that's being done elsewhere now. eventually we'll phase this module out entirely
				//WE MIGHT NEED TO LOWER CASE IT, IF THAT ISNT HANDLED ELSEWHERE
				if(r.IsFailure())
					return -1;

				fs.opened = true;
				return (int)i;
			}
		}
		return -1;
	}
    int     _sys_close(FILEHANDLE fh)
	{
		int n = (int)fh;
		MyFileStream &fs = s_fileStreams[n];
		if(!fs.opened) return -1;
		fs.Finalize();
		fs.opened = 0;
		return 0;
	}

    int     _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode) { NN_UNUSED_VAR(fh); NN_UNUSED_VAR(buf); NN_UNUSED_VAR(len); NN_UNUSED_VAR(mode); return -1; }
    int     _sys_read(FILEHANDLE fh, unsigned char * buf, unsigned len, int mode)
	{
		int n = (int)fh;
		MyFileStream &fs = s_fileStreams[n];
		if(!fs.opened) return -1;

		s32 readed;
		nn::Result r = fs.TryRead(&readed,buf,len);
		if(r.IsFailure())
			return -1;
		int retval = len-readed;
		if(readed==0) retval |= 0x80000000;
		return retval;
	}
	
    void    _ttywrch(int ch)                    { NN_UNUSED_VAR(ch); }
    int     _sys_istty(FILEHANDLE fh)           
	{
		return 0;
	}
    int     _sys_seek(FILEHANDLE fh, long pos)
	{
		int n = (int)fh;
		MyFileStream &fs = s_fileStreams[n];
		if(!fs.opened) return -1;

		nn::Result r = fs.TrySetPosition(pos);
		if(r.IsFailure())
			return -1;
		return 0;
	}
    int     _sys_ensure(FILEHANDLE fh)          { NN_UNUSED_VAR(fh); NN_LOG("_sys_ensure\n"); return -1; }
    long    _sys_flen(FILEHANDLE fh)
		{ 
			int n = (int)fh;
			MyFileStream &fs = s_fileStreams[n];
			if(!fs.opened) return -1;
			return fs.GetSize();
		}
    int     _sys_tmpnam(char *name, int fileno, unsigned maxlength) { NN_UNUSED_VAR(name); NN_UNUSED_VAR(fileno); NN_UNUSED_VAR(maxlength); return -1; }
    void    _sys_exit(int return_code)          { NN_UNUSED_VAR(return_code); }
    char*   _sys_command_string (char* cmd, int len) { NN_UNUSED_VAR(cmd); NN_UNUSED_VAR(len); return NULL; }

    // rt_misc.h
//    void _getenv_init(void) {}
//    void _clock_init(void) {}
    int __raise(int signal, int type) { NN_UNUSED_VAR(signal); NN_UNUSED_VAR(type); return 0; }
    void __rt_raise(int sig, int type) { NN_UNUSED_VAR(sig); NN_UNUSED_VAR(type); }

    // 標準関数
//    clock_t clock(void) { return 0; }
//    time_t time(time_t* timer){ NN_UNUSED_VAR(timer); return 0; }
//    char* getenv(const char* name) { NN_UNUSED_VAR(name); return NULL; }
//    int remove(const char* filename) { NN_UNUSED_VAR(filename); return -1; }
//    int rename(const char* old, const char* newname) { NN_UNUSED_VAR(old); NN_UNUSED_VAR(newname); return -1; }
//    int system(const char* string) { NN_UNUSED_VAR(string); return -1; }

    // その他
    void __aeabi_atexit()       {}
    void __cxa_finalize()       {}
    void __rt_SIGTMEM()         {}
    void __rt_div0()            {}
}

#else

extern "C"
{
    void __aeabi_atexit()       {}
}

#endif
