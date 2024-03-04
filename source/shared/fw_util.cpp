// fw_util.cpp - anchor

#include "fw_util.h"

//
// crypter
//

cCrypto::cCrypto(const stringc& key, eCryptTask task)
{
	_key = key;
	_counter = 0;
	_delta = _key.size();
	_task = task;
	for(u32 _i=0;_i<_key.size();_i++)
	{
		u8* c = (u8*)&(_key.c_str()[_i]);
		*c ^= (_i&1)?0xAA:0x55;
	}
}

void cCrypto::cryptBytes(u8* buf, u32 size)
{
	u8* c;
	u8 adder;
	for(u32 _i=0;_i<size;_i++)
	{
		c = (u8*)&(_key.c_str()[_counter]);
		if ( _task == ctEncrypt )
		{
			adder = *buf;
			*buf ^= _delta;
			_delta += adder;
		}
		*buf ^= *c;
		if ( _task == ctDecrypt )
		{
			*buf ^= _delta;
			_delta += *buf;
		}
		*c = (*c)+_counter;
		_counter++;
		buf++;
		if ( _counter == _key.size() )
		{
			_counter = 0;
		}
	}
}

//
// file i/o
//

fw_filehandle __openFile(const char* _path, fwFileAccessMode _fam) 
{
#ifdef _LARGE_FILE_SUPPORT
	int ret = 0;
# if defined PLATFORM_WINDOWS
	ret = _sopen(_path,_O_BINARY|(_fam==famReadOnly?0:_O_CREAT)|_O_RDWR,_SH_DENYNO,_S_IREAD|_S_IWRITE);
# else
	ret = _sopen(_path,(_fam==famReadOnly?0:O_CREAT)|O_RDWR,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
# endif
	return (ret > 0) ? ret : 0;
#elif defined PLATFORM_AMIGAOS3
	switch ( _fam )
	{
	case famAppend:
		return Open(_path,MODE_READWRITE);
	case famNewFile:
		return Open(_path,MODE_NEWFILE);
	case famReadOnly:
		return Open(_path,MODE_OLDFILE);
	}
#else
	switch ( _fam )
	{
	case famAppend:
		{
# ifndef PLATFORM_WINDOWS
			// this preparation for linux append
			fw_filehandle _f = fopen(_path,"rb");
			if ( !_f )
			{
				_f = fopen(_path,"wb");
				if ( _f )
				{
					fclose(_f);
				}
				else
				{
					return (fw_filehandle)0;
				}
			}
			else
			{
				fclose(_f);
			}
# endif
			return fopen(_path,"r+b");
		}
	case famNewFile:
		return fopen(_path,"wb");
	case famReadOnly:
		return fopen(_path,"rb");
	}
#endif
	return (fw_filehandle)0;
}

int __closeFile(fw_filehandle _fh)
{
#ifdef _LARGE_FILE_SUPPORT
	return _close(_fh);
#elif defined PLATFORM_AMIGAOS3
	return Close(_fh);
#else
	return fclose(_fh);
#endif
}

long long __seekFile(fw_filehandle _fh, long long _ofs, fwSeekMode _sm)
{
#ifdef _LARGE_FILE_SUPPORT
	switch ( _sm )
	{
	case smStart:
		return _lseeki64(_fh,_ofs,SEEK_SET);
	case smEnd:
		return _lseeki64(_fh,_ofs,SEEK_END);
	case smCurrent:
		return _lseeki64(_fh,_ofs,SEEK_CUR);
	}
#elif defined PLATFORM_AMIGAOS3
	switch ( _sm )
	{
	case smStart:
		return Seek(_fh,_ofs,OFFSET_BEGINNING);
	case smEnd:
		return Seek(_fh,_ofs,OFFSET_END);
	case smCurrent:
		return Seek(_fh,_ofs,OFFSET_CURRENT);
	}
#else
	switch ( _sm )
	{
	case smStart:
		return fseek(_fh,(int)_ofs,SEEK_SET);
	case smEnd:
		return fseek(_fh,(int)_ofs,SEEK_END);
	case smCurrent:
		return fseek(_fh,(int)_ofs,SEEK_CUR);
	}
#endif
	return 0;
}

int __readFile(fw_filehandle _fh, void* _buf, int _len)
{
#ifdef _LARGE_FILE_SUPPORT
	return _read(_fh,_buf,_len);
#elif defined PLATFORM_AMIGAOS3
	return Read(_fh,_buf,_len);
#else
	return (int) fread(_buf,1,_len,_fh);
#endif
}

int __writeFile(fw_filehandle _fh, void* _buf, int _len)
{
#ifdef _LARGE_FILE_SUPPORT
	return _write(_fh,_buf,_len);
#elif defined PLATFORM_AMIGAOS3
	return Write(_fh,_buf,_len);
#else
	return (int) fwrite(_buf,1,_len,_fh);
#endif
}

int __flushFile(fw_filehandle _fh)
{
#ifdef _LARGE_FILE_SUPPORT
	return _commit(_fh);
#elif defined PLATFORM_AMIGAOS3
	return Flush(_fh);
#else
	return fflush(_fh);
#endif
}

long long __fileSize(const char* pFile)
{
	long long fsize = 0;
	fw_filehandle f = __openFile(pFile,famReadOnly);
	if ( f )
	{
		__seekFile(f,0,smEnd);
#ifdef _LARGE_FILE_SUPPORT
		fsize = _tell(f);
#elif defined PLATFORM_AMIGAOS3
		fsize = __seekFile(f,0,smStart);
#else
		fsize = ftell(f);
#endif
		__closeFile(f);
	}
	return fsize;
}

bool __isFileExist(const char* pFile)
{
	fw_filehandle f = __openFile(pFile,famReadOnly);
	if ( f )
	{
		__closeFile(f);
		return true;
	}
	return false;
}

bool __loadFile(const char* pFile, char*& pbuf, int& psize, bool ignorePak)
{
	/*
	if ( !ignorePak )
	{
		if ( __isArchiveMounted() && __loadFileFromArchive(pFile,(unsigned char*&)pbuf,psize) )
		{
			return true;
		}
	}
	*/
	psize = (int) __fileSize(pFile);
	if ( psize > 1 )
	{
		fw_filehandle f = __openFile(pFile,famReadOnly);
		if ( f )
		{
			pbuf = (char*) malloc(psize+1);
			if ( pbuf )
			{
				pbuf[psize] = 0;
				int ret = __readFile(f,pbuf,psize);
				__closeFile(f);
				return (ret==psize);
			}
			__closeFile(f);
		}
	}
	__log("__loadFile() %s failed",pFile);
	return false;
}

//
// file writer class
//

cFileWriter::cFileWriter(const char* name, bool append)
{
	crypter = NULL;
	file = __openFile(name,append?famAppend:famNewFile);
}

cFileWriter::~cFileWriter()
{
	if ( crypter )
	{
		delete crypter;
	}
	if ( isValid() )
	{
		__closeFile(file);
	}
}

void cFileWriter::setEncryptKey(const stringc& key)
{
	crypter = new cCrypto(key,cCrypto::ctEncrypt);
}

bool cFileWriter::isValid()
{
	return (file!=0);
}

long long cFileWriter::seek(long long pos, fwSeekMode origin)
{
	return __seekFile(file,pos,origin); 
}

/*
long long cFileWriter::tell()
{
	return ftell(file); 
}
*/

void cFileWriter::flush()
{
	if ( isValid() )
	{
		__flushFile(file);
	}
}

int cFileWriter::saveRaw(void* buf, u32 count)
{
	int ret = 0;
	if ( crypter )
	{
		char* copy = NULL;
		if ( count <= 1024 )
		{
			copy = temp;
		}
		else
		{
			copy = (char*) malloc(count);
		}
		if ( copy )
		{
			memcpy(copy,buf,count);
			crypter->cryptBytes((u8*)copy,count);
			if ( isValid() )
			{
				ret = (int) __writeFile(file,copy,count);
			}
			if ( copy != temp )
			{
				free(copy);
			}
		}
	}
	else
	{
		if ( isValid() )
		{
			ret = (int) __writeFile(file,buf,count);
		}
	}
	return ret;
}

void cFileWriter::saveInt(int val)
{
	saveRaw((void*)&val,sizeof(int));
}

void cFileWriter::saveInt64(const long long val)
{
	saveRaw((void*)&val,sizeof(long long));
}

void cFileWriter::saveVec3(const vector3df& vec)
{
	saveRaw((void*)&vec,sizeof(vector3df));
}

void cFileWriter::saveFloat(float data)
{
	saveRaw((void*)&data,sizeof(float));
}

void cFileWriter::saveStr(const char* str)
{
	u8 count = (u8) strlen(str);
	saveRaw((void*)&count,sizeof(u8));
	saveRaw((void*)str,sizeof(char)*count);
}

void cFileWriter::saveStr(const stringw& str) // u16 sized wide char on every platform!
{
	// head
	u8 count = str.size();
	saveRaw((void*)&count,sizeof(u8));
	// body
	u16* string16 = new u16[count];
	for(u32 _i=0;_i<count;_i++)
	{
		string16[_i] = (u16)str.c_str()[_i];
	}
	saveRaw((void*)string16,sizeof(u16)*count);
	delete [] string16;
}

void cFileWriter::saveStr(const stringc& str)
{
	u8 count = str.size();
	saveRaw((void*)&count,sizeof(u8));
	saveRaw((void*)str.c_str(),sizeof(char)*count);
}

//
// memory file reader class
//

cMemoryFileReader::cMemoryFileReader(const char* filename)
{
	_seekPos = 0;
	_len = 0;
	_ptr = NULL;
	/*
	if ( __isArchiveMounted() && __loadFileFromArchive(filename,(unsigned char*&)_ptr,_len) )
	{
		_valid = true;
	}
	else
	*/
	{
		_valid = __loadFile(filename,_ptr,_len);
	}
	_crypter = NULL;
	_detached = false;
}

cMemoryFileReader::cMemoryFileReader(char* buf, int len, bool detached)
{
	_ptr = buf;
	_seekPos = 0;
	_len = len;
	_valid = true;
	_crypter = NULL;
	_detached = detached;
}

cMemoryFileReader::~cMemoryFileReader()
{
	if ( _ptr && !_detached )
	{
		free(_ptr);
	}
	if ( _crypter )
	{
		delete _crypter;
	}
}

void cMemoryFileReader::__read(void* out, int len)
{
	memcpy(out,_ptr+_seekPos,len);
	_seekPos += len;
}

void cMemoryFileReader::seek(int pos)
{
	_seekPos = pos;
}

int cMemoryFileReader::tell()
{
	return _seekPos;
}

int cMemoryFileReader::size()
{
	return _len;
}

char* cMemoryFileReader::ptr()
{
	return _ptr;
}

bool cMemoryFileReader::isValid()
{
	return _valid;
}

void cMemoryFileReader::setDecryptKey(const stringc& key)
{
	if ( !_crypter )
	{
		_crypter = new cCrypto(key,cCrypto::ctDecrypt);
	}
}

void cMemoryFileReader::decryptFullData(const stringc& key)
{
	setDecryptKey(key);
	if ( _crypter && isValid() )
	{
		_crypter->cryptBytes((u8*)ptr(),size());
		delete _crypter;
		_crypter = NULL;
	}
}

void cMemoryFileReader::loadStr(char* pstr)
{
	u8 count = 0;
	__read(&count,sizeof(u8));
	if ( _crypter )
	{
		_crypter->cryptBytes(&count,sizeof(u8));
	}
	if ( count )
	{
		__read(pstr,count);
		if ( _crypter )
		{
			_crypter->cryptBytes((u8*)pstr,count);
		}
	}
	pstr[count] = 0;
}

void cMemoryFileReader::loadStr(stringc& pstr)
{
	u8 count = 0;
	char _temp[STR_LEN];
	memset(_temp,0,STR_LEN);
	__read(&count,sizeof(u8));
	if ( _crypter )
	{
		_crypter->cryptBytes(&count,sizeof(u8));
	}
	if ( count )
	{
		__read(_temp,count);
		if ( _crypter )
		{
			_crypter->cryptBytes((u8*)_temp,count);
		}
	}
	pstr = _temp;
}

void cMemoryFileReader::loadStr(stringw& pstr) // u16 sized wide char on every platform!
{
	u8 count = 0;
	u16 _temp[STR_LEN];
	memset(_temp,0,STR_LEN*sizeof(u16));
	__read(&count,sizeof(u8)); // string length
	if ( _crypter )
	{
		_crypter->cryptBytes(&count,sizeof(u8));
	}
	if ( count )
	{
		__read(_temp,count*sizeof(u16));
		if ( _crypter )
		{
			_crypter->cryptBytes((u8*)_temp,count*sizeof(u16));
		}
	}
#if MARKUP_SIZEOFWCHAR != 2
	pstr = L"";
	for(u32 _i=0;_i<count;_i++)
	{
		pstr.append((wchar_t)_temp[_i]);
	}
#else
	pstr = _temp;
#endif
}

int cMemoryFileReader::loadInt()
{
	int _int = 0;
	__read(&_int,sizeof(int));
	if ( _crypter )
	{
		_crypter->cryptBytes((u8*)&_int,sizeof(int));
	}
	return _int;
}

long long cMemoryFileReader::loadInt64()
{
	long long _int = 0;
	__read(&_int,sizeof(long long));
	if ( _crypter )
	{
		_crypter->cryptBytes((u8*)&_int,sizeof(long long));
	}
	return _int;
}

float cMemoryFileReader::loadFloat()
{
	float _flt = 0;
	__read(&_flt,sizeof(float));
	if ( _crypter )
	{
		_crypter->cryptBytes((u8*)&_flt,sizeof(float));
	}
	return _flt;
}

void cMemoryFileReader::loadVec3(vector3df& out)
{
	__read(&out.X,sizeof(float));
	__read(&out.Y,sizeof(float));
	__read(&out.Z,sizeof(float));
	if ( _crypter )
	{
		_crypter->cryptBytes((u8*)&out,sizeof(float)*3);
	}
}

void cMemoryFileReader::loadRaw(void* buf, int count)
{
	__read(buf,count);
	if ( _crypter )
	{
		_crypter->cryptBytes((u8*)buf,count);
	}
}

void cMemoryFileReader::setVersion(int ver)
{
	_version = ver;
}

int cMemoryFileReader::getVersion()
{
	return _version;
}

void cMemoryFileReader::loadVersion()
{
	setVersion(loadInt());
}

//
// xml writer
//

cXMLWriter::cXMLWriter(const char* file)
{
	_f = __openFile(file,famNewFile);
	if ( isValid() )
	{
		stringc _h = "<?xml version=\"1.0\"?>\r\n";
		__writeFile(_f,(void*)_h.c_str(),_h.size());
	}
}

cXMLWriter::~cXMLWriter()
{
	if ( isValid() )
	{
		while ( _name.size() )
		{
			closeTag();
		}
		__closeFile(_f);
	}
}

void cXMLWriter::saveInt(const char* name1, const char* name2, int value, bool parent)
{
	if ( isValid() )
	{
		char _t[STR_LEN];
		int l = sprintf(_t,"%s<%s %s=\"%d\" %s\r\n",_tab.c_str(),name1,name2,value,parent?">":"/>");
		__writeFile(_f,_t,l);
		if ( parent )
		{
			_name.push_back(name1);
			_tab += "\t";
		}
	}
}

void cXMLWriter::saveFloat(const char* name1, const char* name2, float value, bool parent)
{
	if ( isValid() )
	{
		char _t[STR_LEN];
		int l = sprintf(_t,"%s<%s %s=\"%.6f\" %s\r\n",_tab.c_str(),name1,name2,value,parent?">":"/>");
		__writeFile(_f,_t,l);
		if ( parent )
		{
			_name.push_back(name1);
			_tab += "\t";
		}
	}
}

void cXMLWriter::saveVec3(const char* name1, const vector3df& value, bool parent)
{
	if ( isValid() )
	{
		char _t[STR_LEN];
		int l = sprintf(_t,"%s<%s x=\"%.6f\" y=\"%.6f\" z=\"%.6f\" %s\r\n",_tab.c_str(),name1,value.X,value.Y,value.Z,parent?">":"/>");
		__writeFile(_f,_t,l);
		if ( parent )
		{
			_name.push_back(name1);
			_tab += "\t";
		}
	}
}

void cXMLWriter::saveString(const char* name1, const char* name2, const char* value, bool parent)
{
	if ( isValid() )
	{
		char* _t = (char*) malloc(strlen(value)+STR_LEN);
		assert(_t);
		int l = sprintf(_t,"%s<%s %s=\"%s\" %s\r\n",_tab.c_str(),name1,name2,value,parent?">":"/>");
		__writeFile(_f,_t,l);
		if ( parent )
		{
			_name.push_back(name1);
			_tab += "\t";
		}
		free(_t);
	}
}

void cXMLWriter::saveRect(const char* name1, int vx, int vy, int vw, int vh, bool parent)
{
	if ( isValid() )
	{
		char _t[STR_LEN];
		int l = sprintf(_t,"%s<%s x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" %s\r\n",_tab.c_str(),name1,vx,vy,vw,vh,parent?">":"/>");
		__writeFile(_f,_t,l);
		if ( parent )
		{
			_name.push_back(name1);
			_tab += "\t";
		}
	}
}

void cXMLWriter::closeTag()
{
	if ( isValid() && _name.size() )
	{
		_tab.erase(_tab.size()-1);
		char _t[STR_LEN];
		int l = sprintf(_t,"%s</%s>\r\n",_tab.c_str(),_name.getLast().c_str());
		__writeFile(_f,_t,l);
		_name.erase(_name.size()-1);
	}
}

//
// string handlers, parsers
//

int __atoi(const char* str)
{
	int v = 0;
	bool negative = (str[0]=='-');
	if ( negative )
	{
		str++;
	}
	while ( str[0] >= '0' && str[0] <= '9' )
	{
		v *= 10;
		v += str[0] - '0';
		str++;
	}
	return (negative?-v:v);
}

int __atoi(const stringc& str)
{
	return __atoi(str.c_str()); 
}

long long __atoi64(const char* str)
{
	long long v = 0;
	bool negative = (str[0]=='-');
	if ( negative )
	{
		str++;
	}
	while ( str[0] >= '0' && str[0] <= '9' )
	{
		v *= 10;
		v += str[0] - '0';
		str++;
	}
	return (negative?-v:v);
}

void __skipLine(char*& ptr)
{
	if ( *ptr )
	{
		while ( *ptr && *ptr != 10 ) ptr++;
		if ( *ptr == 10 )
		{
			ptr++;
		}
	}
}

void __extractAsString(char*& ptr, stringc& out, const char separator)
{
	char t[STR_LEN];
	char* tp = t;
	while ( *ptr != separator && *ptr && *ptr != 10 && *ptr != 13 )
	{
		*tp = *ptr;
		tp++;
		ptr++;
	}
	*tp = 0;
	out = t;
	if ( *ptr == separator )
	{
		ptr++;
	}
}

void __extractAsFloat(char*& ptr, float& out, const char separator)
{
	char t[STR_LEN];
	char* tp = t;
	while ( *ptr != separator && *ptr && *ptr != 10 && *ptr != 13 )
	{
		*tp = *ptr;
		tp++;
		ptr++;
	}
	*tp = 0;
	out = fast_atof(t);
	if ( *ptr == separator )
	{
		ptr++;
	}
}

void __extractAsInt(char*& ptr, int& out, const char separator)
{
	char t[STR_LEN];
	char* tp = t;
	while ( *ptr != separator && *ptr && *ptr != 10 && *ptr != 13 )
	{
		*tp = *ptr;
		tp++;
		ptr++;
	}
	*tp = 0;
	out = __atoi(t);
	if ( *ptr == separator )
	{
		ptr++;
	}
}

void __extractAsInt(wchar_t*& ptr, int& out, const wchar_t separator)
{
	out = 0;
	while ( *ptr != separator && *ptr && *ptr != L'\n' && *ptr != L'\r' )
	{
		out *= 10;
		out += (*ptr)-L'0';
		ptr++;
	}
	if ( *ptr == separator )
	{
		ptr++;
	}
}

bool __isNumber(const char c)
{
	return ((c>='0'&&c<='9')||c=='-'); 
}

static struct sFormatContext
{
	bool _formatting; // got %?
	bool _null; // insert zeros if fixed sized
	int _fixedSize;
	wchar_t _char,_letter;
	wchar_t* _string;
	u32 _num;
	int _decimals;
	int _i;
	va_list _args;
} fctx;

void __wsFormat(wchar_t* out, const wchar_t* fmt, ...) // handles only %02d %ls %%
{
	fctx._formatting = false;

	va_start(fctx._args, fmt);

	while ( *fmt != L'\0' )
	{
		fctx._char = *fmt;
		if ( fctx._formatting )
		{
			if ( fctx._char == L'%' ) // % sign
			{
				*out = fctx._char;
				out++;
				fctx._formatting = false; // off
			}
			else if ( fctx._char == L'0' )
			{
				fctx._null = true;
			}
			else if ( fctx._char >= L'1' && fctx._char <= L'9' )
			{
				fctx._fixedSize *= 10;
				fctx._fixedSize += fctx._char-L'0';
			}
			else if ( fctx._char == L'd' ) // decimal
			{
				fctx._num = va_arg(fctx._args, u32);
				if ( fctx._num&0x80000000 )
				{
					*out = L'-';
					out++;
					fctx._num = (u32) ((int)fctx._num*-1);
				}
				fctx._decimals = 10;
				for(fctx._i=1000000000;fctx._i>0;fctx._i/=10)
				{
					fctx._letter = (fctx._num/fctx._i) + L'0';
					fctx._num %= fctx._i;
					if ( !fctx._fixedSize || fctx._decimals <= fctx._fixedSize )
					{
						// emit
						if ( fctx._letter != L'0' || fctx._decimals == 1 )
						{
							fctx._null = true;
						}
						if ( fctx._letter != L'0' || fctx._null )
						{
							*out = fctx._letter;
							out++;
						}
						else if ( fctx._letter == L'0' && !fctx._null && fctx._decimals <= fctx._fixedSize )
						{
							*out = L' ';
							out++;
						}
					}
					fctx._decimals--;
				}
				fctx._formatting = false; // off
			}
			else if ( fctx._char == L's' ) // string
			{
				fctx._string = va_arg(fctx._args, wchar_t*);
				while(*fctx._string)
				{
					*out = *fctx._string;
					out++;
					fctx._string++;
				}
				fctx._formatting = false; // off
			}
		}
		else
		{
			if ( fctx._char == L'%' )
			{
				fctx._formatting = true;
				fctx._null = false; // reset
				fctx._fixedSize = 0;
			}
			else // simple emit
			{
				*out = fctx._char;
				out++;
			}
		}
		fmt++;
	}

	va_end(fctx._args);

	*out = L'\0'; // close
}

void __utf8ToWideChar(const stringc& utf8, stringw& out)
{
	out = L"";
	u8* ptr = (u8*)utf8.c_str();
	while ( *ptr )
	{
		wchar_t cin = wchar_t(*ptr++);
		if ( cin&0x80 )
		{
			int shift = 1;
			if ( (cin&0xE0) == 0xC0 ) // 2
			{
				cin &= 0x1F;
			}
			else if ( (cin&0xF0) == 0xE0 ) // 3
			{
				cin &= 0x0F;
				shift = 2;
			}
			else if ( (cin&0xF8) == 0xF0 ) // 4
			{
				cin &= 0x07;
				shift = 3;
			}
			for(int _i=0;_i<shift;_i++)
			{
				cin <<= 6;
				wchar_t cin2 = wchar_t(*ptr++);
				cin2 &= 0x3F;
				cin |= cin2;
			}
		}
		out.append(cin);
	}
}

void __wideCharToUTF8(const stringw& pIn, stringc& pUTF8)
{
	pUTF8 = "";
	for(u32 _i=0;_i<pIn.size();_i++)
	{
		u32 wc = pIn.c_str()[_i];
		if ( wc <= 0x7f )
		{
			pUTF8.append( char(wc) );
		}
		else if ( wc <= 0x7ff )
		{
			pUTF8.append( char( (wc>>6)|0xc0 ) );
			pUTF8.append( char( (wc&0x3f)|0x80 ) );
		}
		else if ( wc <= 0xffff )
		{
			pUTF8.append( char( (wc>>12)|0xe0 ) );
			pUTF8.append( char( ((wc>>6)&0x3f)|0x80 ) );
			pUTF8.append( char( (wc&0x3f)|0x80 ) );
		}
	}
}

void __escapeToWideChar(const stringc& pEscaped, stringw& out)
{
	out = L"";
	for(u32 _i=0;_i<pEscaped.size();_i++)
	{
		if ( pEscaped[_i] == '\\' && (pEscaped.size()-_i) >= 6 && pEscaped[_i+1] == 'u' )
		{
			_i += 2;
			u32 _val = 0;
			for(u32 _x=0;_x<4;_x++)
			{
				_val *= 16;
				char _ch = pEscaped[_i++];
				if ( _ch >= '0' && _ch <= '9' )
				{
					_val += u32(_ch-'0');
				}
				else
				{
					_val += u32(10+(_ch-'a'));
				}
			}
			out.append(wchar_t(_val));
			_i--;
		}
		else
		{
			out.append(pEscaped[_i]);
		}
	}
}

void __wideCharToEscape(const stringw& pWide, stringc& out)
{
	out = L"";
	for(u32 _i=0;_i<pWide.size();_i++)
	{
		wchar_t c = pWide[_i];
		if ( (c>='0' && c<='9') || (c>='a' && c<='z') || (c>='A' && c<='Z') )
		{
			out.append(char(c));
		}
		else // \u0000 format
		{
			char _temp[16];
			sprintf(_temp,"\\u%04x",int(c));
			out += _temp;
		}
	}
}

float __fRandFromTo(float pFrom, float pTo)
{
	return pFrom + (((rand()&0x7fff)/32767.f)*(pTo-pFrom));
}

//
// debug
//

#ifndef _NOLOG

#ifdef PLATFORM_AMIGA
bool gConsoleLogEnabled = true;
bool gFileLogEnabled = false;
bool gSerialLogEnabled = false;
int gConsoleWindow = 0;
int gLogFile = 0;
int gSerialOutput = 0;
#endif

char* gLogBuffer = NULL;

void __log(const char* format, ...)
{
	char __log_end[3] = { 0x0d, 0x0a, 0 };
	if ( !gLogBuffer )
	{
		gLogBuffer = (char*) malloc(4096);
	}

	va_list ap;
	va_start(ap,format);
#if defined PLATFORM_WINDOWS
	_vsnprintf_s(gLogBuffer,4096,_TRUNCATE,format,ap);
#else
# if defined __amiga__ && !defined PLATFORM_AMIGAOS4
	int ret = vsnprintf( gLogBuffer, 4096, format, (char*)ap );
# else
	int ret = vsnprintf( gLogBuffer, 4096, format, ap );
# endif
	if ( ret < 0 )
	{
		return;
	}
#endif
	va_end(ap);

#if defined PLATFORM_WINDOWS
	stringw wlog = gLogBuffer;
	wlog += L'\n';
	OutputDebugString(wlog.c_str());
#elif defined PLATFORM_AMIGA

# if defined PLATFORM_MORPHOS || defined PLATFORM_AROS
	kprintf(gLogBuffer);
	kprintf(__log_end);
# else // 68k
	// serial log
	if ( gSerialLogEnabled )
	{
		if ( !gSerialOutput )
		{
			gSerialOutput = Open("SER:",MODE_READWRITE);
		}
		if ( gSerialOutput )
		{
			Write(gSerialOutput,gLogBuffer,ret);
			Write(gSerialOutput,__log_end,2);
		}
	}

	if ( !gConsoleLogEnabled && !gFileLogEnabled )
	{
		return;
	}

	bool _isDosListLocked = true;
	if ( (gConsoleLogEnabled || gFileLogEnabled) && AttemptLockDosList(LDF_VOLUMES|LDF_WRITE) )
	{
		UnLockDosList(LDF_VOLUMES|LDF_WRITE);
		_isDosListLocked = false;
	}

	// console log
	if ( gConsoleLogEnabled && !_isDosListLocked )
	{
		if ( !gConsoleWindow )
		{
			gConsoleWindow = Open("CON:0/0/640/480",MODE_NEWFILE);
		}
		Write(gConsoleWindow,(void*)gLogBuffer,ret);
		Write(gConsoleWindow,__log_end,2);
	}

	// file log
	if ( gFileLogEnabled && !_isDosListLocked )
	{
		gLogFile = Open("Devs:Cloud/handler.log",MODE_READWRITE);
		if ( gLogFile )
		{
			Seek(gLogFile,0,OFFSET_END);
			Write(gLogFile,gLogBuffer,ret);
			Write(gLogFile,__log_end,2);
			Close(gLogFile);
			gLogFile = 0;
		}
	}
# endif
#else
	printf("%s\n",gLogBuffer);
#endif
}

void  __logClose()
{
	if ( gLogBuffer )
	{
		free(gLogBuffer);
		gLogBuffer = NULL;
	}
#ifdef PLATFORM_AMIGA
	if ( gConsoleWindow )
	{
#ifdef PLATFORM_AROS
		Close((void*)gConsoleWindow);
#else
		Close(gConsoleWindow);
#endif
		gConsoleWindow = 0;
	}
#endif
};

#endif // _NOLOG
