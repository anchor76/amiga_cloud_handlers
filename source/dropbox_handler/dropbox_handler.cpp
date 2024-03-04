// dropbox_handler.cpp - anchor

// todo

// dropbox forbids following chars: <>/\"*?:|

#include "dropbox_handler.h"

//
// cDropBoxHandler class
//

cDropBoxHandler::cDropBoxHandler()
{
}

cDropBoxHandler::~cDropBoxHandler()
{
}

void cDropBoxHandler::init()
{
	_version = "$VER: dropbox-handler v0.3 (" __DATE__ ")";
#ifndef _NOLOG
# ifdef PLATFORM_AMIGA
	_version += " log(";
	if ( gConsoleLogEnabled )
	{
		_version += "C";
	}
	if ( gFileLogEnabled )
	{
		_version += "F";
	}
	if ( gSerialLogEnabled )
	{
		_version += "S";
	}
	_version += ")";
# else
	_version += " logging";
# endif
#endif

	_volumeName = gVolumeName();
#ifdef PLATFORM_WINDOWS
	stringc _home("devs/");
#else
	stringc _home("Devs:Cloud/");
#endif
	_accessTokenFile = _home + "dropbox_access_token";
	_clientCodePath = _home + "dropbox_client_code";
	_codePagesPath = _home + ".codepages";
	_keyfilePath = _home + "keyfile";
	_replaceDescriptor = "<_>_/_\\_\"_*_?_:_|_";

	_appendUploadSupported = true;

	cDosHandler::init();
}

/*
void cDropBoxHandler::onHandlerCommand(const stringc& pStr)
{
	stringc _path(pStr);
	_path.make_lower();
	if ( _path.find("@ocr") == 0 )
	{
#ifdef PLATFORM_AMIGA
		struct EasyStruct myES =
		{
			sizeof(struct EasyStruct),
			0,
			(EASY_TEXT)"OCR Setting",
			(EASY_TEXT)"OCR is currently turned %s.",
			(EASY_TEXT)"Turn it %s|Cancel",
		};

#ifdef PLATFORM_AMIGAOS4
		IIntuition = (struct IntuitionIFace*) GetInterface(IntuitionBase, "main", 1, NULL);
#endif

		int answer = EasyRequest(NULL, &myES, NULL,
			(const char*)(_ocr?"on":"off"), (const char*)(_ocr?"off":"on") );

		if ( answer == 1 )
		{
			_ocr = !_ocr;
			__log("OCR is turned %s",_ocr?"ON":"OFF");
		}
#endif
	}
	else
	{
		cDosHandler::onHandlerCommand(pStr);
	}
	cDosHandler::onHandlerCommand(pStr);
}
*/

void cDropBoxHandler::auth(bool reAuth)
{
#ifdef OFFLINE_SIMULATION
	_authed = true;
#else

	if ( reAuth )
	{
		return;
	}

	__log("cDropBoxHandler::auth() starting...");

	stringc atKey("\"access_token\"");

	int _len = 0;

	__loadFile(_accessTokenFile.c_str(),_accessToken,_len);

	if ( !_accessToken ) // no access token yet?
	{
		__loadFile(_clientCodePath.c_str(),_clientCode,_len);
		if ( !_clientCode )
		{
			stringc _err = "cDropBoxHandler::auth() no client code found";
			__log(_err.c_str());
			messageBox(_err.c_str());
			return;
		}
		else
		{
			// fix client code
			for(int _i=0;_i<_len;_i++)
			{
				if ( _clientCode[_i] == '\r' || _clientCode[_i] == '\n' )
				{
					_clientCode[_i] = 0;
				}
			}
		}

		curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://api.dropbox.com/oauth2/token");
		// get new tokens
		sprintf(_temp,"code=%s&grant_type=authorization_code&client_id=%s&client_secret=%s",
			_clientCode, APP_KEY, APP_SECRET );

		curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _temp);

		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, 0);

		__initDownloadToLocalIoBuffer(); // setup io buffer

#if defined PLATFORM_AMIGA && !defined _NOLOG
		struct DateStamp _t1;
		DateStamp(&_t1);
#endif

		CURLcode curlResult = curl_easy_perform(_curlCtx);

#if defined PLATFORM_AMIGA && !defined _NOLOG
		struct DateStamp _t2;
		DateStamp(&_t2);

		int dt = ((_t2.ds_Minute*3000)+_t2.ds_Tick) - ((_t1.ds_Minute*3000)+_t1.ds_Tick);
		__log("dt=%d",dt);
#endif

		if ( curlResult != CURLE_OK )
		{
			sprintf(_temp,"cDropBoxHandler::auth() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(_temp);
			messageBox(_temp);
		}
		else
		{
			stringc response(gDownloadBuffer);

			if ( response.find(atKey.c_str()) != -1 )
			{
				// extract access token

				int ofs = response.find(atKey.c_str());
				ofs += atKey.size();
				while ( response[ofs] != '\"' )
				{
					ofs++;
				}
				ofs++; // skip "
				stringc accessToken;
				char* ptr = (char*)(response.c_str()+ofs);
				__extractAsString(ptr,accessToken,'\"');

				// save token

				cFileWriter* writer = new cFileWriter(_accessTokenFile.c_str());
				if ( writer->isValid() )
				{
					writer->saveRaw((void*)accessToken.c_str(),accessToken.size());
					delete writer;
				}

				// load token

				__loadFile(_accessTokenFile.c_str(),_accessToken,_len);
				//__loadFile(_refreshTokenFile.c_str(),_refreshToken,_len);
				_authed = true;
			}
			else
			{
				sprintf(_temp,"cDropBoxHandler::auth() failed (first time login)\n%s",gDownloadBuffer);
				__log(_temp);
				messageBox(_temp);
			}
		}
	}
	else
	{
		_authed = true;
	}
#endif
}

bool cDropBoxHandler::getFileList(const stringc& pFolderId)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() )
	{
		__log("cDropBoxHandler::getFileList() invoked folderid='%s'",pFolderId.c_str());

		//int filesPerPage = NETWORK_BUF_LEN/4096;

		stringc _folderId = pFolderId;
		if ( _folderId == "root" )
		{
			_folderId = "";
		}

		stringc _pageToken;
		bool hasMore = false;

		do 
		{
			if ( _pageToken.size() )
			{
				curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://api.dropboxapi.com/2/files/list_folder/continue");
				sprintf(_iobuf,"{\n\t\"cursor\": \"%s\"\n}", _pageToken.c_str());
			}
			else
			{
				curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://api.dropboxapi.com/2/files/list_folder");
				sprintf(_iobuf,"{\n\t\"path\": \"%s\",\n\t\"recursive\": false,\n\t\"include_media_info\": false,\n\t\"include_deleted\": false\n}",
					_folderId.c_str());
			}
			curl_easy_setopt(_curlCtx, CURLOPT_POST, 1);
			long clen = strlen(_iobuf);
			curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _iobuf );
			curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, clen);

			struct curl_slist *_headers = NULL;
			sprintf(_temp,"Authorization: Bearer %s",_accessToken);
			_headers = curl_slist_append(_headers, _temp);
			_headers = curl_slist_append(_headers,"Content-Type: application/json");

			curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

			cFileOpen* _buffer = new cFileOpen(cFileOpen::omDownload,NULL);

			__initDownloadToFileBuffer(_buffer); // setup io buffer
			CURLcode curlResult = curl_easy_perform(_curlCtx);

			curl_slist_free_all(_headers);
			curl_easy_setopt(_curlCtx, CURLOPT_POST, 0);

			if ( curlResult != CURLE_OK )
			{
				__log("cGoogleDriveHandler::getFileList() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
				if ( _buffer->_cache.size() )
				{
					__log((char*)_buffer->_cache[0]);
				}
				delete _buffer;
			}
			else
			{
				int _len = _buffer->_openCursor;
				_buffer->_cachedFileSize = _len;
				_buffer->_openCursor = 0;

				__log("cGoogleDriveHandler::getFileList() response length:%d",_len);

				// concatenate the buffer slices
				char* _fullData = (char*) malloc(_len+1);
				_fullData[_len] = 0;
				_buffer->readData((u8*)_fullData,_len);

				delete _buffer;

				// process page header
				if ( !strstr(_fullData,"\"entries\":") )
				{
					__log("cGoogleDriveHandler::getFileList() list error");
					return false;
				}

				char* _cursor = _fullData;
				getJSON_string(_cursor,"\"cursor\":",_pageToken);
				getJSON_bool(_cursor,"\"has_more\":",hasMore);
				_cursor = _fullData; // seek back to start
				processPage(_cursor,_len,_folderId.c_str());
			}
		} while ( hasMore );
		return true;
	}
	return false;
#endif
}

void cDropBoxHandler::processPage(char* pBuf, u32 pLen, const char* pParentId)
{
	// get file start poisitions for file pieces
	char* buf = pBuf;
	array<char*> _fileStartPosList;
	bool _found = true;
	while ( _found )
	{
		jumpToKey(buf,"\".tag\":");
		if ( *buf && u32(buf-pBuf) < pLen )
		{
			_fileStartPosList.push_back(buf);
			buf++;
		}
		else
		{
			_found = false;
		}
	}

	for(u32 _i=1;_i<_fileStartPosList.size();_i++)
	{
		*(_fileStartPosList[_i]-1) = 0; // terminate
	}

	/*
	for(u32 _i=0;_i<_fileStartPosList.size();_i++)
	{
		__log(_fileStartPosList[_i]);
	}
	*/

	for(u32 _i=0;_i<_fileStartPosList.size();_i++)
	{
		buf = (char*) _fileStartPosList[_i];

		cDosFile* _file = new cDosFile();
		_file->_parentId = pParentId;
		_fileList.push_back(_file);

		processFile(buf,_file,false);

		__log("cDropBoxHandler::processPage() #%d: title:'%s' [%s] id:'%s' size:%d ptr:%08x",
			_fileList.size(),_file->_title.c_str(),_file->_folder?"folder":"file",
			_file->_id.c_str(),_file->_fileSize,u32(_file));
	}
}

void cDropBoxHandler::processFile(char* pBuf, cDosFile* pFile, bool pForceFile)
{
	char* bufSave = pBuf;
	stringc _tempStr;

	if ( !pForceFile && !strstr(pBuf,"\".tag\": \"file\"") )
	{
		pFile->_folder = true;
	}

	getJSON_string(pBuf,"\"name\":",_tempStr);
	__convertFromEscaped(_tempStr,pFile->_title);
	pFile->_titleLC = pFile->_title;
	pFile->_titleLC.make_lower();

	getJSON_string(pBuf,"\"path_lower\":",pFile->_id);

	pFile->_parentId = pFile->_id;
	int _pos = pFile->_parentId.findLast('/');
	if ( _pos != -1 )
	{
		if ( _pos == 0 )
		{
			pFile->_parentId = "";
		}
		else
		{
			pFile->_parentId = pFile->_parentId.subString(0,_pos);
		}
	}

	// download url
#ifdef DROPBOX_APIv2
	getJSON_string(pBuf,"\"id\":",_tempStr);
#else
	_tempStr = pFile->_id;
	_tempStr.erase(0);
	stringw _wt;
	__escapeToWideChar(_tempStr,_wt);
	__wideCharToUTF8(_wt,_tempStr);
#endif
	char* _encoded = curl_easy_escape(_curlCtx, _tempStr.c_str(), _tempStr.size() );
	pFile->_downloadURL = _encoded;
	curl_free(_encoded);

	// read optional fields

	if ( !pFile->_folder )
	{
		getJSON_string(pBuf,"\"client_modified\":",pFile->_modDate);
		getJSON_int(pBuf,"\"size\":",pFile->_fileSize);
	}
}

bool cDropBoxHandler::downloadFile(const stringc& pFile, int pStart, int pCount, u8* pBuffer)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile.size() )
	{
		sprintf(_temp,"https://content.dropboxapi.com/2/files/download?arg=%%7B%%22path%%22:%%20%%22/%s%%22%%7D",pFile.c_str());
		curl_easy_setopt(_curlCtx, CURLOPT_URL, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_POST, 0);

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		sprintf(_temp,"Range: bytes=%d-%d",pStart,pStart+pCount-1);
		_headers = curl_slist_append(_headers, _temp);

		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		if ( pCount >= NETWORK_BUF_LEN ) // direct download allowed
		{
			__initDownloadToCustomBuffer((char*)pBuffer,pCount); // setup io buffer
		}
		else
		{
			__initDownloadToLocalIoBuffer(); // setup io buffer
		}
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		curl_slist_free_all(_headers);

		if ( curlResult != CURLE_OK )
		{
			__log("cDropBoxHandler::downloadFileExact() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			__log("cDropBoxHandler::downloadFileExact() response length:%d",gDownloadCursor);

			if ( pCount < NETWORK_BUF_LEN ) // local buffer was used?
			{
				memcpy(pBuffer,gDownloadBuffer,gDownloadCursor);
			}
			return true;
		}
	}
	return false;
#endif
}

bool cDropBoxHandler::createDirectory(cDosFile* pFile)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile )
	{
		//
		// update file
		//

		stringc _title;
		__convertToEscaped(pFile->_title,_title);

		curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://api.dropboxapi.com/2/files/create_folder");
		curl_easy_setopt(_curlCtx, CURLOPT_POST, 1);

		sprintf(_iobuf,"{\"path\": \"%s/%s\"}",pFile->_parentId.c_str(),_title.c_str());
		long clen = strlen(_iobuf);
		curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _iobuf );
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, clen);

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		_headers = curl_slist_append(_headers,"Content-Type: application/json");

		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		__initDownloadToLocalIoBuffer();
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, NULL);
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)-1);

		if ( curlResult != CURLE_OK )
		{
			__log("cDropBoxHandler::createDirectory() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			if ( !strstr(gDownloadBuffer,"\"error\":") )
			{
				char* buf = gDownloadBuffer;
				processFile(buf,pFile,true);
				return true;
			}
			else
			{
				__log("cDropBoxHandler::createDirectory() failed: %s",gDownloadBuffer);
			}
		}
	}
	return false;
#endif
}

bool cDropBoxHandler::beginUpload(cDosFile* pFile)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile )
	{
		//
		// creating file
		//

		__log("cDropBoxHandler::beginUpload() %s",pFile->_title.c_str());

		curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://content.dropboxapi.com/2/files/upload_session/start");
		curl_easy_setopt(_curlCtx, CURLOPT_POST, 1);

		// zero length payload
		_iobuf[0] = 0;
		curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _iobuf );

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		_headers = curl_slist_append(_headers, "Content-Type: application/octet-stream");

		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		__initDownloadToLocalIoBuffer();
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);

		if ( curlResult != CURLE_OK )
		{
			__log("cDropBoxHandler::beginUpload() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			char* buf = gDownloadBuffer;
			getJSON_string(buf,"\"session_id\":",pFile->_id);
			return (pFile->_id.size()!=0);
		}
	}
	return false;
#endif
}

bool cDropBoxHandler::uploadFile(cDosFile* pFile, cFileOpen* pHandle, bool pFinal)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile && pHandle )
	{
		//
		// upload content
		//

		__log("cDropBoxHandler::uploadFile() size:%d offset:%d",pHandle->_cachedFileSize,pHandle->_userData);

		sprintf(_temp,"https://content.dropboxapi.com/2/files/upload_session/%s",pFinal?"finish":"append");
		curl_easy_setopt(_curlCtx, CURLOPT_URL, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_POST, 1);
		curl_easy_setopt(_curlCtx, CURLOPT_UPLOAD, 1);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, "POST");

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		if ( pFinal )
		{
			stringc _title;
			__convertToEscaped(pFile->_title,_title);
			sprintf(_temp,"Dropbox-API-Arg: {\"cursor\": {\"session_id\": \"%s\",\"offset\": %d},\"commit\": {\"path\": \"%s/%s\",\"mode\": \"add\",\"autorename\": false,\"mute\": false}}",
				pFile->_id.c_str(),pHandle->_userData,pFile->_parentId.c_str(),_title.c_str());
		}
		else
		{
			sprintf(_temp,"Dropbox-API-Arg: {\"session_id\": \"%s\",\"offset\": %d}",pFile->_id.c_str(),pHandle->_userData);
		}
		_headers = curl_slist_append(_headers, _temp);
		_headers = curl_slist_append(_headers, "Content-Type: application/octet-stream");
		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		curl_easy_setopt(_curlCtx, CURLOPT_READFUNCTION, rdfu);
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)pHandle->_cachedFileSize);

		__initUploadFromFileBuffer(pHandle);
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)-1);
		curl_easy_setopt(_curlCtx, CURLOPT_READFUNCTION, 0);
		curl_easy_setopt(_curlCtx, CURLOPT_UPLOAD, 0);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, NULL);

		if ( curlResult != CURLE_OK )
		{
			__log("cDropBoxHandler::uploadFile() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			if ( !strstr(gDownloadBuffer,"\"error\":") )
			{
				pHandle->_userData += pHandle->_cachedFileSize;
				if ( pFinal )
				{
					processFile(gDownloadBuffer,pFile,true);
				}
				return true;
			}
		}
	}
	return false;
#endif
}

bool cDropBoxHandler::updateFile(cDosFile* pFile, bool pNameAndParent, bool pDate, bool pTrashed)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile )
	{
		//
		// update file
		//

		stringc _title;
		__convertToEscaped(pFile->_title,_title);

		if ( pTrashed )
		{
			curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://api.dropboxapi.com/2/files/delete");
			sprintf(_iobuf,"{\"path\": \"%s\"}",pFile->_id.c_str());
		}
		else if ( pNameAndParent )
		{
			curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://api.dropboxapi.com/2/files/move");
			sprintf(_iobuf,"{\"from_path\": \"%s\",\"to_path\": \"%s/%s\"}",
				pFile->_id.c_str(),pFile->_parentId.c_str(),_title.c_str());
		}
		else if ( pDate )
		{
			// todo
		}
		else
		{
			return false;
		}

		curl_easy_setopt(_curlCtx, CURLOPT_POST, 1);
		long clen = strlen(_iobuf);
		curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _iobuf );
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, clen);

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		_headers = curl_slist_append(_headers,"Content-Type: application/json");

		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		__initDownloadToLocalIoBuffer();
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, NULL);
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)-1);

		if ( curlResult != CURLE_OK )
		{
			__log("cDropBoxHandler::updateFile() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			if ( !strstr(gDownloadBuffer,"\"error\":") )
			{
				if ( !pTrashed )
				{
					char* buf = gDownloadBuffer;
					processFile(buf,pFile,true);
				}
				return true;
			}
			else
			{
				__log("cDropBoxHandler::updateFile() failed: %s",gDownloadBuffer);
			}
		}
	}
	return false;
#endif
}

bool cDropBoxHandler::getAccountInfo()
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() )
	{
		//
		// get dropbox account info
		//

		curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://api.dropbox.com/2/users/get_current_account");
		curl_easy_setopt(_curlCtx, CURLOPT_UPLOAD, 0);

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, "POST");

		__initDownloadToLocalIoBuffer();
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, NULL);

		if ( curlResult != CURLE_OK )
		{
			__log("cDropBoxHandler::getAccountInfo() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			__log("cDropBoxHandler::getAccountInfo() response length:%d",gDownloadCursor);

			char* buf = gDownloadBuffer;
			getJSON_string(buf,"\"email\":",_accountMailAddress);

			//
			// get dropbox quota info
			//

			curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://api.dropbox.com/2/users/get_space_usage");
			curl_easy_setopt(_curlCtx, CURLOPT_UPLOAD, 0);

			struct curl_slist *_headers = NULL;
			sprintf(_temp,"Authorization: Bearer %s",_accessToken);
			_headers = curl_slist_append(_headers, _temp);
			curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

			curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, "POST");

			__initDownloadToLocalIoBuffer();
			CURLcode curlResult = curl_easy_perform(_curlCtx);

			// cleanup
			curl_slist_free_all(_headers);
			curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, NULL);

			if ( curlResult != CURLE_OK )
			{
				__log("cDropBoxHandler::getAccountInfo() [QUOTA] curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
				__log(gDownloadBuffer);
			}
			else
			{
				__log("cDropBoxHandler::getAccountInfo() [QUOTA] response length:%d",gDownloadCursor);

				char* buf = gDownloadBuffer;
				getJSON_int64(buf,"\"used\":",_quotaUsed);
				getJSON_int64(buf,"\"allocated\":",_quotaLimit);
			}

			// finished ...
			return checkLicence();
		}
	}
	return false;
#endif
}

// globals

cDosHandler* gDosHandlerFactory()
{
	return new cDropBoxHandler();
}

const char* gVolumeName()
{
	return "DropBox";
}

const char* gTaskHandlerName()
{
	return "DropBox-Task-Handler";
}

//

#ifdef PLATFORM_WINDOWS

#ifdef _WIN32
# include "crtdbg.h"
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
	_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG)|_CRTDBG_LEAK_CHECK_DF);
#endif

	// test
	curl_global_init(CURL_GLOBAL_DEFAULT);

	sTask _task;
	FileHandle _handle;
	FileLock _lock;

	memset(&_task,0,sizeof(sTask));
	memset(&_handle,0,sizeof(FileHandle));
	memset(&_lock,0,sizeof(FileLock));

	cDosHandler* dbh = gDosHandlerFactory();
	dbh->init();

	dbh->__readCodePage("windows-1250");
	dbh->__selectCodePage("windows-1250");
	dbh->auth();

	const char* ver = curl_version();

	if ( !dbh->isAuthed() )
	{
		delete dbh;
		curl_global_cleanup();
		return 0;
	}
	else
	{
		dbh->getAccountInfo();
	}

	// benchmark test
	_task.set(sTask::ttLog,""); // dummy

	dbh->onHandlerCommand("@benchmark");

	/*
	//
	// create dir test
	//

	_task.set(sTask::ttLocateObject, "temp", 0, -2 );
	dbh->__lock(_task);
	int _lock1 = _task._result;
	_task.set(sTask::ttCreateDir, "f lder1", _lock1 );
	dbh->__createdir(_task);
	*/

	/*
	//
	// rename test
	//
	_task.set(sTask::ttLocateObject, "temp", 0, -2 );
	dbh->__lock(_task);
	int _lock1 = _task._result;
	_task.set(sTask::ttLocateObject, "temp", 0, -2 );
	dbh->__lock(_task);
	int _lock2 = _task._result;
	_task.set(sTask::ttRename, "me.png", _lock1, (int)"me2.png", _lock2 );
	dbh->__rename(_task);
	*/

	/*
	//
	// upload test
	//
	char* _buf = NULL;
	int _len = 0;
	__loadFile("c:/!/data.bin",_buf,_len);
	char* _buffer = _buf;
	_task.set(sTask::ttFind, "data.bin", (int)&_handle, 0, MODE_NEWFILE );
	dbh->__open(_task);
	cDosFile* file = (cDosFile*) _handle.fh_Args;
	if ( file )
	{
		int _ofs = 0;
		while ( _len )
		{
			if ( _len < NETWORK_BUF_LEN )
			{ // final chunk
				_task.set(sTask::ttWrite, "", (int)file, (int)_buffer+_ofs, _len );
				_len = 0;
			}
			else
			{
				_task.set(sTask::ttWrite, "", (int)file, (int)_buffer+_ofs, NETWORK_BUF_LEN );
				_len -= NETWORK_BUF_LEN;
				_ofs += NETWORK_BUF_LEN;
			}
			dbh->__write(_task);
		}
		dbh->__close(_task);
		free(_buf);
	}
	*/

	/*
	// list test
	_task.set(sTask::ttLocateObject, ":", 0, -2 );
	dbh->__lock(_task);
	*/

	/*
	// download test
	_task.set(sTask::ttFind, "elite2d.rar", (int)&_handle, 0, MODE_OLDFILE );
	dbh->__open(_task);
	u8 _buffer[512];
	_task.set(sTask::ttRead, "", (int)(_handle.fh_Arg1), (int)_buffer, 512 );
	dbh->__read(_task);
	*/

	/*
	// benchmark test
	sTask _task;
	_task.set(sTask::ttLog,""); // dummy

	gdh->onHandlerCommand("@benchmark");
	*/

	/*
	//
	// delete test
	//
	_task.set(sTask::ttDelete,"folder2/Copy of handler45", 0 );
	gdh->__delete(_task);
	*/

	/*
	_task.set(sTask::ttFind,"whdload/3ddemo/disk.1", (int)&fh, (int)&gdh->_root, MODE_OLDFILE );
	gdh->__open(_task);

	char _buf[901120];
	memset(_buf,0,901120);

	_task.set(sTask::ttRead,"", (int)fh.fh_Args, (int)_buf, 901120 );
	gdh->__read(_task);
	*/

	/*
	//
	// export test
	//
	_task.set(sTask::ttFind,"udb", (int)&fh, (int)&gdh->_root, MODE_OLDFILE );
	gdh->__open(_task);

	//_task.set(sTask::ttSeek,"", (int)fh.fh_Args, 0, 0 );
	//gdh->__seek(_task);

	FILE* file = fopen("export.bin","wb");
	_task._result = 1;
	while ( _task._result )
	{
		_task.set(sTask::ttRead,"", (int)fh.fh_Args, (int)gWriteBuffer, 200000 );
		gdh->__read(_task);
		if ( _task._result > 0 )
		{
			fwrite(gWriteBuffer,1,_task._result,file);
		}
	}
	fclose(file);
	*/

	/*
	_task.set(sTask::ttLocateObject, "folder2", 0, -2 );
	gdh->__lock(_task);
	_task.set(sTask::ttLocateObject, "folder2", 0, -2 );
	gdh->__lock(_task);
	int _lock = _task._result;
	_task.set(sTask::ttRename, " ^WB^:handler24", _lock, (int)"handler25", _lock );
	gdh->__rename(_task);

	_task.set(sTask::ttFreeLock, "", _lock, -2 );
	gdh->__unlock(_task);
	_task.set(sTask::ttFreeLock, "", _lock, -2 );
	gdh->__unlock(_task);
	*/

	/*
	//_task.set(sTask::ttLocateObject, "me.png", (int)&gdh->_root, -2 );
	//gdh->__lock(_task);
	_task.set(sTask::ttFind, "me.png", (int)&fh, (int)&gdh->_root, MODE_OLDFILE );
	gdh->__open(_task);
	_task.set(sTask::ttRead, "", (int)file, (int)_buffer, 512 );
	gdh->__read(_task);
	*/

	/*
	gdh->__checkFolder(&gdh->_root);

	FILE* f = fopen("devs/.cache/picsafust","wb");
	gdh->downloadFileStream(
		"https://docs.google.com/spreadsheets/export?id=1_l9oZY5UvAFc9aD-2b1LjmMgWIWMjXyxYH2VvssRN-A&exportFormat=pdf",f);
	fclose(f);

	_task.set(sTask::ttFind,"udb", (int)&fh, (int)&gdh->_root, MODE_OLDFILE );
	gdh->__open(_task);

	_task.set(sTask::ttSeek,"", (int)fh.fh_Args, 0, 0 );
	gdh->__seek(_task);

	FILE* file = fopen("dump.bin","wb");
	_task._result = 1;
	while ( _task._result )
	{
		_task.set(sTask::ttRead,"", (int)fh.fh_Args, (int)responseBuffer, 8000 );
		gdh->__read(_task);
		if ( _task._result > 0 )
		{
			fwrite(responseBuffer,1,_task._result,file);
		}
	}
	fclose(file);

	// cache dump
	cGoogleFile* _f = gdh->_fileList[4];
	int len = _f->_exportedFileSize;
	FILE* file = fopen("dump.bin","wb");
	for(u32 _i=0;_i<_f->_cache.size();_i++)
	{
		if ( len >= NETWORK_IO_BUF_LEN )
		{
			fwrite(_f->_cache[_i],1,NETWORK_IO_BUF_LEN,file);
		}
		else
		{
			fwrite(_f->_cache[_i],1,len,file);
		}
		len -= NETWORK_IO_BUF_LEN;
	}
	fclose(file);

	_task.set(sTask::ttEnd,"", (int)fh.fh_Args );
	gdh->__close(_task);

	_task.set(sTask::ttDelete,"handler.log", int(&gdh->_root) );
	gdh->__delete(_task);
	_task.set(sTask::ttLocateObject,"folder1", int(&gdh->_root) );
	gdh->__lock(_task);
	FileInfoBlock fib;
	memset(&fib,0,sizeof(FileInfoBlock));
	_task.set(sTask::ttLocateObject,"folder1",int(&gdh->_root),(int)&fib);
	gdh->__examine(_task);
	gdh->getFileList("root");
	gdh->deleteFile("0B-anzhK6p1uRdm0wQ21aR0Uyb2M");
	*/

	delete dbh;

	curl_global_cleanup();

	return 0;
}

#else

// ...

#endif
