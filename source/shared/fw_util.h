// fw_util.h - anchor

#ifndef _FW_UTIL_H
#define _FW_UTIL_H

#include "fw_base.h"

#if defined PLATFORM_WINDOWS || defined __linux__
# define _LARGE_FILE_SUPPORT
#endif

#ifdef PLATFORM_AMIGA
# include <dos/dos.h>
# include <proto/dos.h>
# include <clib/debug_protos.h>
# ifdef PLATFORM_AROS
#  define DEBUG 1
#  include <aros/config.h>
#  include <dos/dos.h>
#  include <exec/memory.h>
#  include <workbench/startup.h>
#  include <proto/exec.h>
#  include <proto/dos.h>
#  include <aros/asmcall.h>
#  include <aros/debug.h>
#  include <aros/symbolsets.h>
#  include <aros/startup.h>
# endif
#endif

#if defined _LARGE_FILE_SUPPORT || defined PLATFORM_AMIGAOS3
typedef int fw_filehandle;
#else
typedef FILE* fw_filehandle;
#endif

enum fwFileAccessMode
{
	famAppend,
	famNewFile,
	famReadOnly,
	famCOUNT
};

enum fwSeekMode
{
	smStart,
	smEnd,
	smCurrent,
	smCOUNT
};

//
// crypter
//

class cCrypto
{
public:
	enum eCryptTask
	{
		ctEncrypt,
		ctDecrypt,
		ctCOUNT
	};
	cCrypto(const stringc& key, eCryptTask task);
	void cryptBytes(u8* buf, u32 size);
	stringc _key;
	int _counter;
	u8 _delta;
	eCryptTask _task;
};

//
// file i/o
//

fw_filehandle __openFile(const char* _path, fwFileAccessMode _fam);
int __closeFile(fw_filehandle _fh);
long long __seekFile(fw_filehandle _fh, long long _ofs, fwSeekMode _sm);
int __readFile(fw_filehandle _fh, void* _buf, int _len);
int __writeFile(fw_filehandle _fh, void* _buf, int _len);
int __flushFile(fw_filehandle _fh);

long long __fileSize(const char* pFile);
bool __isFileExist(const char* pFile);
bool __loadFile(const char* pFile, char*& pbuf, int& psize, bool ignorePak=false);

//
// file writer class
//

class cFileWriter
{
public:
	cFileWriter(const char* name, bool append=false);
	~cFileWriter();
	void setEncryptKey(const stringc& key);
	bool isValid();
	long long seek(long long pos, fwSeekMode origin);
	//long long tell();
	void flush();
	int saveRaw(void* buf, u32 count);
	void saveInt(int val);
	void saveInt64(const long long val);
	void saveBool(bool val) { saveInt(val?1:0); }
	void saveVec3(const vector3df& vec);
	void saveFloat(float data);
	void saveStr(const char* str);
	void saveStr(const stringw& str);
	void saveStr(const stringc& str);

protected:
	fw_filehandle file;
	cCrypto* crypter;
	char temp[1024];
};

//
// memory file reader class
//

class cMemoryFileReader : public io::IFileReadCallBack
{
public:
	cMemoryFileReader(const char* filename);
	cMemoryFileReader(char* buf, int len, bool detached=false); // deatch means: the reader wont free the databuffer
	~cMemoryFileReader();
	void seek(int pos);
	int tell();
	int size();
	char* ptr();
	bool isValid();
	void setDecryptKey(const stringc& key);
	void decryptFullData(const stringc& key);
	void loadStr(char* pstr);
	void loadStr(stringc& pstr);
	void loadStr(stringw& pstr);
	int loadInt();
	long long loadInt64();
	bool loadBool() { return (loadInt()?true:false); }
	float loadFloat();
	void loadVec3(vector3df& out);
	void loadRaw(void* buf, int count);
	void setVersion(int ver);
	int getVersion();
	void loadVersion();
	// io::IFileReadCallBack class implementation
	virtual int read(void* buffer, int sizeToRead) { loadRaw(buffer,sizeToRead); return sizeToRead; }
	virtual int getSize() { return size(); }
protected:
	void __read(void* out, int len);
	bool        _valid;
	char*       _ptr;
	int         _len;
	int         _seekPos;
	cCrypto*    _crypter;
	int         _version;
	bool        _detached;
};

//
// xml writer class
//

class cXMLWriter
{
public:
	cXMLWriter(const char* file);
	~cXMLWriter();

	void saveInt(const char* name1, const char* name2, int value, bool parent=true);
	void saveFloat(const char* name1, const char* name2, float value, bool parent=true);
	void saveVec3(const char* name1, const vector3df& value, bool parent=true);
	void saveString(const char* name1, const char* name2, const char* value, bool parent=true);
	void saveRect(const char* name1, int vx, int vy, int vw, int vh, bool parent=true);
	void closeTag();
	bool isValid() { return (_f!=0); }

protected:
	array<stringc> _name;
	stringc        _tab;
	fw_filehandle  _f;
};

//
// string handlers, parsers
//

int __atoi(const char* str);
int __atoi(const stringc& str);
long long __atoi64(const char* str);
void __skipLine(char*& ptr);
void __extractAsString(char*& ptr, stringc& out, const char separator);
void __extractAsFloat(char*& ptr, float& out, const char separator);
void __extractAsInt(char*& ptr, int& out, const char separator);
void __extractAsInt(wchar_t*& ptr, int& out, const wchar_t separator);
bool __isNumber(const char c);
void __wsFormat(wchar_t* out, const wchar_t* fmt, ...);
void __utf8ToWideChar(const stringc& utf8, stringw& out);
void __wideCharToUTF8(const stringw& pIn, stringc& pUTF8);
void __escapeToWideChar(const stringc& pEscaped, stringw& out);
void __wideCharToEscape(const stringw& pWide, stringc& out);
float __fRandFromTo(float pFrom, float pTo);

//
// debug
//

#ifndef _NOLOG
void  __log(const char* format, ...);
void  __logClose();
extern bool gConsoleLogEnabled;
extern bool gFileLogEnabled;
extern bool gSerialLogEnabled;
#else
#define __log(args...)
#define __logClose()
#endif

#endif // _FW_UTIL_H
