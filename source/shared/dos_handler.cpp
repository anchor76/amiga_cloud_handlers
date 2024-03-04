// dos_handler.cpp - anchor

// curl compile test with configure
// ./configure --target=m68k-amigaos --host=m68k-amigaos CPPFLAGS="-D__AMITCP__ -DHAVE_SYS_SOCKET_H -I/home/anchor/netinclude" LDFLAGS="-noixemul" LIBS="-lm" --enable-shared=no --disable-ldap

// ./configure --target=m68k-amigaos --host=m68k-amigaos CPPFLAGS="-D__AMITCP__ -DHAVE_SYS_SOCKET_H -I/home/anchor/netinclude -I/home/anchor/libz/m68k" LDFLAGS="-noixemul -fno-rtti -fno-exceptions" LIBS="-lm" --enable-shared=no --disable-ldap --disable-ares --disable-rt --disable-largefile --disable-ftp --disable-file --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-manual --disable-ipv6 --disable-pthreads --disable-sspi --disable-ntlm-wb --disable-tls-srp --with-ssl=/home/anchor/amissl

#include "dos_handler.h"

#ifndef PLATFORM_AROS
unsigned long __nocommandline = 1;
#endif

// lock task & volume
int gLockTask = 0;
int gLockVolume = 0;
#ifdef PLATFORM_AMIGA
struct DeviceNode *gDevNode = NULL;
#endif

// data handler for DOWNLOAD data
cFileOpen* gDownloadFile = NULL;
u32 gDownloadCursor = 0;
u32 gDownloadSize = 0; // buffer size
char* gDownloadBuffer = NULL;
size_t wrfu(void *ptr, size_t size, size_t nmemb, void *stream)
{
	int _length(size*nmemb);
	if ( gDownloadFile )
	{
		assert(_length<NETWORK_BUF_LEN);
		if ( !gDownloadFile->storeData((u8*)ptr,_length) )
		{
			__log("wrfu() error - out of ram");
			return 0;
		}
	}
	else
	{
		if ( gDownloadCursor + _length <= gDownloadSize )
		{
			memcpy(gDownloadBuffer+gDownloadCursor,ptr,_length);
			gDownloadCursor += _length;
			if ( gDownloadCursor < gDownloadSize )
			{
				gDownloadBuffer[gDownloadCursor] = 0; // with this we can avoid memset()
			}
		}
		else
		{
			__log("wrfu() error - write buffer is full");
			return 0;
		}
	}
	return _length;
}

// data handler for UPLOAD data
cFileOpen* gUploadFile = NULL;
u32 gUploadCursor = 0;
u32 gUploadSize = 0;
char* gUploadBuffer = NULL;
size_t rdfu(void *ptr, size_t size, size_t nmemb, void *stream)
{
	u32 _len = (size*nmemb);
	assert(_len<NETWORK_BUF_LEN);
	if ( gUploadFile )
	{
		int _ret = gUploadFile->readData((u8*)ptr,_len);
		//__log("rdfu() asked:%d loaded:%d",_len,_ret);
		return _ret;
	}
	else
	{
		if ( _len > gUploadSize )
		{
			_len = gUploadSize;
		}
		if ( _len )
		{
			memcpy( (u8*)ptr, gUploadBuffer + gUploadCursor, _len );
			gUploadCursor += _len;
			gUploadSize -= _len;
			return _len;
		}
	}
	return 0;
}

#ifdef CURL_DEBUG_OUTPUT
int curlTrace(CURL *handle, curl_infotype type, char *data, size_t size,  void *userp)
{
	(void)handle; /* prevent compiler warning */

	/*
	// remove new line char
	if ( data )
	{
		int len = strlen(data);
		if ( data[len-1] == 10 )
		{
			data[len-1] = 0;
		}
	}
	*/

	switch (type)
	{
  case CURLINFO_TEXT:
	  __log("== Info: %s", data);
  default: /* in case a new one is introduced to shock us */
	  return 0;
  case CURLINFO_HEADER_OUT:
	  __log("=> Send header: %s", data);
	  break;
  case CURLINFO_HEADER_IN:
	  __log("<= Recv header: %s",data);
	  break;
  case CURLINFO_DATA_OUT:
	  __log("=> Send data (%d bytes)",size);
	  break;
  case CURLINFO_DATA_IN:
	  __log("<= Recv data (%d bytes)",size);
	  break;
  case CURLINFO_SSL_DATA_OUT:
	  __log("=> Send SSL data (%d bytes)",size);
	  break;
  case CURLINFO_SSL_DATA_IN:
	  __log("<= Recv SSL data (%d bytes)",size);
	  break;
	}
	return 0;
}
#endif

//
// cCodePage class
//

cCodePage::cCodePage(const char* pName, const char* pBufPtr, int pBufLen)
{
	_name = pName;

	// parse
	char* buf = (char*) pBufPtr;
	while ( *buf && buf-pBufPtr < pBufLen )
	{
		if ( *buf == '0' )
		{
			// read a row
			buf++;
			if ( *buf == 'x' )
			{
				buf++;
				int _to = 0;
				int _from = 0;
				sscanf(buf,"%02x    0x%04x",&_to,&_from);
				if ( _from && _from != _to )
				{
					_charPairs.push_back((_from<<16|_to));
				}
			}
		}
		__skipLine(buf);
	}
}

int cCodePage::translateUnicode(int puc, bool toAmiga)
{
	if ( toAmiga )
	{
		for(u32 _i=0;_i<_charPairs.size();_i++)
		{
			if ( (_charPairs[_i]>>16) == puc )
			{
				return (_charPairs[_i]&0xffff);
			}
		}
	}
	else // from amiga
	{
		puc &= 0xff;
		for(u32 _i=0;_i<_charPairs.size();_i++)
		{
			if ( (_charPairs[_i]&0xffff) == puc )
			{
				return (_charPairs[_i]>>16);
			}
		}
	}
	return puc;
}

//
// cFileOpen class
//

cFileOpen::cFileOpen(eOpenMode pMode, FileLock* pLock)
{
	_cachedFileSize = 0;
	_openCursor = 0;
	_openMode = pMode;
	_lock = pLock;
	_userData = 0;
}

cFileOpen::~cFileOpen()
{
	purgeCache();
}

void cFileOpen::purgeCache()
{
	for(u32 _i=0;_i<_cache.size();_i++)
	{
		free(_cache[_i]);
	}
	_cache.clear();
	_cachedFileSize = 0;
	_openCursor = 0;
}

int cFileOpen::__storeData(u8* pBuf, int pLen) // atomic
{
	int _startChunkId = _openCursor / NETWORK_BUF_LEN;
	int _endChunkId = (_openCursor+pLen) / NETWORK_BUF_LEN;
	int _startOfs = _openCursor % NETWORK_BUF_LEN;
	int _emptySpaceInLastChunk = NETWORK_BUF_LEN - _startOfs;
	if ( _cache.size() <= u32(_endChunkId) )
	{
		u8* _chunk = (u8*) malloc(NETWORK_BUF_LEN);
		if ( !_chunk )
		{
			return 0;
		}
		_cache.push_back(_chunk);
	}
	if ( _startChunkId == _endChunkId || (pLen-_emptySpaceInLastChunk) == 0 )
	{
		memcpy(_cache.getLast()+_startOfs,pBuf,pLen);
	}
	else // two piece? - must be minimum two chunk allocated at this point
	{
		memcpy(_cache[_cache.size()-2]+_startOfs,pBuf,_emptySpaceInLastChunk);
		memcpy(_cache[_cache.size()-1],pBuf+_emptySpaceInLastChunk,pLen-_emptySpaceInLastChunk);
	}
	_openCursor += pLen;
	return pLen;
}

int cFileOpen::storeData(u8* pBuf, int pLen)
{
	int _ret = 1;
	while ( pLen && _ret )
	{
		if ( pLen > NETWORK_BUF_LEN )
		{
			_ret = __storeData(pBuf,NETWORK_BUF_LEN);
			pLen -= NETWORK_BUF_LEN;
			pBuf += NETWORK_BUF_LEN;
		}
		else
		{
			_ret = __storeData(pBuf,pLen);
			pLen = 0;
		}
	}
	return _ret;
}

int cFileOpen::__readData(u8* pBuf, int pLen)
{
	/*
	int _readLen = 0;
	if ( _openCursor + pLen <= _cachedFileSize )
	{
		_readLen = pLen;
	}
	else if ( _openCursor < _cachedFileSize )
	{
		_readLen = _cachedFileSize - _openCursor;
	}
	if ( _readLen )
	{
	if ( _readLen > gNetworkIOBufferLength )
	{
		_readLen = gNetworkIOBufferLength;
	}
	*/
	//__log("read from ram. cursor:%d lenght:%d",_open->_cursor,_readLen);
	int _startChunkId = _openCursor / NETWORK_BUF_LEN;
	int _endChunkId = (_openCursor+pLen) / NETWORK_BUF_LEN;
	int _startOfs = _openCursor % NETWORK_BUF_LEN;
	int _emptySpaceInLastChunk = NETWORK_BUF_LEN - _startOfs;
	if ( _startChunkId == _endChunkId || (pLen-_emptySpaceInLastChunk) == 0 )
	{
		memcpy(pBuf, _cache[_startChunkId]+_startOfs, pLen);
	}
	else // two piece? - must be minimum two chunk allocated at this point
	{
		memcpy(pBuf, _cache[_startChunkId]+_startOfs, _emptySpaceInLastChunk);
		memcpy(pBuf+_emptySpaceInLastChunk, _cache[_endChunkId], pLen-_emptySpaceInLastChunk);
	}
	_openCursor += pLen;
	//}
	return pLen;
}

int cFileOpen::readData(u8* pBuf, int pLen)
{
	int _readLen = 0;
	if ( _openCursor + pLen <= _cachedFileSize )
	{
		_readLen = pLen;
	}
	else if ( _openCursor < _cachedFileSize )
	{
		_readLen = _cachedFileSize - _openCursor;
	}

	int _readed = 0; // total
	int _processed = 0;
	while ( _readLen )
	{
		if ( _readLen > NETWORK_BUF_LEN )
		{
			_processed = __readData(pBuf,NETWORK_BUF_LEN);
		}
		else
		{
			_processed = __readData(pBuf,_readLen);
		}
		_readLen -= _processed;
		pBuf += _processed;
		_readed += _processed;
	}
	return _readed;
}

//
// cDosFile class
//

cDosFile::cDosFile()
{
	_folder = false;
	_fileSize = 0;
	_contentDownloaded = false;

	// lock related
	/*
	_lockCount = 0;
	_sysLock.fl_Link = 0;
	_sysLock.fl_Key = (int)this;
	_sysLock.fl_Access = 0;
	_sysLock.fl_Task = 0;
	_sysLock.fl_Volume = 0;
#ifdef PLATFORM_AMIGAOS4
	_sysLock.fl_StructSize = sizeof(struct Lock);
	_sysLock.fl_DOSType = 0;
#endif
	*/
}

cDosFile::~cDosFile()
{
	for(u32 _i=0;_i<_opens.size();_i++)
	{
		delete _opens[_i];
	}
	for(u32 _i=0;_i<_locks.size();_i++)
	{
		delete _locks[_i];
	}
}

bool cDosFile::unlock(FileLock* plock)
{
	for(u32 _i=0;_i<_locks.size();_i++)
	{
		if ( _locks[_i] == plock )
		{
			free(_locks[_i]);
			_locks.erase(_i);
			return true;
		}
	}
	return false;
}

bool cDosFile::close(cFileOpen* popen)
{
	for(u32 _i=0;_i<_opens.size();_i++)
	{
		if ( _opens[_i] == popen )
		{
			delete popen;
			_opens.erase(_i);
			return true;
		}
	}
	return false;
}

//
// cDosHandler class
//

cDosHandler::cDosHandler()
{
	_accessToken = NULL;
	_refreshToken = NULL;
	_clientCode = NULL;
	_curlCtx = NULL;
	_authed = false;
	_quotaLimit = 0;
	_quotaUsed = 0;
	_blockSize = 1024;
	_licenceName = "nobody";
	_codePageIndex = -1; // off
	_expiresIn = 0;
	_tokenExpiresAt = 0;
	_appendUploadSupported = false;

#ifndef OFFLINE_SIMULATION
	_curlCtx = curl_easy_init();
	curl_easy_setopt(_curlCtx, CURLOPT_WRITEFUNCTION, wrfu); 
	curl_easy_setopt(_curlCtx, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(_curlCtx, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(_curlCtx, CURLOPT_ACCEPT_ENCODING, "");
	/*
	curl_easy_setopt(_curlCtx, CURLOPT_SSLVERSION, 3);
	curl_easy_setopt(_curlCtx, CURLOPT_TIMEOUT, 0);
	curl_easy_setopt(_curlCtx, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	*/
#endif

	_root._folder = true;
	_root._title = gVolumeName();
	_lockCount = 0;

#ifdef CURL_DEBUG_OUTPUT
	curl_easy_setopt(_curlCtx, CURLOPT_VERBOSE, 1); 
	curl_easy_setopt(_curlCtx, CURLOPT_DEBUGFUNCTION, curlTrace);
#endif
}

cDosHandler::~cDosHandler()
{
	/*
	if ( gUploadBuffer ) // upload buffer
	{
		free(gUploadBuffer);
	}
	*/

	for(u32 _i=0;_i>_codePages.size();_i++)
	{
		delete _codePages[_i];
	}
	_codePages.clear();

	for(u32 _i=0;_i<_fileList.size();_i++)
	{
		delete _fileList[_i];
	}
	_fileList.clear();

	if ( _accessToken )
	{
		free(_accessToken);
	}

	if ( _refreshToken )
	{
		free(_refreshToken);
	}

	if ( _clientCode )
	{
		free(_clientCode);
	}

#ifndef OFFLINE_SIMULATION
	if ( _curlCtx )
	{
		curl_easy_cleanup(_curlCtx);
	}
#endif
}

void cDosHandler::init()
{
	__log(_version.c_str());
}

void cDosHandler::__initDownloadToLocalIoBuffer()
{
	gDownloadBuffer = _iobuf;
	gDownloadSize = NETWORK_BUF_LEN;
	gDownloadCursor = 0;
	gDownloadFile = NULL;
	//memset(gDownloadBuffer,0,gDownloadSize);
}

void cDosHandler::__initDownloadToCustomBuffer(char* buf, int len)
{
	gDownloadBuffer = buf;
	gDownloadSize = len;
	gDownloadCursor = 0;
	gDownloadFile = NULL;
	//memset(gDownloadBuffer,0,min_(len,NETWORK_BUF_LEN));
}

void cDosHandler::__initDownloadToFileBuffer(cFileOpen* pFile)
{
	gDownloadBuffer = _iobuf; // for response
	gDownloadSize = NETWORK_BUF_LEN;
	gDownloadCursor = 0;
	gDownloadFile = pFile;
	//memset(gDownloadBuffer,0,gDownloadSize);
}

void cDosHandler::__initUploadFromFileBuffer(cFileOpen* pHandle)
{
	gDownloadBuffer = _iobuf; // for response
	gDownloadSize = NETWORK_BUF_LEN;
	gDownloadCursor = 0;
	gDownloadFile = NULL;
	//memset(gDownloadBuffer,0,gDownloadSize);

	gUploadBuffer = NULL;
	gUploadSize = pHandle->_cachedFileSize;
	gUploadCursor = 0;
	gUploadFile = pHandle;
}

void cDosHandler::onHandlerCommand(const stringc& pStr)
{
	stringc _path(pStr);
	_path.make_lower();
	if ( _path.find("@about") == 0 )
	{
#ifdef PLATFORM_AMIGA
		struct EasyStruct myES =
		{
			sizeof(struct EasyStruct),
			0,
			(EASY_TEXT)"About",
			(EASY_TEXT)"%s\nProgrammed by: Norbert Kett (anchor@rocketmail.com)\nRegistered to: %s (%s)\nComponents: %s",
			(EASY_TEXT)"Ok",
		};

		const void* ptrs[5];
		ptrs[0] = _version.c_str();
		ptrs[1] = _licenceName.c_str();
		ptrs[2] = _accountMailAddress.c_str();
		ptrs[3] = curl_version();
		ptrs[4] = NULL;

#ifdef PLATFORM_AMIGAOS4
		IIntuition = (struct IntuitionIFace*) GetInterface(IntuitionBase, "main", 1, NULL);
#endif

		int answer = EasyRequestArgs(NULL, &myES, NULL, ptrs);
#endif
	}
	else if ( _path.find("@benchmark") == 0 )
	{
		//
		// upload 1MB data, download 1MB data, delete datafile
		//

		int _uploadSpeed = 0;
		int _downloadSpeed = 0;
		const int _len = 1024*1024; // 1MB
#ifdef PLATFORM_AMIGA
		struct DateStamp _t1,_t2;
#endif
		// create file
		cDosFile* _file = new cDosFile();
		_file->_title = "amiga_benchmark_test.bin";
		if ( beginUpload(_file) )
		{
			// upload 1M
			cFileOpen* fo = new cFileOpen(cFileOpen::omUpload,NULL);
			_file->_opens.push_back(fo);
			u8* _temp = (u8*) malloc(_len);
			fo->storeData(_temp,_len);
			fo->_cachedFileSize = _len;
			fo->_openCursor = 0;
#ifdef PLATFORM_AMIGA
			DateStamp(&_t1);
#endif
			if ( uploadFile(_file,fo,true) )
			{
#ifdef PLATFORM_AMIGA
				DateStamp(&_t2);
				_uploadSpeed = ((_t2.ds_Minute*3000)+_t2.ds_Tick) - ((_t1.ds_Minute*3000)+_t1.ds_Tick);
				_uploadSpeed = int( (_len/1024.f) / (_uploadSpeed/50.f));
#endif

				_file->close(fo);

				// download 1M
				fo = new cFileOpen(cFileOpen::omUpload,NULL);
				_file->_opens.push_back(fo);
#ifdef PLATFORM_AMIGA
				DateStamp(&_t1);
#endif
				if ( downloadFile(_file->_downloadURL,0,_len,_temp) )
				{
#ifdef PLATFORM_AMIGA
					DateStamp(&_t2);
					_downloadSpeed = ((_t2.ds_Minute*3000)+_t2.ds_Tick) - ((_t1.ds_Minute*3000)+_t1.ds_Tick);
					_downloadSpeed = int( (_len/1024.f) / (_downloadSpeed/50.f));
#endif
				}

				_file->close(fo);
			}
			free(_temp);
			updateFile(_file,false,false,true);
		}
		delete _file;

#ifdef PLATFORM_AMIGA
		struct EasyStruct myES =
		{
			sizeof(struct EasyStruct),
			0,
			(EASY_TEXT)"Benchmark result",
			(EASY_TEXT)"Upload speed: %ld kB/s\nDownload speed: %ld kB/s",
			(EASY_TEXT)"Ok",
		};

		const void* ptrs[3];
		ptrs[0] = (void*)_uploadSpeed;
		ptrs[1] = (void*)_downloadSpeed;
		ptrs[2] = NULL;

#ifdef PLATFORM_AMIGAOS4
		IIntuition = (struct IntuitionIFace*) GetInterface(IntuitionBase, "main", 1, NULL);
#endif

		int answer = EasyRequestArgs( NULL, &myES, NULL, ptrs );
#endif
	}
}

void cDosHandler::getExpirationData(char* buf)
{
	getJSON_int(buf,"\"expires_in\":",_expiresIn);
#ifdef PLATFORM_AMIGA
	struct DateStamp _expiresAt;
	DateStamp(&_expiresAt);
	_tokenExpiresAt = (86400*_expiresAt.ds_Days)+(_expiresAt.ds_Minute*60)+(_expiresAt.ds_Tick/50);
	_tokenExpiresAt += _expiresIn;
#endif
}

void cDosHandler::jumpToKey(char*& pBuf, const char* pKey)
{
	char _c = *pBuf;
	char _first = pKey[0];
	int _keyLen = strlen(pKey);
	while ( _c )
	{
		if ( _c == _first && !memcmp(pBuf,pKey,_keyLen) )
		{
			return;
		}
		pBuf++;
		_c = *pBuf;
	}
}

void cDosHandler::getJSON_string(char*& pBuf, const char* pKey, stringc& out)
{
	out = "";
	jumpToKey(pBuf,pKey);
	char _char;
	if ( *pBuf )
	{
		pBuf += strlen(pKey);
		pBuf++; // skip white space
		if ( *pBuf == '"' )
		{
			pBuf++;
			while ( *pBuf && *pBuf != '"' )
			{
				_char = *pBuf;
				if ( _char == '\\' && pBuf[1] ) // escape sign?
				{
					if ( pBuf[1] != 'u' ) // keep dropbox \u0000 encoding
					{
						pBuf++;
						_char = *pBuf;
					}
				}
				out.append(_char);
				pBuf++;
			}
		}
	}
}

bool cDosHandler::moveToChar(char*& pBuf, const char pChar)
{
	while ( true )
	{
		if ( *pBuf == 0 || *pBuf == '}' )
		{
			return false;
		}
		else if ( *pBuf == pChar )
		{
			break;
		}
		pBuf++;
	}
	return true;
}

void cDosHandler::getJSON_stringArray(char*& pBuf, const char* pKey, array<stringc>& out)
{
	jumpToKey(pBuf,pKey);
	if ( *pBuf )
	{
		pBuf += strlen(pKey);
		while ( true )
		{
			if ( !moveToChar(pBuf,'"')  )
			{
				return;
			}
			if ( !moveToChar(pBuf,':')  )
			{
				return;
			}
			if ( !moveToChar(pBuf,'"')  )
			{
				return;
			}
			// at link content
			pBuf++;
			if ( *pBuf == 0 )
			{
				return;
			}
			char* p1 = pBuf;
			if ( !moveToChar(pBuf,'"')  )
			{
				return;
			}
			memcpy(_temp,p1,pBuf-p1);
			_temp[pBuf-p1] = 0;
			out.push_back(_temp);
			// skip last "
			pBuf++;
			if ( *pBuf == 0 )
			{
				return;
			}
		}
	}
}

void cDosHandler::getJSON_int(char*& pBuf, const char* pKey, int& out)
{
	out = 0;
	jumpToKey(pBuf,pKey);
	if ( *pBuf )
	{
		pBuf += strlen(pKey);
		pBuf++; // skip white space
		if ( *pBuf == '"' )
		{
			pBuf++;
		}
		out = __atoi(pBuf);
		while ( *pBuf && *pBuf != '"' && *pBuf != '\n' )
		{
			pBuf++;
		}
	}
}

void cDosHandler::getJSON_int64(char*& pBuf, const char* pKey, long long& out)
{
	out = 0;
	jumpToKey(pBuf,pKey);
	if ( *pBuf )
	{
		pBuf += strlen(pKey);
		pBuf++; // skip white space
		if ( *pBuf == '"' )
		{
			pBuf++;
		}
		out = __atoi64(pBuf);
		while ( *pBuf && *pBuf != '"' && *pBuf != '\n' )
		{
			pBuf++;
		}
	}
}

void cDosHandler::getJSON_bool(char*& pBuf, const char* pKey, bool& out)
{
	out = false;
	jumpToKey(pBuf,pKey);
	if ( *pBuf )
	{
		pBuf += strlen(pKey);
		pBuf++;
		if ( *pBuf == 't' )
		{
			out = true;
		}
	}
}

void cDosHandler::messageBox(const char* pMsg, const char* pCaption)
{
#if defined _WIN32 || defined _WIN64
	MessageBox(NULL,stringw(pMsg).c_str(),stringw(pCaption).c_str(),0);
#else
	struct EasyStruct myES =
	{
		sizeof(struct EasyStruct),
		0,
		(EASY_TEXT)(pCaption ? pCaption : "Error"),
		(EASY_TEXT)pMsg,
		(EASY_TEXT)"Ok",
	};
#ifdef PLATFORM_AMIGAOS4
	IIntuition = (struct IntuitionIFace*) GetInterface(IntuitionBase, "main", 1, NULL);
#endif
	EasyRequestArgs(NULL, &myES, NULL, NULL );
#endif
}

bool cDosHandler::checkLicence()
{
	// read key file
	int len = 0;
	char* _buf = NULL;
	if ( __loadFile(_keyfilePath.c_str(),_buf,len) )
	{
		stringc _p; _p.append('A'); _p.append('m'); _p.append('i'); _p.append('g'); _p.append('a'); _p.append('1'); _p.append('2'); _p.append('0'); _p.append('0');
		cCrypto _crypt(_p.c_str(),cCrypto::ctDecrypt);
		_crypt.cryptBytes((u8*)_buf,len);
		char* __buf = (char*) _buf;
		getJSON_string(__buf,"\"name\":",_licenceName);
		if ( _licenceName.size() )
		{
			array<stringc> _mailAddressList;
			stringc _temp = "";
			while ( true )
			{
				getJSON_string(__buf,"\"email\":",_temp);
				if ( _temp.size() && _temp.find("@") != -1 && _temp.find(".") != -1 )
				{
					_mailAddressList.push_back(_temp);
				}
				else
				{
					break;
				}
			}

			if ( _mailAddressList.size() )
			{
				stringc ama = _accountMailAddress;
				ama.make_lower();
				bool _found = false;
				for(u32 _i=0;_i<_mailAddressList.size();_i++)
				{
					stringc lma = _mailAddressList[_i];
					lma.make_lower();
					if ( lma == ama )
					{
						_licenceMailAddress = lma;
						_found = true;
						break;
					}
				}

				if ( !_found )
				{
					messageBox("Keyfile belongs to another account!");
				}
			}
			else
			{
				messageBox("Invalid keyfile! (email)");
			}
		}
		else
		{
			messageBox("Invalid keyfile! (name)");
		}
		free(_buf);
	}
	return true;
}

//
// codepage related
//

void cDosHandler::__readCodePage(const char* pName)
{
	stringc _path = _codePagesPath;
	_path += "/";
	_path += pName;
	_path += ".txt";
	char* _buf = NULL;
	int _len = 0;
	if ( __loadFile(_path.c_str(),_buf,_len) )
	{
		_codePages.push_back(new cCodePage(pName,_buf,_len));
		free(_buf);
	}
}

bool cDosHandler::__selectCodePage(const char* pName)
{
	for(u32 _i=0;_i<_codePages.size();_i++)
	{
		if ( _codePages[_i]->_name == pName )
		{
			_codePageIndex = _i;
			return true;
		}
	}
	return false;
}

void cDosHandler::__translateString(stringw& pText, bool toAmiga)
{
	if ( _codePageIndex != -1 )
	{
		for(u32 _i=0;_i<pText.size();_i++)
		{
			pText[_i] = _codePages[_codePageIndex]->translateUnicode(pText[_i],toAmiga);
		}
	}
}

void cDosHandler::__convertFromUTF8(const stringc& pUTF8, stringc& pAmiga)
{
	stringw wtemp;
	__utf8ToWideChar(pUTF8,wtemp);
	__convertFromUnicode(wtemp,pAmiga);
}

void cDosHandler::__convertFromEscaped(const stringc& pEscaped, stringc& pAmiga)
{
	stringw wtemp;
	__escapeToWideChar(pEscaped,wtemp);
	__convertFromUnicode(wtemp,pAmiga);
}

void cDosHandler::__convertFromUnicode(stringw& pUnicode, stringc& pAmiga)
{
	__translateString(pUnicode,true);

	// convert remained widechar chars to a special sign
	for(u32 _i=0;_i<pUnicode.size();_i++)
	{
		if ( pUnicode[_i]&0xffffff00 )
		{
			pUnicode[_i] = 0x7f;
		}
	}

	pAmiga = stringc(pUnicode);

#ifdef PLATFORM_AMIGA
	if ( pAmiga.size() > 100 ) // limit file name length
	{
		pAmiga = pAmiga.subString(0,100);
	}
#endif

	__repairName(pAmiga);
}

void cDosHandler::__convertToUTF8(const stringc& pAmiga, stringc& pUTF8)
{
	stringw wtemp = stringw(pAmiga);
	__translateString(wtemp,false);
	__wideCharToUTF8(wtemp,pUTF8);
}

void cDosHandler::__convertToEscaped(const stringc& pAmiga, stringc& pEscaped)
{
	stringw wtemp = stringw(pAmiga);
	__translateString(wtemp,false);
	__wideCharToEscape(wtemp,pEscaped);
}

void cDosHandler::__repairName(stringc& pAmiga)
{
	for(u32 _i=0;_i<pAmiga.size();_i++)
	{
		for(u32 _r=0;_r<_replaceDescriptor.size()/2;_r+=2)
		{
			if ( pAmiga[_i] == _replaceDescriptor[_r] )
			{
				pAmiga[_i] = _replaceDescriptor[_r+1];
			}
		}
	}
}

//
// low level
//

#ifdef CHECK_LOCK
bool cDosHandler::__CheckLock(cDosFile* pFile)
{
	// this part is outdated
	/*
	if ( pFile )
	{
#ifdef PLATFORM_AMIGA
		//__log("CheckLock() link:%08x next:%08x",pFile,BADDR(pFile->_sysLock.fl_Link));
		if ( BADDR(pFile->_sysLock.fl_Link) == pFile )
		{
			__log("cGoogleDriveHandler::__CheckLock() fl_Link points to self! **********",pFile);
			return false;
		}
		else if ( BADDR(pFile->_sysLock.fl_Link) != NULL )
		{
			__log("cGoogleDriveHandler::__CheckLock() fl_Link not null! **********",pFile);
			return false;
		}
#endif
		if ( pFile != &_root )
		{
			for(u32 _i=0;_i<_fileList.size();_i++)
			{
				if ( _fileList[_i] == pFile )
				{
					return true;
				}
			}
			__log("cGoogleDriveHandler::__CheckLock() UNKNOWN_LOCK: %08x **********",pFile);
		}
	}
	*/
	return false;
}
#endif

void cDosHandler::__makeDateStamp(const stringc& pDateString, struct DateStamp* pDateStamp)
{
	static const int _dayAccum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334  };
	if ( pDateString.size() >= 19 )
	{
		int _year,_month,_day,_hour,_minute,_second;
		sscanf(pDateString.c_str(),"%04d-%02d-%02dT%02d:%02d:%02d",&_year,&_month,&_day,&_hour,&_minute,&_second);
		_year -= 1978;
		pDateStamp->ds_Days = int(_year*365.25f) + _dayAccum[_month-1] + (_day-1);
		pDateStamp->ds_Minute = (_hour*60) + _minute;
		pDateStamp->ds_Tick = (_second*50);
	}
}

cDosFile* cDosHandler::__getNextObjectWithSameParent(cDosFile* _from)
{
	bool _watch = (_from==&_root);
	for(u32 _i=0;_i<_fileList.size();_i++)
	{
		if ( !_watch && _fileList[_i] == _from )
		{
			_watch = true;
		}
		if ( _watch && _fileList[_i] != _from && _fileList[_i]->_parentId == _from->_parentId )
		{
			return _fileList[_i];
		}
	}
	return NULL;
}

cDosFile* cDosHandler::__getObject(const stringc& pName, cDosFile* pParent, int& pError)
{
	cDosFile* _pout;
	return __getObject(pName, pParent, pError, _pout);
}

cDosFile* cDosHandler::__getObject(const stringc& pName, cDosFile* pParent, int& pError, cDosFile*& pDirectParentOut)
{
	stringc _name = pName;
	_name.make_lower();
	cDosFile* _file = NULL;
	cDosFile* _parent = pParent;
	pDirectParentOut = _parent;
	pError = 0;

	// fixlock
	if ( !_parent )
	{
		_parent = &_root;
		pDirectParentOut = _parent;
	}

	// remove volume name
	u32 _ofs = _name.find(":");
	if ( _ofs != -1 )
	{
		_name = _name.subString(_ofs+1,_name.size()-_ofs);
	}

	// find updir chars and tokenize them
	if ( _name.size() )
	{
		for(int _i=int(_name.size()-1);_i>=0;_i--)
		{
			if ( _name[_i] == '/' )
			{
				if ( (_i && _name[_i-1] == '/') || _i==0 )
				{
					_name[_i] = UPDIR_SIGN;
				}
			}
		}
	}

	// split to dir names
	array<stringc> _dirs;
	_name.split(_dirs,"/");
	_file = NULL;
	_file = _parent;
	for(u32 _d=0;_d<_dirs.size();_d++)
	{
		stringc _filePart = _dirs[_d];

		// handle updirs
		while ( _filePart[0] == UPDIR_SIGN )
		{
			_filePart.erase(0);
			if ( _parent != &_root )
			{
				if ( _file->_parentId == _root._id )
				{
					_parent = _file = &_root;
					pDirectParentOut = _parent;
				}
				else
				{
					for(u32 _f=0;_f<_fileList.size();_f++)
					{
						if ( _fileList[_f]->_id == _file->_parentId )
						{
							_parent = _file = _fileList[_f];
							pDirectParentOut = _parent;
							break;
						}
					}
				}
			}
		}

		if ( !_filePart.size() )
		{
			continue;
		}

		if ( !__checkFolder(_file) ) // sync
		{
			pError = ERROR_DISK_NOT_VALIDATED; // network error
			return NULL;
		}

		_file = NULL;

		for(u32 _f=0;_f<_fileList.size();_f++)
		{
			if ( _fileList[_f]->_titleLC == _filePart && _fileList[_f]->_parentId == _parent->_id )
			{
				_parent = _file = _fileList[_f];
				pDirectParentOut = _parent;
				break;
			}
		}

		if ( !_file )
		{
			pError = ERROR_OBJECT_NOT_FOUND;
			return NULL;
		}
	}

	if ( !__checkFolder(_file) ) // sync
	{
		pError = ERROR_DISK_NOT_VALIDATED; // network error
		return NULL;
	}

	return _file;
}

cDosFile* cDosHandler::__getFirstChild(cDosFile* pFolder) // only for folders!
{
	if ( pFolder && pFolder->_folder )
	{
		__checkFolder(pFolder);
		for(u32 _i=0;_i<_fileList.size();_i++)
		{
			if ( _fileList[_i]->_parentId == pFolder->_id )
			{
				return _fileList[_i];
			}
		}
	}
	return NULL;
}

bool cDosHandler::__checkFolder(cDosFile* pFolder)
{
	if ( pFolder && pFolder->_folder && !pFolder->_contentDownloaded ) // nem volt még letöltve a tartalma?
	{
		if ( !getFileList( (pFolder==(&_root)) ? "root" : pFolder->_id ) ) // download content
		{
			return false;
		}
		pFolder->_contentDownloaded = true;
	}
	return true;
}

FileLock* cDosHandler::__lockObject(cDosFile* pFile, int mode)
{
	if ( mode == ACCESS_WRITE && pFile->_locks.size() && pFile->_locks.getLast()->fl_Access == ACCESS_WRITE )
	{
		return NULL; // ERROR_OBJECT_IN_USE;
	}

	FileLock* lock = (FileLock*) malloc(sizeof(struct FileLock));
	if ( lock )
	{
		lock->fl_Access = mode;
		lock->fl_Key = (int)pFile;
#ifdef PLATFORM_AMIGA
		lock->fl_Task = (struct MsgPort*)gLockTask;
#else
		lock->fl_Task = gLockTask;
#endif
#ifdef PLATFORM_AROS
		lock->fl_Volume = (void*) gLockVolume;
#else
		lock->fl_Volume = gLockVolume;
#endif

		pFile->_locks.push_back(lock);

		// manage lock counts
		__incLockCount(); // volume statistics

		return lock;
	}

	return NULL;
}

bool cDosHandler::__deleteObject(cDosFile* pFile, int& pError)
{
	if ( pFile )
	{
		if ( pFile->_folder && __getFirstChild(pFile) ) // if dir, empty?
		{
			pError = ERROR_DIRECTORY_NOT_EMPTY;
			return false;
		}
		else
		{
			if ( updateFile(pFile,false,false,true) )
			/*
			google drive delete function is not working!
			if ( deleteFile(pFile->_id) )
			*/
			{
				for(u32 _i=0;_i<_fileList.size();_i++)
				{
					if ( _fileList[_i] == pFile )
					{
						_fileList.erase(_i);
						delete pFile;
						return true;
					}
				}
			}
		}
	}
	pError = ERROR_OBJECT_NOT_FOUND;
	return false;
}

cDosFile* cDosHandler::__createObject(const char* pName, cDosFile* pParentLock, int& pError)
{
	cDosFile* _parent = NULL;
	cDosFile* _file = __getObject(pName,pParentLock,pError,_parent);

	if ( pError )
	{
		if ( pError != ERROR_OBJECT_NOT_FOUND )
		{
			return NULL; // serious error
		}
		pError = 0; // reset error
	}

	if ( _file ) // already existing?
	{
		pError = ERROR_OBJECT_EXISTS;
		return NULL;
	}

	stringc _name;
	__filePart(pName,_name);

	_file = new cDosFile();

	_file->_title = _name;

	if ( _parent )
	{
		_file->_parentId = _parent->_id;
	}

	if ( !pError )
	{
		__lockObject(_file,ACCESS_WRITE); // lock
		_fileList.push_back(_file);
		return _file;
	}

	// if failed
	delete _file;
	return NULL;
}

void cDosHandler::__filePart(const stringc& pFullPath, stringc& pFilePart)
{
	// keep only the file
	int _ofs = 0;
	for(_ofs=int(pFullPath.size()-1);_ofs>=0;_ofs--)
	{
		if ( pFullPath[_ofs] == '/' || pFullPath[_ofs] == ':' )
		{
			_ofs++;
			break;
		}
	}
	if ( _ofs < 0 )
	{
		_ofs = 0;
	}
	pFilePart = (pFullPath.c_str()+_ofs);
}

void cDosHandler::__examineObject(cDosFile* _file, struct FileInfoBlock* _fib)
{

#ifdef PLATFORM_AMIGAOS4
# define fET fib_Obsolete
#else
# define fET fib_EntryType
#endif

	if ( _file == &_root )
	{
		_fib->fET = _fib->fib_DirEntryType = ST_ROOT;
	}
	else
	{
		if ( _file->_folder )
		{
			_fib->fET = _fib->fib_DirEntryType = ST_USERDIR;
		}
		else
		{
			_fib->fET = _fib->fib_DirEntryType = ST_FILE;
		}
	}

	strcpy((char*)_fib->fib_FileName+1,_file->_title.c_str());
	*_fib->fib_FileName = (char)_file->_title.size();

	_fib->fib_Comment[0] = 0;

	_fib->fib_Protection = 0;

	_fib->fib_NumBlocks = 1;

	if ( !_file->_fileSize && _file->_exportURLs.size() )
	{
		_fib->fib_Size = 0x08000000; // ~130M
	}
	else
	{
		_fib->fib_Size = _file->_fileSize;
	}

	__makeDateStamp(_file->_modDate,&_fib->fib_Date);
}

void cDosHandler::__incLockCount()
{
	_lockCount++;
	//__log("++[global_lock_count:%d]",_lockCount);
}

void cDosHandler::__decLockCount()
{
	_lockCount--;
	//__log("--[global_lock_count:%d]",_lockCount);
}

bool cDosHandler::__isShutdownPossible()
{
#ifdef PLATFORM_AMIGAOS4
	return true;
#else
	return (_lockCount == 0);
#endif
}

bool cDosHandler::__shutdown()
{
#ifdef PLATFORM_AMIGAOS4
	struct MsgPort *fs_mp, *lh_mp;
	struct Message *msg;

	fs_mp = gDevNode->dn_Port;
	lh_mp = FindPort("dos_lock_handler_port");
	gDevNode->dn_Port = lh_mp;

	for(u32 _i=0;_i<_fileList.size();_i++)
	{
		for(u32 _l=0;_l<_fileList[_i]->_locks.size();_l++)
		{
			__log("cDosHandler::__shutdown() LOCK '%s'",_fileList[_i]->_title.c_str());
			DoPkt2(lh_mp, ACTION_COLLECT, ID_COLLECT_LOCK, (uint32) _fileList[_i]->_locks[_l] );
		}
		for(u32 _o=0;_o<_fileList[_i]->_opens.size();_o++)
		{
			__log("cDosHandler::__shutdown() HANDLE '%s'",_fileList[_i]->_title.c_str());
			DoPkt2(lh_mp, ACTION_COLLECT, ID_COLLECT_FILEHANDLE, (uint32) _fileList[_i]->_opens[_o] );
		}
	}

	while ( (msg = GetMsg(fs_mp)) )
	{
		PutMsg(lh_mp, msg);
	}
#endif
	return true;
}

//
// actions
//

void cDosHandler::__lock(sTask& pTask)
{
	FileLock* _parentLock = (FileLock*) pTask._int1;
	cDosFile* _parentFile = (_parentLock ? (cDosFile*)_parentLock->fl_Key : NULL);
	int _access = pTask._int2;
	stringc _name = pTask._str;

	_name.make_lower();

	//bool _exclusive = (_access==EXCLUSIVE_LOCK);
	//__log("cGoogleDriveHandler::lock() name:'%s' parent_lock:%08x exclusive:%s",_name.c_str(),_parentLock,_exclusive?"yes":"no");

	cDosFile* _file = NULL;

	if ( (!_name.size()&&!_parentFile) || _name.lastChar() == ':' ) // root?
	{
		_file = &_root;

		if ( !__checkFolder(_file) ) // sync
		{
			pTask._error = ERROR_DISK_NOT_VALIDATED; // network error
			return;
		}
	}
	else
	{
		_file = __getObject(_name,_parentFile,pTask._error); // find object by name, relatively from the parent lock
	}

	if ( _file )
	{
		pTask._result = (int) __lockObject(_file,_access);
		if ( !pTask._result )
		{
			pTask._error = ERROR_OBJECT_IN_USE;
		}
	}
}

void cDosHandler::__unlock(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int1);
#endif

	if ( pTask._int1 )
	{
		FileLock* _lock = (FileLock*) pTask._int1;
		cDosFile* _file = (_lock ? (cDosFile*)_lock->fl_Key : NULL);
		if ( _file && _file->unlock(_lock) )
		{
			__decLockCount();
		}
	}
}

void cDosHandler::__duplock(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int1);
#endif

	if ( pTask._int1 )
	{
		FileLock* _lock = ((FileLock*)pTask._int1);
		cDosFile* _file = (_lock ? (cDosFile*)_lock->fl_Key : NULL);
		if ( _file )
		{
			pTask._result = (int) __lockObject(_file,ACCESS_READ);
			if ( !pTask._result )
			{
				pTask._error = ERROR_OBJECT_IN_USE;
			}
		}
	}
}

void cDosHandler::__parent(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int1);
#endif

	pTask._result = pTask._error = 0;
	FileLock* _lock = ((FileLock*)pTask._int1);
	if ( _lock && _lock->fl_Key && _lock->fl_Key != int(&_root) ) // not the root?
	{
		cDosFile* _file = (_lock ? (cDosFile*)_lock->fl_Key : NULL);
		//cDosFile* _lock = (cDosFile*) pTask._int1;
		cDosFile* _parent = NULL;

		if ( _file->_parentId == _root._id )
		{
			_parent = &_root;
		}
		else
		{
			for(u32 _f=0;_f<_fileList.size();_f++)
			{
				if ( _fileList[_f]->_id == _file->_parentId )
				{
					_parent = _fileList[_f];
					break;
				}
			}
		}

		if ( _parent )
		{
			pTask._result = (int) __lockObject(_parent,ACCESS_READ);
			if ( !pTask._result )
			{
				pTask._error = ERROR_OBJECT_IN_USE;
			}
		}
		else
		{
			pTask._error = ERROR_OBJECT_NOT_FOUND;
		}
	}
}

void cDosHandler::__examine(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int1);
#endif

	FileInfoBlock* _fib = (FileInfoBlock*) pTask._int2;
	cDosFile* _file = NULL;
	int _next = 0;

	if ( pTask._int1 ) // has parent lock?
	{
		FileLock* _plock = ((FileLock*)pTask._int1);
		cDosFile* _pfile = (_plock ? (cDosFile*)_plock->fl_Key : NULL);
		_file = __getObject(pTask._str,_pfile,pTask._error);
		if ( _file )
		{
			_next = (int) (_file->_folder ? __getFirstChild(_file) : __getNextObjectWithSameParent(_file));
		}
	}
	else
	{
		_file = (cDosFile*) _fib->fib_DiskKey;
		if ( _file )
		{
			_next = (int) __getNextObjectWithSameParent(_file);
		}
	}

	if ( _file  )
	{
		__examineObject(_file,_fib);

		_fib->fib_DiskKey = _next;

		pTask._result = DOSTRUE;
	}
	else
	{
		pTask._result = DOSFALSE;
		pTask._error = ERROR_NO_MORE_ENTRIES;
	}
}

void cDosHandler::__info(sTask& pTask)
{
	//cDosFile* _lock = (cDosFile*) pTask._int1; // unused
	InfoData* info_data = (InfoData*) pTask._int2;

	info_data->id_NumSoftErrors = 0;
	info_data->id_UnitNumber = 0;
	info_data->id_DiskState = ( (_licenceMailAddress==_accountMailAddress) ? ID_VALIDATED : ID_WRITE_PROTECTED );
	info_data->id_NumBlocks = int(_quotaLimit/_blockSize);
	info_data->id_NumBlocksUsed = int(_quotaUsed/_blockSize);
	info_data->id_BytesPerBlock = _blockSize;
	info_data->id_DiskType = ID_DOS_DISK;
	info_data->id_InUse = (_lockCount>1) ? DOSTRUE : DOSFALSE; 
}

void cDosHandler::__samelock(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int1);
	__CheckLock((cDosFile*)pTask._int2);
#endif

	if ( pTask._int1 && pTask._int2 )
	{
		FileLock* _lock1 = ((FileLock*)pTask._int1);
		FileLock* _lock2 = ((FileLock*)pTask._int2);
		pTask._result = (_lock1->fl_Key==_lock2->fl_Key) ? DOSTRUE : DOSFALSE;
	}
}

void cDosHandler::__open(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int2);
#endif

	// int1 = filehandle out
	// int2 = parent lock
	// int3 = open mode

	// check licence
	if ( _licenceMailAddress != _accountMailAddress && (pTask._int3 == MODE_NEWFILE || pTask._int3 == MODE_READWRITE) )
	{
		messageBox(NO_LICENCE_MSG);
		pTask._error = ERROR_WRITE_PROTECTED;
		pTask._result = DOSFALSE;
		return;
	}
	//

	cFileOpen::eOpenMode om = cFileOpen::omCOUNT;

	stringc _name = pTask._str;

	FileLock* _plock = ((FileLock*)pTask._int2);
	cDosFile* _pfile = (_plock ? (cDosFile*)_plock->fl_Key : NULL);
	cDosFile* _file = __getObject(pTask._str,_pfile,pTask._error);
	FileLock* _lock = NULL;

	// filtering forbidden cases
	if ( _file )
	{
		if ( _file->_folder )
		{
			pTask._error = ERROR_OBJECT_WRONG_TYPE;
		}
		else if ( pTask._int3 == MODE_NEWFILE || pTask._int3 == MODE_READWRITE ) // no write allowed for existing files
		{
			pTask._error = ERROR_WRITE_PROTECTED;
		}
		else if ( pTask._int3 == MODE_OLDFILE )
		{
			if ( !_file->_fileSize && !_file->_exportURLs.size() ) // unaccessible zero length file
			{
				pTask._error = ERROR_OBJECT_WRONG_TYPE;
			}
			else if ( _file->_opens.size() && _file->_opens[0]->_openMode != cFileOpen::omDownload ) // one export/upload allowed
			{
				pTask._error = ERROR_OBJECT_IN_USE;
			}
		}
	}
	else // want new file?
	{
		pTask._error = 0;
		// only MODE_NEWFILE allowed here
		if ( pTask._int3 == MODE_OLDFILE )
		{
			pTask._error = ERROR_OBJECT_NOT_FOUND;
		}
		else if ( pTask._int3 == MODE_READWRITE )
		{
			pTask._error = ERROR_WRITE_PROTECTED;
		}
	}

	if ( !pTask._error )
	{
		if ( pTask._int3 == MODE_NEWFILE ) // create new file item
		{
			_file = __createObject(pTask._str,_pfile,pTask._error);
			_lock = _file->_locks[0];
			// .. props will be updated after upload
			om = cFileOpen::omUpload;
		} 
		else
		{
			// open old file
			_lock = __lockObject(_file,ACCESS_READ); // lock
			if ( !_lock )
			{
				pTask._error = ERROR_OBJECT_IN_USE;
			}
			if ( _file->_fileSize )
			{
				om = cFileOpen::omDownload;
			}
			else
			{
				om = cFileOpen::omExport;
			}
		}
	}

	if ( !pTask._error )
	{
		cFileOpen* fo = new cFileOpen(om,_lock);
		_file->_opens.push_back(fo);
		//__log("cGoogleDriveHandler::__open() fileopen=%08x file=%08x",_open,_file);
		((FileHandle*)(pTask._int1))->fh_Arg1 = (int)fo;
	}

	pTask._result = (pTask._error ? DOSFALSE : DOSTRUE);
}

void cDosHandler::__close(sTask& pTask)
{
	cFileOpen* _handle = (cFileOpen*) pTask._int1;
	cDosFile* _file = (cDosFile*) _handle->_lock->fl_Key;

	if ( _handle->_openMode == cFileOpen::omUpload )
	{
		if ( (_handle->_cachedFileSize && _handle->_cache.size()) || _appendUploadSupported )
		{
			_handle->_openCursor = 0; // seek to begin
			if ( uploadFile(_file,_handle,true) ) // commit data & set final file length
			{
				_handle->_openCursor = 0; // seek to begin. this is obsolete, we will close this file
			}
		}
	}

	pTask._int1 = (int)_handle->_lock;
	__unlock(pTask);

	_file->close(_handle);

	pTask._result = DOSTRUE;
}

void cDosHandler::__delete(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int1);
#endif

	// check licence
	if ( _licenceMailAddress != _accountMailAddress )
	{
		messageBox(NO_LICENCE_MSG);
		pTask._error = ERROR_WRITE_PROTECTED;
		pTask._result = DOSFALSE;
		return;
	}
	//

	FileLock* _lock = (FileLock*) pTask._int1;
	cDosFile* _pfile = (cDosFile*) _lock->fl_Key;

	cDosFile* _file = __getObject(pTask._str,_pfile,pTask._error);
	if ( _file )
	{
		if ( __deleteObject(_file,pTask._error) )
		{
			pTask._result = DOSTRUE;

		}
		else
		{
			pTask._result = DOSFALSE;
		}
	}
	else
	{
		pTask._result = DOSFALSE;
		pTask._error = ERROR_OBJECT_NOT_FOUND;
	}
}

void cDosHandler::__seek(sTask& pTask)
{
	cFileOpen* _handle = (cFileOpen*) pTask._int1;
	cDosFile* _file = (cDosFile*) _handle->_lock->fl_Key;
	int _offset = pTask._int2;
	int _mode = pTask._int3;

	if ( !_file->_fileSize && !_handle->_cachedFileSize ) // empty file
	{
		pTask._result = -1;
		pTask._error = ERROR_SEEK_ERROR;
	}
	else
	{
		if ( _handle->_openMode == cFileOpen::omUpload ) // opened for upload?
		{
			pTask._result = _handle->_cachedFileSize;
			return; // no seek possible
		}

		int _size = 0;
		if ( _file->_fileSize )
		{
			_size = _file->_fileSize;
		}
		else if ( _handle->_cachedFileSize )
		{
			_size = _handle->_cachedFileSize;
		}

		pTask._result = _handle->_openCursor;

		switch ( _mode )
		{
		case OFFSET_BEGINNING:
			_handle->_openCursor = _offset;
			break;
		case OFFSET_CURRENT:
			_handle->_openCursor += _offset;
			break;
		case OFFSET_END:
			_handle->_openCursor = _size + _offset;
			break;
		}

		if ( _handle->_openCursor < 0 )
		{
			_handle->_openCursor = 0;
		}
		else if ( _handle->_openCursor > _size )
		{
			_handle->_openCursor = _size;
		}
	}
}

void cDosHandler::__read(sTask& pTask)
{
	cFileOpen* _handle = (cFileOpen*) pTask._int1;
	cDosFile* _file = (cDosFile*) _handle->_lock->fl_Key;
	u8* _buffer = (u8*) pTask._int2;
	int _len = pTask._int3;
	int _readLen = 0;

	if ( _handle->_openMode == cFileOpen::omUpload ) // reading not allowed during upload
	{
		return;
	}

	if ( _file->_fileSize ) // ismerjük a file méretet?
	{
		if ( _handle->_openCursor + _len <= _file->_fileSize )
		{
			_readLen = _len;
		}
		else if ( _handle->_openCursor < _file->_fileSize )
		{
			_readLen = _file->_fileSize - _handle->_openCursor;
		}

		if ( _readLen )
		{
			/*
			if ( _readLen > gNetworkIOBufferLength ) // maximize
			{
				_readLen = gNetworkIOBufferLength;
			}
			*/

			// do read from cursor to cursor+readlen

			if ( downloadFile(_file->_downloadURL, _handle->_openCursor, _readLen, _buffer) )
			{
				_handle->_openCursor += _readLen;
				pTask._result = _readLen;
			}
			else
			{
				pTask._result = -1; // read error
			}
		}
	}
	else // file méret ismeretlen, le stream-eljük a cache-be
	{
		if ( !_handle->_cachedFileSize )
		{
#ifdef PLATFORM_AMIGA
			// avoid unwanted peeks
			if ( !_handle->_openCursor )
			{
				stringc callee(((struct Task*)pTask._task)->tc_Node.ln_Name);
				__log("callee: %s", callee.c_str() );
				if ( _len == 480 && callee == "Workbench" ) // wb peek (OS3.1)
				{
					pTask._result = _len;
					return;
				}
				else if ( (_len == 4 || _len == 512) && callee == "dopus_function" ) // opus peek (OS3.9/AROS)
				{
					pTask._result = _len;
					return;
				}
				else if ( _len == _blockSize && callee.find("Ambient Thread") == 0 ) // ambient peek (MOS)
				{
					pTask._result = _len;
					return;
				}
			}
#endif

			// generate requester
			int _exportIndex = 0;
			if ( _file->_exportURLs.size() > 1 )
			{
				stringc _key = "exportFormat=";
				stringc _choices;
				for(u32 _i=0;_i<_file->_exportURLs.size();_i++)
				{
					if ( _i )
					{
						_choices += "|";
					}
					int _ofs = _file->_exportURLs[_i].find(_key.c_str());
					if ( _ofs != -1 )
					{
						_ofs += _key.size();
						_choices += _file->_exportURLs[_i].c_str()+_ofs;
					}
				}

#ifdef PLATFORM_AMIGA
				struct EasyStruct myES =
				{
					sizeof(struct EasyStruct),
					0,
					(EASY_TEXT)"Export file",
					(EASY_TEXT)"Please choose the desired export format",
					(EASY_TEXT)_choices.c_str(),
				};

#ifdef PLATFORM_AMIGAOS4
				IIntuition = (struct IntuitionIFace*) GetInterface(IntuitionBase, "main", 1, NULL);
#endif

				int answer = EasyRequestArgs(NULL, &myES, NULL, NULL );
				if ( !answer )
				{
					_exportIndex = (_file->_exportURLs.size()-1);
				}
				else
				{
					_exportIndex = answer-1;
				}
#endif
			}

			// check licence
			if ( _licenceMailAddress != _accountMailAddress )
			{
				messageBox(NO_LICENCE_MSG);
				pTask._error = ERROR_WRITE_PROTECTED;
				pTask._result = DOSFALSE;
				return;
			}
			//

			if ( !exportFile(_file->_exportURLs[_exportIndex],_file,_handle) )
			{
				pTask._result = -1; // read error
			}
			else
			{
				_handle->_openCursor = 0;
			}
		}
		if ( pTask._result != -1 )
		{
			if ( _handle->_openCursor + _len <= _handle->_cachedFileSize )
			{
				_readLen = _len;
			}
			else if ( _handle->_openCursor < _handle->_cachedFileSize )
			{
				_readLen = _handle->_cachedFileSize - _handle->_openCursor;
			}
			else
			{
				_readLen = 0; // EOF
			}
			if ( _readLen )
			{
				_readLen = _handle->readData(_buffer,_readLen);
			}
			pTask._result = _readLen;
		}
	}
}

void cDosHandler::__write(sTask& pTask) // upload file
{
	// check licence
	if ( _licenceMailAddress != _accountMailAddress )
	{
		messageBox(NO_LICENCE_MSG);
		pTask._error = ERROR_WRITE_PROTECTED;
		pTask._result = DOSFALSE;
		return;
	}
	//

	cFileOpen* _handle = (cFileOpen*) pTask._int1;
	cDosFile* _file = (cDosFile*) _handle->_lock->fl_Key;
	u8* _buffer = (u8*) pTask._int2;
	int _len = pTask._int3;

	if ( !_file->_id.size() )
	{
		if ( !beginUpload(_file) ) // initiate upload
		{
			pTask._error = ERROR_DISK_FULL;
		}
	}

	if ( !pTask._error && _file->_id.size() )
	{
		if ( !_handle->storeData(_buffer,_len) )
		{
			pTask._error = ERROR_DISK_FULL;
		}
		else
		{
			_handle->_cachedFileSize += _len;
			if ( _appendUploadSupported && _handle->_cachedFileSize >= (1024*1024) ) // after 1MB of data cached, upload
			{ // upload a chunk
				_handle->_openCursor = 0; // seek to begin
				if ( !uploadFile(_file,_handle,false) )
				{
					pTask._error = ERROR_DISK_FULL;
				}
				_handle->purgeCache();
			}
		}
	}

	// finished
	pTask._result = 0;
	if ( !pTask._error )
	{
		pTask._result = pTask._int3;
	}
}

void cDosHandler::__createdir(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int1);
#endif

	// check licence
	if ( _licenceMailAddress != _accountMailAddress )
	{
		messageBox(NO_LICENCE_MSG);
		pTask._error = ERROR_WRITE_PROTECTED;
		pTask._result = DOSFALSE;
		return;
	}
	//

	FileLock* _plock = (FileLock*) pTask._int1;
	cDosFile* _pfile = (_plock ? (cDosFile*)_plock->fl_Key : NULL);;

	cDosFile* _file = __createObject(pTask._str,_pfile,pTask._error);
	if ( !pTask._error && _file )
	{
		_file->_folder = true;
		if ( createDirectory(_file) ) // upload directory
		{
			pTask._result = int(_file->_locks[0]);
		}
		else
		{
			pTask._error = ERROR_DISK_NOT_VALIDATED;
		}
	}
}
void cDosHandler::__rename(sTask& pTask)
{
#ifdef CHECK_LOCK
	__CheckLock((cDosFile*)pTask._int1);
	__CheckLock((cDosFile*)pTask._int3);
#endif

	// check licence
	if ( _licenceMailAddress != _accountMailAddress )
	{
		messageBox(NO_LICENCE_MSG);
		pTask._error = ERROR_WRITE_PROTECTED;
		pTask._result = DOSFALSE;
		return;
	}
	//

	stringc _nameFrom = pTask._str;
	stringc _nameTo = (const char*) pTask._int2;

	FileLock* _lockFrom = (FileLock*) pTask._int1;
	FileLock* _lockTo = (FileLock*) pTask._int3;

	cDosFile* _fileFrom = (_lockFrom ? (cDosFile*)_lockFrom->fl_Key : NULL);;
	cDosFile* _fileTo = (_lockTo ? (cDosFile*)_lockTo->fl_Key : NULL);;

	cDosFile* _file1 = __getObject(_nameFrom,_fileFrom,pTask._error);

	if ( !pTask._error ) // _file1 found?
	{
		cDosFile* _realParent = NULL;
		cDosFile* _file2 = __getObject(_nameTo,_fileTo,pTask._error,_realParent);
		if ( _file2 && _file1 != _file2 ) // already exist?
		{
			pTask._error = ERROR_OBJECT_EXISTS;
		}
		else if ( !_realParent )
		{
			pTask._error = ERROR_OBJECT_NOT_FOUND;
		}
		else // let's go
		{
			pTask._error = 0;
			__filePart(_nameTo,_file1->_title); // rename
			//__convertToUTF8(_nameFrom,_file1->_title);
			_file1->_parentId = _realParent->_id; // reparent
			if ( !updateFile(_file1,true,false,false) )
			{
				pTask._error = ERROR_DISK_NOT_VALIDATED;
			}
		}
	}

	pTask._result = (pTask._error ? DOSFALSE : DOSTRUE);
}

void cDosHandler::__duplockFromFH(sTask& pTask)
{
	cFileOpen* _handle = (cFileOpen*) pTask._int1;
	cDosFile* _file = (cDosFile*) _handle->_lock->fl_Key;
	pTask._result = (int) __lockObject(_file,ACCESS_READ);
	if ( !pTask._result )
	{
		pTask._error = ERROR_OBJECT_IN_USE;
	}
}

void cDosHandler::__openFromLock(sTask& pTask)
{
	FileLock* _lock = ((FileLock*)pTask._int2);
	cDosFile* _file = (_lock ? (cDosFile*)_lock->fl_Key : NULL);

	if ( _file->_folder )
	{
		pTask._error = ERROR_OBJECT_WRONG_TYPE;
	}
	else if ( !_file->_fileSize && !_file->_exportURLs.size() ) // unaccessible zero length file
	{
		pTask._error = ERROR_OBJECT_WRONG_TYPE;
	}
	else if ( _file->_opens.size() && _file->_opens[0]->_openMode != cFileOpen::omDownload ) // one export/upload allowed
	{
		pTask._error = ERROR_OBJECT_IN_USE;
	}

	if ( !pTask._error )
	{
		cFileOpen* fo = new cFileOpen(cFileOpen::omDownload,_lock);
		_file->_opens.push_back(fo);
		((FileHandle*)(pTask._int1))->fh_Arg1 = (int)fo;
	}

	pTask._result = (pTask._error ? DOSFALSE : DOSTRUE);
}

void cDosHandler::__parentFH(sTask& pTask)
{
	cFileOpen* _handle = (cFileOpen*) pTask._int1;
	cDosFile* _file = (cDosFile*) _handle->_lock->fl_Key;
	if ( _file != &_root )
	{
		for(u32 _i=0;_i<_fileList.size();_i++)
		{
			if ( _fileList[_i]->_id == _file->_parentId )
			{
				_file = _fileList[_i];
				break;
			}
		}
	}

	pTask._result = (int) __lockObject(_file,ACCESS_READ);
	if ( !pTask._result )
	{
		pTask._error = ERROR_OBJECT_IN_USE;
	}
}

void cDosHandler::__examineFH(sTask& pTask)
{
	cFileOpen* _handle = (cFileOpen*) pTask._int1;
	cDosFile* _file = (cDosFile*) _handle->_lock->fl_Key;
	FileInfoBlock* _fib = (FileInfoBlock*) pTask._int2;
	__examineObject(_file,_fib);
	pTask._result = DOSTRUE;
}

void cDosHandler::__exAll(sTask& pTask)
{
	// todo
}

void cDosHandler::__exAllEnd(sTask& pTask)
{
	// todo
}

/*
void cGoogleDriveHandler::__changeMode(sTask& pTask)
{
}
*/

//

#ifdef PLATFORM_WINDOWS

// ...

#else

// dos handler code

#ifdef PLATFORM_AMIGAOS3
 struct MathIeeeDoubTransBase* __MathIeeeDoubTransBase = NULL; 
 struct IntuitionBase *IntuitionBase = NULL; 
 extern int segmentStartAddress;
 extern int debugData1;
 extern int debugData2;
#elif defined PLATFORM_MORPHOS

#elif defined PLATFORM_AROS

struct IntuitionBase *IntuitionBase = NULL; 

/*
#define DEBUG 1

#include <aros/config.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/asmcall.h>
#include <aros/debug.h>
#include <aros/symbolsets.h>
#include <aros/startup.h>
*/

#elif defined PLATFORM_AMIGAOS4

extern struct ExecIFace *IExec;
struct Library *IntuitionBase = NULL; 
struct IntuitionIFace *IIntuition = NULL;
struct Library *DOSBase = NULL; 
struct DOSIFace *IDOS = NULL;

#endif

#ifndef PLATFORM_AMIGAOS3
int segmentStartAddress = 0;
int debugData1 = 0xdead0000;
int debugData2 = 0xdead0000;
extern "C"
{
	extern void __initclib(void);
}
#endif

// task interface

sTask gTask;
u32 gWorkerSignalMask = 0;
u32 gHandlerSignalMask = 0;
bool gWorkerIsRunning = false;
bool gHandlerIsRunning = false;
struct Task* gWorkerTask = NULL;
struct Task *gHandlerTask = NULL;
extern int gConsoleWindow;
extern int gLogFile;
extern int gSerialOutput;

//
// dos entry
//

int StrLen(const TEXT *s)
{
	const TEXT *p;

	for(p = s; *p != '\0'; p++);
	return p - s;
} 

// my rename dos entry
#ifdef __AROS__

#ifndef UPINT
#ifdef __AROS__
typedef IPTR UPINT;
typedef SIPTR PINT;
#endif                         
#endif 

BOOL MyRenameDosEntry(struct DosList *entry, const TEXT *name)
{
	LONG error = 0;
	UPINT length;
	TEXT *name_copy, *old_name;
	name_copy = NULL;
	if(name != NULL)
	{
		length = StrLen(name);
#ifdef AROS_FAST_BPTR
		name_copy = (TEXT*) AllocVec(length + 1, MEMF_PUBLIC);
		if(name_copy != NULL)
		{
			CopyMem(name, name_copy, length + 1);
		}
#else
		name_copy = AllocVec(length + 2, MEMF_PUBLIC);
		if(name_copy != NULL)
		{
			name_copy[0] = (UBYTE)(length > 255 ? 255 : length); 
			CopyMem(name, name_copy + 1, length + 1);
		}
#endif
		else
			error = IoErr();
	}
	if(error == 0)
	{
		old_name = (TEXT*) BADDR(entry->dol_Name);
		FreeVec(old_name);
		entry->dol_Name = (char*) MKBADDR(name_copy);
	}
	return error == 0;
}
#else
BOOL MyRenameDosEntry(struct DosList *entry, const TEXT *name)
{
	LONG error = 0;
	int length;
	TEXT *name_copy, *old_name;
	name_copy = NULL;
	if ( name != NULL )
	{
		length = StrLen(name);
		name_copy = (TEXT *) AllocVec(length + 2, MEMF_PUBLIC);
		if ( name_copy != NULL )
		{
			CopyMem((void*)name, name_copy + 1, length + 1);
			*name_copy = length;
		}
		else
			error = IoErr();
	}
	if ( error == 0 )
	{
		old_name = (TEXT*) BADDR(entry->dol_Name);
		if(old_name != NULL)
			FreeVec(old_name);
		entry->dol_Name = MKBADDR(name_copy);
	}
	return error == 0;
}
#endif 
////////////

VOID MyFreeDosEntry(struct DosList *entry)
{
	if ( entry != NULL )
	{
		MyRenameDosEntry(entry, NULL);
		FreeMem(entry, sizeof(struct DosList));
	}
	return;
}

struct DosList *MyMakeDosEntry(const TEXT *name, LONG type)
{
	struct DosList *entry;
	LONG error = 0;
	entry = (struct DosList*) AllocMem(sizeof(struct DosList), MEMF_CLEAR | MEMF_PUBLIC);
	if ( entry != NULL )
	{
		if(!MyRenameDosEntry(entry, name))
		{
			error = IoErr();
		}
		entry->dol_Type = type;
	}
	else
	{
		error = IoErr();
	}
	if (error != 0 )
	{
		MyFreeDosEntry(entry);
		entry = NULL;
	}
	return entry;
}

bool ReallyRemoveDosEntry(struct DosList * entry)
{
	struct Message * mn;
	struct MsgPort * port;
	struct DosList * dl;
	BOOL result = FALSE;
	LONG kind,i;

	if(entry->dol_Type == DLT_DEVICE)
		kind = LDF_DEVICES;
	else
		kind = LDF_VOLUMES;

	port = entry->dol_Task;

	for(i = 0 ; i < 100 ; i++)
	{
		//kprintf("AttemptLockDosList()\n");
		dl = AttemptLockDosList(LDF_WRITE|kind);
		if(((ULONG)dl) <= 1)
			dl = NULL;

		if(dl != NULL)
		{
			//kprintf("RemDosEntry()\n");
			RemDosEntry(entry);

			UnLockDosList(LDF_WRITE|kind);

			result = TRUE;

			break;
		}

		while((mn = GetMsg(port)) != NULL)
			ReplyPkt((struct DosPacket *)mn->mn_Node.ln_Name,DOSFALSE,ERROR_ACTION_NOT_KNOWN);

		Delay(TICKS_PER_SECOND / 10);
	}

	return(result);
}

//
// helper functions
//

#ifdef PLATFORM_AMIGAOS3
static const ULONG tricky=0x16c04e75; // move.b d0,(a3)+ , rts
void __sprintf(STRPTR buffer, STRPTR fmt, ...)
{
	STRPTR *arg = &fmt+1;
	RawDoFmt(fmt,arg,(void (*)())&tricky,buffer);
}
#else
# define __sprintf sprintf
#endif

void getString(const char* pbstr, char* pout) // BSTR to null temrinated
{
#ifdef PLATFORM_AROS
	strcpy(pout,pbstr);
#else
	int len = *pbstr;
	pbstr++;
	for(u32 _i=0;_i<len;_i++)
	{
		*pout = *pbstr;
		pout++;
		pbstr++;
	}
	*pout = 0; // close
#endif
}

void returnPacket(struct DosPacket *packet, struct Process *p, long res1, long res2)
{
	struct Message *msg;
	struct MsgPort *replyport;
	packet->dp_Res1 = res1;
	packet->dp_Res2 = res2;
	replyport = packet->dp_Port;
	msg = packet->dp_Link;
	packet->dp_Port = &p->pr_MsgPort;
	msg->mn_Node.ln_Name = (char *)packet;
	msg->mn_Node.ln_Succ = NULL;
	msg->mn_Node.ln_Pred = NULL;
	PutMsg(replyport,msg);
}

struct DosPacket *getPacket(struct Process *p)
{
	struct MsgPort *port;
	struct Message *msg;
	port = &p->pr_MsgPort;
	WaitPort(port);
	msg = GetMsg(port);
	return ((struct DosPacket *) msg->mn_Node.ln_Name);
} 

void doTask(sTask::eTaskType ptt, const char* paramStr, int paramInt1=0, int paramInt2=0, int paramInt3=0)
{
	gTask.set(ptt,paramStr,paramInt1,paramInt2,paramInt3);
	Signal(gWorkerTask,gWorkerSignalMask); // launch task
	Wait(gHandlerSignalMask); // wait for finish
}

//
// worker process
//

void taskFunc(void)
{
	// init std lib
#ifdef PLATFORM_AMIGAOS3
	/*
	__asm__ __volatile__ (
		"movem.l d0-d7/a0-a6,-(sp) \n"
		"lea ___INIT_LIST__+4,a2 \n"
		"moveql	#-1,d2 \n"
		"jsr	_callfuncs \n"
		"movem.l (sp)+,d0-d7/a0-a6 \n"
		);
		*/
#else
	__initclib();
#endif

#ifdef PLATFORM_AMIGAOS4
	IntuitionBase = OpenLibrary("intuition.library",50);
	IIntuition = (struct IntuitionIFace*) GetInterface(IntuitionBase, "main", 1, NULL);
	//__log("IntuitionBase:%08x IIntuition:%08x",IntuitionBase,IIntuition);
	//__log("DOSBase:%08x IDOS:%08x",DOSBase,IDOS);
#else
	IntuitionBase = (struct IntuitionBase * ) OpenLibrary("intuition.library",37);
#endif

	gWorkerTask = FindTask(NULL);
	u8 mySignal = AllocSignal(-1);
	gWorkerSignalMask = (1<<mySignal);

	ULONG stacksize = (char*)gWorkerTask->tc_SPUpper-(char*)gWorkerTask->tc_SPLower; 
	__log("worker task_id:%08x stack_size:%d signal:%08x",gWorkerTask,stacksize,gWorkerSignalMask);
	__log(" debugdata1_adr:%08lx debugdata2_adr:%08lx", &debugData1,&debugData2);

#ifndef OFFLINE_SIMULATION
	curl_global_init(CURL_GLOBAL_DEFAULT);
	__log("curl inited (%s)",curl_version());
#endif

	cDosHandler *gdh = gDosHandlerFactory();
	gdh->init();

	//__log(" _sysLock offset:%d", int( (char*)(&gdh->_root._sysLock) - (char*)(&gdh->_root) ) );

	__log("reading codepages...");
	gdh->__readCodePage("windows-1250");
	gdh->__selectCodePage("windows-1250");

	gdh->auth();

	if ( gdh->isAuthed() )
	{
		__log("token expires in %d secs",gdh->_expiresIn);
		__log("getting drive info...");
		if ( gdh->getAccountInfo() )
		{
			__log("user_email:%s quota_limit: %dMB quota_used: %dMB",
				gdh->_accountMailAddress.c_str(), int(gdh->_quotaLimit/1048576), int(gdh->_quotaUsed/1048576) );

			__log("init was successful. root_object:%08x",&gdh->_root);

			gWorkerIsRunning = true;
			bool gWorkerShuttingDown = false;

			while ( !gWorkerShuttingDown )
			{
				Wait(gWorkerSignalMask);

				// do task
				switch ( gTask._type )
				{
				case sTask::ttLog:
					{
						__log(gTask._str);
					}
					break;
				case sTask::ttInhibit:
					if ( !gdh->_lockCount )
					{
						gTask._result = DOSTRUE;
					}
					else
					{
						gTask._result = DOSFALSE;
					}
					break;
				case sTask::ttDie:
					if ( gdh->__isShutdownPossible() )
					{
						gdh->__shutdown();
						gWorkerShuttingDown = true;
						gTask._result = DOSTRUE;
					}
					else
					{
						gTask._result = ERROR_OBJECT_IN_USE;
					}
					break;
				case sTask::ttLocateObject:
					{
						if ( gTask._str[0] == '@' )
						{
							gdh->onHandlerCommand(gTask._str);
						}

						gdh->__lock(gTask);
						if ( gTask._result )
						{
							__log("[done] lock:%08x file:%08x lock_count:%d [total_lock_count:%d]",
								gTask._result,
								((FileLock*)gTask._result)->fl_Key,
								((cDosFile*)((FileLock*)gTask._result)->fl_Key)->_locks.size(),
								gdh->_lockCount );
						}
						else
						{
							__log("[fail] error_code:%d", gTask._error );
						}
					}
					break;
				case sTask::ttFreeLock:
					gdh->__unlock(gTask);
					__log("[done] [total_lock_count:%d]", gdh->_lockCount );
					break;
				case sTask::ttExamineObject:
					{
						gdh->__examine(gTask);
						if ( gTask._result == DOSTRUE )
						{
							__log("[done] name:'%s' next:%08x",
								((FileInfoBlock*)(gTask._int2))->fib_FileName+1,
								((FileInfoBlock*)(gTask._int2))->fib_DiskKey);
						}
						else
						{
							__log("[fail] error_code:%d",gTask._error );
						}
					}
					break;
				case sTask::ttCopyDir:
					{
						gdh->__duplock(gTask);
						if ( !gTask._error )
						{
							__log("[done] lock:%08x file:%08x [total_lock_count:%d]",
								gTask._result,
								((FileLock*)gTask._result)->fl_Key,
								gdh->_lockCount);
						}
						else
						{
							__log("[fail] error_code:%d",gTask._error);
						}
					}
					break;
				case sTask::ttParent:
					{
						gdh->__parent(gTask);
						if ( !gTask._error )
						{
							if ( gTask._result )
							{
								__log("[done] lock:%08x file:%08x [total_lock_count:%d]",
									gTask._result,
									((FileLock*)gTask._result)->fl_Key,
									gdh->_lockCount);
							}
							else
							{
								__log("[done] lock:%08x",gTask._result);
							}
						}
						else
						{
							__log("[fail] error_code:%d",gTask._error);
						}
					}
					break;
				case sTask::ttInfo:
					{
						gdh->__info(gTask);
						__log("[done]");
					}
					break;
				case sTask::ttSameLock:
					{
						gdh->__samelock(gTask);
						__log("[done]");
					}
					break;
				case sTask::ttFind:
					{
						gdh->__open(gTask);
						if ( gTask._result == DOSTRUE )
						{
							__log("[done] my_handle_ptr:%08x",((FileHandle*)(gTask._int1))->fh_Arg1);
						}
						else
						{
							__log("[fail] error_code:%d",gTask._error);
						}
					}
					break;
				case sTask::ttSeek:
					{
						gdh->__seek(gTask);
						if ( gTask._result != -1 )
						{
							__log("[done] oldpos:%d", gTask._result );
						}
						else
						{
							__log("[fail] error_code:%d",gTask._error);
						}
					}
					break;
				case sTask::ttRead:
					{
						gdh->__read(gTask);
						if ( gTask._result == -1 )
						{
							__log("[fail] error_code:%d",gTask._error);
						}
						else if ( gTask._result == 0 )
						{
							__log("[eof]");
						}
						else
						{
							__log("[done] readed:%d",gTask._result);
						}
					}
					break;
				case sTask::ttWrite:
					{
						gdh->__write(gTask);
						if ( gTask._result != gTask._int3 )
						{
							__log("[fail] error_code:%d",gTask._error);
						}
						else
						{
							__log("[done] written:%d",gTask._result);
						}
					}
					break;
				case sTask::ttEnd:
					{
						gdh->__close(gTask);
						__log("[done]");
					}
					break;
				case sTask::ttDelete:
					{
						gdh->__delete(gTask);
						if ( gTask._result == DOSTRUE )
						{
							__log("[done]");
						}
						else
						{
							__log("[fail] error_code:%d",gTask._error);
						}
					}
					break;
				case sTask::ttCreateDir:
					{
						gdh->__createdir(gTask);
						if ( gTask._result )
						{
							__log("[done] lock:%08x",gTask._result);
						}
						else
						{
							__log("[fail] error_code:%d",gTask._error);
						}
					}
					break;
				case sTask::ttRename:
					{
						gdh->__rename(gTask);
						if ( gTask._result == DOSTRUE )
						{
							__log("[done]");
						}
						else
						{
							__log("[fail] error_code:%d",gTask._error);
						}
					}
					break;
				case sTask::ttCopyDirFH:
					gdh->__duplockFromFH(gTask);
					if ( !gTask._error )
					{
						__log("[done] lock:%08x file:%08x [total_lock_count:%d]",
							gTask._result, 
							((FileLock*)gTask._result)->fl_Key,
							gdh->_lockCount);
					}
					else
					{
						__log("[fail] error_code:%d",gTask._error);
					}
					break;
				case sTask::ttFHFromLock:
					gdh->__openFromLock(gTask);
					if ( gTask._result == DOSTRUE )
					{
						__log("[done] my_handle_ptr:%08x",((FileHandle*)(gTask._int1))->fh_Arg1);
					}
					else
					{
						__log("[fail] error_code:%d",gTask._error);
					}
					break;
				case sTask::ttParentFH:
					gdh->__parentFH(gTask);
					if ( !gTask._error )
					{
						__log("[done] lock:%08x file:%08x [total_lock_count:%d]",
							gTask._result,
							((FileLock*)gTask._result)->fl_Key,
							gdh->_lockCount);
					}
					else
					{
						__log("[fail] error_code:%d",gTask._error);
					}
					break;
				case sTask::ttExamineFH:
					gdh->__examineFH(gTask);
					if ( gTask._result == DOSTRUE )
					{
						__log("[done] name:'%s'", ((FileInfoBlock*)(gTask._int2))->fib_FileName+1);
					}
					else
					{
						__log("[fail] error_code:%d",gTask._error );
					}
					break;
				case sTask::ttExamineAll:
					break;
				case sTask::ttExamineAllEnd:
					break;
				default:
					break;
				}
				Signal(gHandlerTask,gHandlerSignalMask);
			}
		}
		else
		{
			__log("info failed");
		}
	}
	else
	{
		__log("auth failed");
	}

	__log("closing worker process handler");

	// kill handler instance
	delete gdh;

#ifndef OFFLINE_SIMULATION
	curl_global_cleanup();
#endif

	FreeSignal(mySignal);

	// close std lib
#ifdef PLATFORM_AMIGAOS3
	/*
	__asm__ __volatile__ (
		"movem.l d0-d7/a0-a6,-(sp) \n"
		"lea ___EXIT_LIST__+4,a2 \n"
		"moveql	#0,d2 \n"
		"jsr	_callfuncs \n"
		"movem.l (sp)+,d0-d7/a0-a6 \n"
		);
	*/
#elif defined PLATFORM_MORPHOS
	//UnInitLibnix(); // causes crash on mos
#endif

	//kprintf("worker bye\n");

	gWorkerIsRunning = false;
}

#if defined PLATFORM_AROS || defined PLATFORM_AMIGAOS4
int main()
#else
int __saveds main()
#endif
{
	struct Process *handler = NULL;
	struct DosPacket *packet = NULL;
	struct FileHandle *fh = NULL;
	struct DosList *volume = NULL;

	gTask._task = 0;
	gHandlerIsRunning = true;

	char filename[STR_LEN*2];
	char filename2[STR_LEN*2];
	char _temp[STR_LEN*2];

#if !defined PLATFORM_AMIGAOS4 && !defined PLATFORM_AROS
	SysBase = *(struct ExecBase **)4;
#endif

	gHandlerTask = FindTask(0);
	handler = (struct Process*) gHandlerTask;

	//kprintf("debugData1:%08lx debugData2:%08lx\n",&debugData1,&debugData2);

	packet = getPacket(handler);

	// 
	// startup begin
	//

	u8 mySignal = AllocSignal(-1);
	gHandlerSignalMask = (1<<mySignal);

#ifdef PLATFORM_AMIGAOS4
	DOSBase = OpenLibrary(DOSNAME, DOS_VERSION);
	IDOS = (struct DOSIFace*) GetInterface(DOSBase, "main", 1, NULL);
#else
	DOSBase = (struct DosLibrary*) OpenLibrary(DOSNAME, DOS_VERSION);
#endif

	// setup volume

	volume = MyMakeDosEntry((TEXT*)gVolumeName(), DLT_VOLUME);
	if ( !volume )
	{
		gHandlerIsRunning = false;
	}
	else
	{
		volume->dol_Task = &handler->pr_MsgPort;
		volume->dol_Lock = 0;
		DateStamp(&volume->dol_misc.dol_volume.dol_VolumeDate);
		volume->dol_misc.dol_volume.dol_DiskType = ID_DOS_DISK;
		volume->dol_misc.dol_volume.dol_LockList = 0; 
		AddDosEntry(volume);

		gLockTask = int(&handler->pr_MsgPort);
		gLockVolume = int(MKBADDR(volume));
	}

	gDevNode = (struct DeviceNode*)BADDR(packet->dp_Arg3);

	gDevNode->dn_Task = &handler->pr_MsgPort;

	returnPacket(packet,handler,DOSTRUE,0);

	if ( gHandlerIsRunning )
	{
		struct TagItem tagitem[5];
		int idx = 0;
		tagitem[idx].ti_Tag = NP_Entry;
		tagitem[idx++].ti_Data = (ULONG) &taskFunc;
		tagitem[idx].ti_Tag = NP_Priority;
		tagitem[idx++].ti_Data = 1;
		tagitem[idx].ti_Tag = NP_Name;
		tagitem[idx++].ti_Data = (ULONG) gTaskHandlerName();
		tagitem[idx].ti_Tag = NP_StackSize;
		tagitem[idx++].ti_Data = 32768;
#ifdef PLATFORM_MORPHOS
		tagitem[idx].ti_Tag = NP_CodeType;
		tagitem[idx++].ti_Data = CODETYPE_PPC;
#endif
		tagitem[idx].ti_Tag = TAG_DONE;
		tagitem[idx++].ti_Data = 0;
		struct Process *myTask = CreateNewProcTagList(tagitem);

		while ( !gWorkerIsRunning ) // wait for subtask setup
		{
			Delay(1);
		}
		Delay(1); // make sure the subtask is in wait() state

		// this will be the subtask's first task :)
		ULONG stacksize = (char*)((Task*)handler)->tc_SPUpper-(char*)((Task*)handler)->tc_SPLower; 
		__sprintf(_temp,"dos handler\n segment:%08lx task_id:%08lx stacksize:%ld signal:%08lx",
			segmentStartAddress,handler,stacksize,gWorkerSignalMask);
		doTask(sTask::ttLog,_temp);
	}

	while ( gHandlerIsRunning && gWorkerIsRunning )
	{
		packet = getPacket(handler);

		gTask._task = ((struct MsgPort*)packet->dp_Port)->mp_SigTask; // pass task ptr

		switch ( packet->dp_Type )
		{
		case ACTION_IS_FILESYSTEM:
			doTask(sTask::ttLog,"ACTION_IS_FILESYSTEM (YES)");
			returnPacket(packet,handler,DOSTRUE,0);
			break;

		case ACTION_LOCATE_OBJECT:
			getString( (const char*) BADDR(packet->dp_Arg2), filename);

			__sprintf(_temp,"ACTION_LOCATE_OBJECT parent_lock:%08lx name:'%s' mode:%ld",
				BADDR(packet->dp_Arg1), filename, packet->dp_Arg3 );
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttLocateObject, filename, int(BADDR(packet->dp_Arg1)), packet->dp_Arg3 );

			/*
			if ( gTask._result )
			{
				((FileLock*)gTask._result)->fl_Task = &handler->pr_MsgPort;
				((FileLock*)gTask._result)->fl_Volume = MKBADDR(volume);
			}
			*/

			returnPacket(packet,handler,(long)MKBADDR(gTask._result),gTask._error);
			break;

		case ACTION_EXAMINE_OBJECT:
			__sprintf(_temp,"ACTION_EXAMINE_OBJECT lock:%08lx fileinfoblock:%08lx",BADDR(packet->dp_Arg1),BADDR(packet->dp_Arg2));
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttExamineObject, "", int(BADDR(packet->dp_Arg1)), int(BADDR(packet->dp_Arg2)) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_EXAMINE_NEXT:
			__sprintf(_temp,"ACTION_EXAMINE_NEXT [lock:%08lx] fileinfoblock:%08lx",BADDR(packet->dp_Arg1),BADDR(packet->dp_Arg2));
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttExamineObject, "", 0, int(BADDR(packet->dp_Arg2)) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_FREE_LOCK:
			__sprintf(_temp,"ACTION_FREE_LOCK lock:%08lx",BADDR(packet->dp_Arg1));
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttFreeLock, "", int(BADDR(packet->dp_Arg1)) );

			returnPacket(packet,handler,DOSTRUE,0);
			break;

		case ACTION_INFO:
			__sprintf(_temp,"ACTION_INFO lock:%08lx info_block:%08lx",BADDR(packet->dp_Arg1),BADDR(packet->dp_Arg2));
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttInfo, "", int(BADDR(packet->dp_Arg1)), int(BADDR(packet->dp_Arg2)) );

			((InfoData*)(BADDR(packet->dp_Arg2)))->id_VolumeNode = MKBADDR(volume);

			returnPacket(packet,handler,DOSTRUE,0);
			break;

		case ACTION_DISK_INFO:
			__sprintf(_temp,"ACTION_DISK_INFO info_block:%08lx",BADDR(packet->dp_Arg1));
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttInfo, "", 0, int(BADDR(packet->dp_Arg1)) );

			((InfoData*)(BADDR(packet->dp_Arg1)))->id_VolumeNode = MKBADDR(volume);

			returnPacket(packet,handler,DOSTRUE,0);
			break;

		case ACTION_PARENT:
			__sprintf(_temp,"ACTION_PARENT lock:%08lx",BADDR(packet->dp_Arg1));
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttParent, "", int(BADDR(packet->dp_Arg1)) );

			/*
			if ( gTask._result )
			{
				((FileLock*)gTask._result)->fl_Task = &handler->pr_MsgPort;
				((FileLock*)gTask._result)->fl_Volume = MKBADDR(volume);
			}
			*/

			returnPacket(packet,handler,(long)MKBADDR(gTask._result),gTask._error);
			break;

		case ACTION_COPY_DIR:
			__sprintf(_temp,"ACTION_COPY_DIR lock:%08lx",BADDR(packet->dp_Arg1));
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttCopyDir, "", int(BADDR(packet->dp_Arg1)) );

			/*
			if ( gTask._result )
			{
				((FileLock*)gTask._result)->fl_Task = &handler->pr_MsgPort;
				((FileLock*)gTask._result)->fl_Volume = MKBADDR(volume);
			}
			*/

			returnPacket(packet,handler,(long)MKBADDR(gTask._result),gTask._error);
			break;

		case ACTION_SAME_LOCK:
			__sprintf(_temp,"ACTION_SAME_LOCK lock1:%08lx lock2:%08lx",BADDR(packet->dp_Arg1), BADDR(packet->dp_Arg2));
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttSameLock, "", int(BADDR(packet->dp_Arg1)), int(BADDR(packet->dp_Arg2)) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_DELETE_OBJECT:
			getString( (const char*) BADDR(packet->dp_Arg2), filename);

			__sprintf(_temp,"ACTION_DELETE_OBJECT lock:%08lx name:'%s'", BADDR(packet->dp_Arg1), filename );
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttDelete, filename, int(BADDR(packet->dp_Arg1)) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_FINDINPUT: // read only
			getString( (const char*) BADDR(packet->dp_Arg3), filename);

			__sprintf(_temp,"ACTION_FINDINPUT handle:%08lx lock:%08lx name:'%s'", BADDR(packet->dp_Arg1), BADDR(packet->dp_Arg2), filename );
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttFind, filename, int(BADDR(packet->dp_Arg1)), int(BADDR(packet->dp_Arg2)), MODE_OLDFILE );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_FINDOUTPUT: // new file (existing file can not be deleted this way)
			getString( (const char*) BADDR(packet->dp_Arg3), filename);

			__sprintf(_temp,"ACTION_FINDOUTPUT handle:%08lx lock:%08lx name:'%s'", BADDR(packet->dp_Arg1), BADDR(packet->dp_Arg2), filename );
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttFind, filename, int(BADDR(packet->dp_Arg1)), int(BADDR(packet->dp_Arg2)), MODE_NEWFILE );
			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_FINDUPDATE: // rw - not supported if file exist
			getString( (const char*) BADDR(packet->dp_Arg3), filename);

			__sprintf(_temp,"ACTION_FINDUPDATE handle:%08lx lock:%08lx name:'%s'", BADDR(packet->dp_Arg1), BADDR(packet->dp_Arg2), filename );
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttFind, filename, int(BADDR(packet->dp_Arg1)), int(BADDR(packet->dp_Arg2)), MODE_READWRITE );
			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_END:
			__sprintf(_temp,"ACTION_END my_handle_ptr:%08lx",packet->dp_Arg1);
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttEnd, "", int(packet->dp_Arg1) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_READ:
			__sprintf(_temp,"ACTION_READ my_handle_ptr:%08lx buffer:%08lx number:%ld", packet->dp_Arg1, packet->dp_Arg2, packet->dp_Arg3 );
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttRead, "", int(packet->dp_Arg1), int(packet->dp_Arg2), int(packet->dp_Arg3) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_WRITE:
			__sprintf(_temp,"ACTION_WRITE my_handle_ptr:%08lx buffer:%08lx number:%ld", packet->dp_Arg1, packet->dp_Arg2, packet->dp_Arg3 );
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttWrite, "", int(packet->dp_Arg1), int(packet->dp_Arg2), int(packet->dp_Arg3) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_SEEK:
			__sprintf(_temp,"ACTION_SEEK my_handle_ptr:%08lx offset:%ld mode:%ld", packet->dp_Arg1, packet->dp_Arg2, packet->dp_Arg3 );
			doTask(sTask::ttLog,_temp);

			doTask( sTask::ttSeek, "", int(packet->dp_Arg1), int(packet->dp_Arg2), int(packet->dp_Arg3) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_CREATE_DIR:
			getString( (const char*) BADDR(packet->dp_Arg2), filename);

			__sprintf(_temp,"ACTION_CREATE_DIR lock:%08lx name:'%s'", BADDR(packet->dp_Arg1), filename );
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttCreateDir, filename, int(BADDR(packet->dp_Arg1)) );

			/*
			if ( gTask._result )
			{
				((FileLock*)gTask._result)->fl_Task = &handler->pr_MsgPort;
				((FileLock*)gTask._result)->fl_Volume = MKBADDR(volume);
			}
			*/

			returnPacket(packet,handler,(long)MKBADDR(gTask._result),gTask._error);
			break;

		case ACTION_RENAME_OBJECT:
			getString( (const char*) BADDR(packet->dp_Arg2), filename); // mit
			getString( (const char*) BADDR(packet->dp_Arg4), filename2); // mire

			__sprintf(_temp,"ACTION_RENAME_OBJECT lock_from:%08lx name_from:'%s' lock_to:%08lx name_to:'%s'",
				BADDR(packet->dp_Arg1), filename, BADDR(packet->dp_Arg3), filename2);
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttRename, filename, int(BADDR(packet->dp_Arg1)), (int)filename2, int(BADDR(packet->dp_Arg3)) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_CURRENT_VOLUME:
			returnPacket(packet,handler,(long)MKBADDR(volume),0);
			break;

		case ACTION_COPY_DIR_FH:
			__sprintf(_temp,"ACTION_COPY_DIR_FH my_handle_ptr:%08lx", packet->dp_Arg1);
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttCopyDirFH, "", int(packet->dp_Arg1) );

			/*
			if ( gTask._result )
			{
				((FileLock*)gTask._result)->fl_Task = &handler->pr_MsgPort;
				((FileLock*)gTask._result)->fl_Volume = MKBADDR(volume);
			}
			*/

			returnPacket(packet,handler,(long)MKBADDR(gTask._result), gTask._error);
			break;

		case ACTION_FH_FROM_LOCK:
			__sprintf(_temp,"ACTION_FH_FROM_LOCK handle:%08lx lock:%08lx", BADDR(packet->dp_Arg1), BADDR(packet->dp_Arg2) );
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttFHFromLock, "", int(BADDR(packet->dp_Arg1)), int(BADDR(packet->dp_Arg2)) );

			returnPacket(packet,handler,(long)MKBADDR(gTask._result), gTask._error);
			break;

		case ACTION_PARENT_FH:
			__sprintf(_temp,"ACTION_PARENT_FH handle:%08lx", packet->dp_Arg1 );
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttParentFH, "", int(packet->dp_Arg1) );

			/*
			if ( gTask._result )
			{
				((FileLock*)gTask._result)->fl_Task = &handler->pr_MsgPort;
				((FileLock*)gTask._result)->fl_Volume = MKBADDR(volume);
			}
			*/

			returnPacket(packet,handler,(long)MKBADDR(gTask._result), gTask._error);
			break;

		case ACTION_EXAMINE_FH:
			__sprintf(_temp,"ACTION_EXAMINE_FH handle:%08lx fileinfoblock:%08lx", packet->dp_Arg1, BADDR(packet->dp_Arg2) );
			doTask(sTask::ttLog,_temp);

			doTask(sTask::ttExamineFH, "", int(packet->dp_Arg1), int(BADDR(packet->dp_Arg2)) );

			returnPacket(packet,handler,gTask._result,gTask._error);
			break;

			// ------
			// todo's
			// ------

		case ACTION_EXAMINE_ALL:
			doTask(sTask::ttLog,"ACTION_EXAMINE_ALL");
			returnPacket(packet,handler,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
			//returnPacket(packet,handler,gTask._result,gTask._error);
			break;

		case ACTION_EXAMINE_ALL_END:
			doTask(sTask::ttLog,"ACTION_EXAMINE_ALL_END");
			returnPacket(packet,handler,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
			//returnPacket(packet,handler,gTask._result,gTask._error);
			break;

			//
			// dummy or unsupported actions
			//

		case ACTION_CHANGE_MODE:
			__sprintf(_temp,"ACTION_CHANGE_MODE *** UNSUPPORTED *** type:%ld object:%08lx new_mode:%0d",
				packet->dp_Arg1, BADDR(packet->dp_Arg2), packet->dp_Arg3 );
			doTask(sTask::ttLog, _temp);
			/*
			doTask(sTask::ttChangeMode, "", int(packet->dp_Arg1), int(BADDR(packet->dp_Arg2)), int(packet->dp_Arg3) );
			returnPacket(packet,handler,gTask._result,gTask._error);
			*/
			returnPacket(packet,handler,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
			break;
		case ACTION_ADD_NOTIFY:
			doTask(sTask::ttLog,"ACTION_ADD_NOTIFY *** UNSUPPORTED ***");
			returnPacket(packet,handler,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
			break;
		case ACTION_SET_FILE_SIZE:
			doTask(sTask::ttLog,"ACTION_SET_FILE_SIZE *** UNSUPPORTED ***");
			returnPacket(packet,handler,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
			break;
		case ACTION_GET_DISK_FSSM:
			doTask(sTask::ttLog,"ACTION_GET_DISK_FSSM *** UNSUPPORTED ***");
			returnPacket(packet,handler,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
			break;
		case ACTION_FREE_DISK_FSSM:
			doTask(sTask::ttLog,"ACTION_FREE_DISK_FSSM *** UNSUPPORTED ***");
			returnPacket(packet,handler,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
			break;
		case ACTION_RENAME_DISK:
			doTask(sTask::ttLog,"ACTION_RENAME_DISK (ignored)");
			returnPacket(packet,handler,DOSTRUE,0);
			break;
		case ACTION_SET_PROTECT:
			doTask(sTask::ttLog,"ACTION_SET_PROTECT (ignored)");
			returnPacket(packet,handler,DOSTRUE,0);
			break;
		case ACTION_SET_COMMENT:
			doTask(sTask::ttLog,"ACTION_SET_COMMENT (ignored)");
			returnPacket(packet,handler,DOSTRUE,0);
			break;
		case ACTION_SET_DATE:
			doTask(sTask::ttLog,"ACTION_SET_DATE (ignored)");
			returnPacket(packet,handler,DOSTRUE,0);
			break;
		case ACTION_FLUSH:
			doTask(sTask::ttLog,"ACTION_FLUSH (ignored)");
			returnPacket(packet,handler,DOSTRUE,0);
			break;
		case ACTION_INHIBIT:
			doTask(sTask::ttLog,"ACTION_INHIBIT");
			doTask(sTask::ttInhibit,"");
			returnPacket(packet,handler,gTask._result,0);
			break;
		case ACTION_DIE:
		case ACTION_SHUTDOWN:
			doTask(sTask::ttLog,"ACTION_DIE");
			doTask(sTask::ttDie,"");
			if ( gTask._result == DOSTRUE )
			{
				gHandlerIsRunning = false;
			}
			else
			{
				returnPacket(packet,handler,gTask._result,ERROR_OBJECT_IN_USE);
			}
			break;
		default:
			__sprintf(_temp,"*** ACTION_UNKNOWN *** %ld",packet->dp_Type);
			doTask(sTask::ttLog,_temp);
			returnPacket(packet,handler,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
			break;
		}
	}

#ifndef _NOLOG
	// close logs
	if ( gLogFile )
	{
		Close(gLogFile);
	}
	if ( gConsoleWindow )
	{
		Close(gConsoleWindow);
	}
	if ( gSerialOutput )
	{
		Close(gSerialOutput);
	}
#endif

	//kprintf("waiting for worker thread end...\n");

	// wait for worker task 
	while ( gWorkerIsRunning )
	{
		Delay(1);
	}

	if ( volume != NULL )
	{
		//kprintf("ReallyRemoveDosEntry()\n");
		if ( ReallyRemoveDosEntry(volume) )
		{
			MyFreeDosEntry(volume);
		}
	}

	FreeSignal(mySignal);

	gDevNode->dn_Task = FALSE;

	returnPacket(packet,handler,DOSTRUE,0);

	return 0;
} 

#endif
