// google_drive_handler.cpp - anchor

// todo

// bugfixes:
// - delete #? a root-ot törli...
// - OS4-en DirOpus 4.18.29 nem tud torolni googledrive:-bol.
// if no internet at startup, retry & self-close properly
// codepage setting (in mountfile)
// change detection
// OCR

// extra functions
//////////////////
//
// 64bit actions
//  ACTION_SEEK64
//  ACTION_QUERY_ATTR
//  ACTION_EXAMINE_OBJECT64
//  ACTION_EXAMINE_NEXT64
//  ACTION_EXAMINE_FH64
//  ACTION_GET_DISK_FSSM
//  ACTION_FREE_DISK_FSSM
// *** ACTION_UNKNOWN *** 15728641 (0xf00001)
//
//  change detection (largestChangeId) GET https://www.googleapis.com/drive/v2/changes?pageToken=[largestChangeId-plus-one]
//         doc: https://developers.google.com/drive/web/manage-changes
//  cache datafiles first 512byte for 'wb peek'
//	icon ghosting (where png is valid icon format)
//  detect changes
//	shared folder support
//  gzip/deflate support (for content listing) (curl can handle it)
//  listing doc: https://developers.google.com/drive/v2/reference/files/list

// history

// 0.8 multiple accounts
// 0.7 benchmark feature
// 0.6 os4 support
// 0.5 unmount support
// 0.4 first release

// benchmark results (kB/s)
// 68020@28   ul: 21  dl: 10
// 68030@50   ul: 38  dl: 18
// 68060@50   ul:139  dl: 113
// 68060@66   ul:139  dl: 150
// Apollo     ul:179  dl:114
// MOS 1,5GHz ul:500  dl:800

// openssl connect test:
// ---------------------
// openssl s_client -connect host.com:443

// gd delete issue link:
// https://code.google.com/a/google.com/p/apps-api-issues/issues/detail?id=4166&thanks=4166&ts=1448220109

#include "google_drive_handler.h"

//
// cGoogleDriveHandler class
//

cGoogleDriveHandler::cGoogleDriveHandler()
{
	_ocr = false;
}

cGoogleDriveHandler::~cGoogleDriveHandler()
{
}

void cGoogleDriveHandler::init()
{
	_version = "$VER: google-drive-handler v0.8 (" __DATE__ ")";
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
	_accessTokenFile = _home + "google_drive_access_token";
	_refreshTokenFile = _home + "google_drive_refresh_token";
	_clientCodePath = _home + "google_drive_client_code";
	_codePagesPath = _home + ".codepages";
	_keyfilePath = _home + "keyfile";
	_replaceDescriptor = ":|";

	cDosHandler::init();
}

void cGoogleDriveHandler::onHandlerCommand(const stringc& pStr)
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

		const void* ptrs[3];
		ptrs[0] = (const char*)(_ocr?"on":"off");
		ptrs[1] = (const char*)(_ocr?"off":"on");
		ptrs[2] = NULL;

		int answer = EasyRequestArgs(NULL, &myES, NULL, ptrs);

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
}

void cGoogleDriveHandler::auth(bool reAuth)
{
	/*
	// facebook login
	curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://graph.facebook.com/v2.6/device/login");
	sprintf(_temp,"access_token=381869662275283|c4642b6d2b95bcfb8bc1c461b2544d3c&scope=public_profile,user_friends,email,user_posts,publish_actions");
	curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _temp);

	// facebook check login
	curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://graph.facebook.com/v2.6/device/login_status");
	sprintf(_temp,"access_token=381869662275283|c4642b6d2b95bcfb8bc1c461b2544d3c&code=39d1a8b0af4e6ca0f6d2605abb4f73b2");
	curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _temp);

	// facebook user info
	curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://graph.facebook.com/v2.11/me/feed");
	sprintf(_temp,"fields=name,picture&access_token=EAAFbTvWSBtMBAARvGPaFdt0aZBCfmcnG8f3ELVU9tVzrrCNA9lTW9tFTYRKUKhfJmfSDmOLZAGQoCZCHVhYUsITIe5oKkqvZBzRCpEh2nUZBDSU6wQ3D3ZCS97D4pgN9p0drgkfboJVAFt6Bf94V9GqWDEdGm8hBwZD");
	curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _temp);

	__initDownloadToLocalIoBuffer(); // setup io buffer
	CURLcode _curlResult = curl_easy_perform(_curlCtx);

	__log(gDownloadBuffer);

	exit(0);
	*/

#ifdef OFFLINE_SIMULATION
	_authed = true;
#else

	__log("cGoogleDriveHandler::auth() %s...", reAuth ? "reauthenticating" : "starting");

	stringc atKey("\"access_token\"");
	stringc rtKey("\"refresh_token\"");

	int _len = 0;

	if ( !reAuth )
	{
		__loadFile(_clientCodePath.c_str(),_clientCode,_len);
		if ( !_clientCode )
		{
			stringc _err = "cGoogleDriveHandler::auth() no client code found";
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

		__loadFile(_accessTokenFile.c_str(),_accessToken,_len);
		__loadFile(_refreshTokenFile.c_str(),_refreshToken,_len);
	}

	if ( !_accessToken && !_refreshToken ) // first time?
	{
		curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://accounts.google.com/o/oauth2/token");
		//curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://www.googleapis.com/oauth2/v4/token");
		// get new tokens
		sprintf(_temp,"code=%s&redirect_uri=%s&client_id=%s&scope=%s&client_secret=%s&grant_type=authorization_code",
			_clientCode, REDIRECT_URI, CLIENT_ID, CLIENT_SCOPE, CLIENT_SECRET );
	}
	else
	{
		curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://www.googleapis.com/oauth2/v1/tokeninfo");
		// examine token
		sprintf(_temp,"access_token=%s",_accessToken);
	}

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
		sprintf(_temp,"cGoogleDriveHandler::auth() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
		__log(_temp);
		messageBox(_temp);
	}
	else
	{
		stringc response(gDownloadBuffer);

		if ( !_accessToken && !_refreshToken ) // first time?
		{
			if ( response.find(atKey.c_str()) != -1 && response.find(rtKey.c_str()) != -1  )
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

				// extract refresh token

				ofs = response.find(rtKey.c_str());
				ofs += rtKey.size();
				while ( response[ofs] != '\"' )
				{
					ofs++;
				}
				ofs++; // skip "
				stringc refreshToken;
				ptr = (char*)(response.c_str()+ofs);
				__extractAsString(ptr,refreshToken,'\"');

				// save tokens

				cFileWriter* writer = new cFileWriter(_accessTokenFile.c_str());
				if ( writer->isValid() )
				{
					writer->saveRaw((void*)accessToken.c_str(),accessToken.size());
					delete writer;
				}

				writer = new cFileWriter(_refreshTokenFile.c_str());
				if ( writer->isValid() )
				{
					writer->saveRaw((void*)refreshToken.c_str(),refreshToken.size());
					delete writer;
				}

				// load tokens

				__loadFile(_accessTokenFile.c_str(),_accessToken,_len);
				__loadFile(_refreshTokenFile.c_str(),_refreshToken,_len);
			}
			else
			{
				sprintf(_temp,"cGoogleDriveHandler::auth() failed (first time login)\n%s",gDownloadBuffer);
				__log(_temp);
				messageBox(_temp);
			}
		}
		else // use existing tokens
		{
			if ( response.find("invalid_token") != -1 ) // expired?
			{
				// ask new token
				curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://accounts.google.com/o/oauth2/token");
				sprintf(_temp,"client_secret=%s&grant_type=refresh_token&refresh_token=%s&client_id=%s",
					CLIENT_SECRET, _refreshToken, CLIENT_ID );

				curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _temp);

				__initDownloadToLocalIoBuffer(); // setup io buffer
				CURLcode curlResult = curl_easy_perform(_curlCtx);

				if ( curlResult != CURLE_OK )
				{
					sprintf(_temp,"cGoogleDriveHandler::auth() curl_easy_perform() failed: %s\n%s",curl_easy_strerror(curlResult),gDownloadBuffer);
					__log(_temp);
					messageBox(_temp);
				}
				else
				{
					stringc response(gDownloadBuffer);

					if ( response.find(atKey.c_str()) != -1 )
					{
						getExpirationData((char*)response.c_str());

						// extract new access token

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

						// re-load token

						free(_accessToken);
						_accessToken = NULL;
						__loadFile(_accessTokenFile.c_str(),_accessToken,_len);
					}
					else // failed
					{
						sprintf(_temp,"cGoogleDriveHandler::auth() failed\n%s",gDownloadBuffer);
						__log(_temp);
						messageBox(_temp);
					}
				}
			}
			else
			{
				getExpirationData(gDownloadBuffer);
				_authed = true;
			}
		}
	}

	if ( _accessToken && _refreshToken ) // ready to work?
	{
		if ( !_authed ) // new token?
		{
			// auth with actual token

			curl_easy_setopt(_curlCtx, CURLOPT_URL, "https://www.googleapis.com/oauth2/v1/tokeninfo");

			sprintf(_temp,"access_token=%s",_accessToken);
			curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _temp);

			__initDownloadToLocalIoBuffer(); // setup io buffer
			CURLcode curlResult = curl_easy_perform(_curlCtx);

			if ( curlResult != CURLE_OK )
			{
				sprintf(_temp,"cGoogleDriveHandler::auth() curl_easy_perform() failed: %s\n%s",curl_easy_strerror(curlResult),gDownloadBuffer);
				__log(_temp);
				messageBox(_temp);
			}
			else
			{
				stringc response(gDownloadBuffer);

				if ( response.find("invalid_token") == -1 ) // valid?
				{
					getExpirationData((char*)response.c_str());

					_authed = true;
					__log("cGoogleDriveHandler::auth() success");
				}
				else
				{
					sprintf(_temp,"cGoogleDriveHandler::auth() invalid token\n%s",gDownloadBuffer);
					__log(_temp);
					messageBox(_temp);
				}
			}
		}
	}
#endif
}

bool cGoogleDriveHandler::isAuthed()
{
	if ( _authed )
	{
#ifdef PLATFORM_AMIGA
		struct DateStamp _expiresAt;
		DateStamp(&_expiresAt);
		long long _currentTime = (86400*_expiresAt.ds_Days)+(_expiresAt.ds_Minute*60)+(_expiresAt.ds_Tick/50);
		if ( _tokenExpiresAt <= _currentTime )
		{
			_authed = false;
			auth(true);
		}
#endif
	}
	return _authed;
}

bool cGoogleDriveHandler::getFileList(const stringc& pFolderId)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() )
	{
		__log("cGoogleDriveHandler::getFileList() invoked");

		stringc _pageToken;
		int filesPerPage = NETWORK_BUF_LEN/4096;

		stringc _folderId;
		if ( pFolderId == "root" )
		{
			_folderId = "+and+'root'+in+parents";
		}
		else if ( pFolderId.size() )
		{
			_folderId = "+and+'";
			_folderId += pFolderId;
			_folderId += "'+in+parents";
		}

		do 
		{
			if ( _pageToken.size() )
			{
				_pageToken = (stringc("&pageToken=") + _pageToken);
			}
			sprintf(_temp,"https://www.googleapis.com/drive/v2/files?maxResults=%d%s&q=trashed%%3Dfalse%s&key=%s",
				filesPerPage,_pageToken.c_str(),_folderId.c_str(),API_KEY);
			_pageToken = "";
			curl_easy_setopt(_curlCtx, CURLOPT_URL, _temp);
			curl_easy_setopt(_curlCtx, CURLOPT_POST, 0);

			struct curl_slist *_headers = NULL;
			sprintf(_temp,"Authorization: Bearer %s",_accessToken);
			_headers = curl_slist_append(_headers, _temp);

			curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

			__initDownloadToLocalIoBuffer(); // setup io buffer
			CURLcode curlResult = curl_easy_perform(_curlCtx);

			curl_slist_free_all(_headers);

			if ( curlResult != CURLE_OK )
			{
				__log("cGoogleDriveHandler::getFileList() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
				__log(gDownloadBuffer);
			}
			else
			{
				__log("cGoogleDriveHandler::getFileList() response length:%d",gDownloadCursor);

				// process page header
				if ( !strstr(gDownloadBuffer,"\"kind\": \"drive#fileList\",") )
				{
					__log("cGoogleDriveHandler::getFileList() list error");
					return false;
				}

				char* _cursor = gDownloadBuffer;

				getJSON_string(_cursor,"\"nextPageToken\":",_pageToken);

				if ( !_pageToken.size() )
				{
					_cursor = gDownloadBuffer; // seek back to start
				}

				processPage(_cursor,gDownloadCursor);
			}
		} while ( _pageToken.size() );
		return true;
	}
	return false;
#endif
}

void cGoogleDriveHandler::processPage(char* pBuf, u32 pLen)
{
	char* buf = pBuf;

	jumpToKey(buf,"\"items\":");

	//stringc _temp;

	// get file start poisitions for file pieces
	array<char*> _fileStartPosList;
	bool _found = true;
	while ( _found )
	{
		jumpToKey(buf,"\"kind\": \"drive#file\",");
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
	array<stringc> _fileItems;
	for(u32 _i=0;_i<_fileStartPosList.size();_i++)
	{
		_fileItems.push_back(stringc(_fileStartPosList[_i]));
		//__log("item length: %d",_fileItems.getLast().size());
	}
	*/

	for(u32 _i=0;_i<_fileStartPosList.size();_i++)
	{
		buf = (char*) _fileStartPosList[_i];

		cDosFile* _file = new cDosFile();
		_fileList.push_back(_file);

		processFile(buf,_file);

		__log("cGoogleDriveHandler::processPage() #%d: title:'%s' [%s] id:'%s' size:%d ptr:%08x",
			_fileList.size(),_file->_title.c_str(),_file->_folder?"folder":"file",
			_file->_id.c_str(),_file->_fileSize,u32(_file));
	}
}

void cGoogleDriveHandler::processFile(char* pBuf, cDosFile* pFile)
{
	char* bufSave = pBuf;
	stringc _tempStr;

	// read always presented fields 

	getJSON_string(pBuf,"\"id\":",pFile->_id);
	getJSON_string(pBuf,"\"iconLink\":",pFile->_iconLink);
	getJSON_string(pBuf,"\"title\":",_tempStr);

	__convertFromUTF8(_tempStr,pFile->_title);
	pFile->_titleLC = pFile->_title;
	pFile->_titleLC.make_lower();

	getJSON_string(pBuf,"\"mimeType\":",_tempStr);
	if ( _tempStr == FOLDER_MIME_TYPE )
	{
		pFile->_folder = true;
	}
	getJSON_string(pBuf,"\"modifiedDate\":",pFile->_modDate);
	getJSON_string(pBuf,"\"parentLink\":",_tempStr);
	bool _isRoot = false;
	getJSON_bool(pBuf,"\"isRoot\":",_isRoot);
	if ( !_isRoot )
	{
		pFile->_parentId = _tempStr;
		array<stringc> _pieces;
		pFile->_parentId.split(_pieces,"/");
		pFile->_parentId = _pieces.getLast(); // only the id kept
	}

	// read optional fields

	pBuf = bufSave;
	getJSON_int(pBuf,"\"fileSize\":",pFile->_fileSize);

	pBuf = bufSave;
	getJSON_string(pBuf,"\"downloadUrl\":",pFile->_downloadURL);

	pBuf = bufSave;
	getJSON_stringArray(pBuf,"\"exportLinks\":",pFile->_exportURLs);
}

/*
bool cGoogleDriveHandler::deleteFile(const stringc& pId)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pId.size() )
	{
		sprintf(_temp,"https://www.googleapis.com/drive/v2/files/%s",pId.c_str());
		curl_easy_setopt(_curlCtx, CURLOPT_URL, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_POST, 0);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, "DELETE");

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);

		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		gWriteCursor = 0;
		//memset(gWriteBuffer,0,gNetworkIOBufferLength);
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		curl_slist_free_all(_headers);

		if ( curlResult != CURLE_OK )
		{
			__log("cGoogleDriveHandler::deleteFile() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gWriteBuffer);
		}
		else
		{
			__log("cGoogleDriveHandler::deleteFile() response length:%d",gWriteCursor);

			return true;
		}
	}
	return false;
#endif
}
*/

bool cGoogleDriveHandler::downloadFile(const stringc& pFile, int pStart, int pCount, u8* pBuffer)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile.size() )
	{
		curl_easy_setopt(_curlCtx, CURLOPT_URL, pFile.c_str());
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
			__log("cGoogleDriveHandler::downloadFileExact() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			__log("cGoogleDriveHandler::downloadFileExact() response length:%d",gDownloadCursor);

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

bool cGoogleDriveHandler::exportFile(const stringc& pFileURL, cDosFile* pFile, cFileOpen* pHandle)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile )
	{
		//pFile->_cachedFileSize = 0;

		//__log("cGoogleDriveHandler::exportFile() url:%s file:%08x",pFile.c_str(),pShadowFile);

		curl_easy_setopt(_curlCtx, CURLOPT_URL, pFileURL.c_str());
		curl_easy_setopt(_curlCtx, CURLOPT_POST, 0);

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		__initDownloadToFileBuffer(pHandle);
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		curl_slist_free_all(_headers);

		if ( curlResult != CURLE_OK )
		{
			__log("cGoogleDriveHandler::downloadFileStream() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			pHandle->_cachedFileSize = pHandle->_openCursor;
			__log("cGoogleDriveHandler::downloadFileStream() file length:%d",pHandle->_cachedFileSize);
			return true;
		}
	}
	return false;
#endif
}

bool cGoogleDriveHandler::createDirectory(cDosFile* pFile)
{
	return beginUpload(pFile);
}

bool cGoogleDriveHandler::beginUpload(cDosFile* pFile)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile )
	{
		//
		// creating file
		//

		sprintf(_temp,"https://www.googleapis.com/drive/v2/files?key=%s",API_KEY);
		curl_easy_setopt(_curlCtx, CURLOPT_URL, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_POST, 1);

		stringc _title;
		__convertToUTF8(pFile->_title,_title);

		if ( pFile->_folder )
		{
			sprintf(_iobuf,"{\n \"title\": \"%s\",\n \"parents\": [{\"id\":\"%s\"}],\n \"mimeType\": \"%s\"\n}",
				_title.c_str(),pFile->_parentId.size()?pFile->_parentId.c_str():"root",FOLDER_MIME_TYPE);
		}
		else
		{
			sprintf(_iobuf,"{\n \"title\": \"%s\",\n \"parents\": [{\"id\":\"%s\"}]\n}",
				_title.c_str(),pFile->_parentId.size()?pFile->_parentId.c_str():"root");
		}
		//stringc _post(_temp);
		long clen = strlen(_iobuf);
		curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _iobuf );
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, clen);

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		_headers = curl_slist_append(_headers, "Content-Type: application/json");

		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		//gReadFile = NULL; // turn off file-upload
		//gReadSize = clen;

		//gReadCursor = gWriteCursor = 0; 
		//memset(gWriteBuffer,0,gNetworkIOBufferLength);
		__initDownloadToLocalIoBuffer();
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);
		//curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)-1);

		if ( curlResult != CURLE_OK )
		{
			__log("cGoogleDriveHandler::beginUpload() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			/*
			__log("cGoogleDriveHandler::uploadFile() created");
			__log(gDownloadBuffer);
			*/

			char* buf = gDownloadBuffer;
			processFile(buf,pFile);
			return true;
		}
	}
	return false;
#endif
}

bool cGoogleDriveHandler::uploadFile(cDosFile* pFile, cFileOpen* pHandle, bool pFinal)
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile && pHandle )
	{
		//
		// upload data
		//

		sprintf(_temp,"https://www.googleapis.com/upload/drive/v2/files/%s",pFile->_id.c_str());
		curl_easy_setopt(_curlCtx, CURLOPT_URL, _temp );
		curl_easy_setopt(_curlCtx, CURLOPT_PUT, 1);
		curl_easy_setopt(_curlCtx, CURLOPT_UPLOAD, 1);

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		_headers = curl_slist_append(_headers, "Content-Type: application/octet-stream");

		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		//gReadFile = pHandle; // turn on file-upload
		//gReadSize = pHandle->_cachedFileSize;
		curl_easy_setopt(_curlCtx, CURLOPT_READFUNCTION, rdfu);
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)pHandle->_cachedFileSize);

		//__log("cGoogleDriveHandler::uploadFile() length:%d segments:%d",pHandle->_cachedFileSize,pHandle->_cache.size());

		//gReadCursor = gWriteCursor = 0; 
		//memset(gWriteBuffer,0,gNetworkIOBufferLength);
		__initUploadFromFileBuffer(pHandle);
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);
		//gReadFile = NULL; // turn off file-download
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)-1);
		curl_easy_setopt(_curlCtx, CURLOPT_READFUNCTION, 0);
		curl_easy_setopt(_curlCtx, CURLOPT_UPLOAD, 0);
		curl_easy_setopt(_curlCtx, CURLOPT_PUT, 0);

		if ( curlResult != CURLE_OK )
		{
			__log("cGoogleDriveHandler::uploadFile() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			/*
			__log("cGoogleDriveHandler::uploadFile() uploaded");
			__log(gDownloadBuffer);
			*/

			pFile->_fileSize = pHandle->_cachedFileSize;
			return true;
		}
	}
	return false;
#endif
}

bool cGoogleDriveHandler::updateFile(cDosFile* pFile, bool pNameAndParent, bool pDate, bool pTrashed)
{
	/*
	// delete OFF
	if ( pTrashed )
	{
		return true;
	}
	//
	*/

#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() && pFile )
	{
		//
		// update file metadata
		//

		sprintf(_temp,"https://www.googleapis.com/drive/v2/files/%s?key=%s",pFile->_id.c_str(),API_KEY);
		curl_easy_setopt(_curlCtx, CURLOPT_URL, _temp);
		//curl_easy_setopt(_curlCtx, CURLOPT_PUT, 1);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, "PATCH");

		// generate payload
		if ( pNameAndParent )
		{
			sprintf(_iobuf,"{\n \"id\":\"%s\",\n \"title\": \"%s\",\n \"parents\": [{\"id\":\"%s\"}]\n}",
				pFile->_id.c_str(),pFile->_title.c_str(),pFile->_parentId.size()?pFile->_parentId.c_str():"root");
		}
		else if ( pTrashed )
		{
			sprintf(_iobuf,"{\n \"id\":\"%s\",\n \"labels\": {\n  \"trashed\": true\n }\n}",pFile->_id.c_str());
		}
		else if ( pDate )
		{
			// "modifiedDate": "2015-11-26T22:38:16.964Z"
		}
		else
		{
			return false;
		}

		curl_easy_setopt(_curlCtx, CURLOPT_POSTFIELDS, _iobuf); /* data goes here */
		long clen = strlen(_iobuf);

		struct curl_slist *_headers = NULL;
		_headers = curl_slist_append(_headers, "Content-Type: application/json");
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)clen);

		//gReadCursor = gWriteCursor = 0; 
		//memset(gWriteBuffer,0,gNetworkIOBufferLength);
		__initDownloadToLocalIoBuffer();
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, NULL);
		curl_easy_setopt(_curlCtx, CURLOPT_INFILESIZE, (long)-1);

		if ( curlResult != CURLE_OK )
		{
			__log("cGoogleDriveHandler::updateFile() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			if ( !strstr(gDownloadBuffer,"\"error\":") )
			{
				if ( !pTrashed )
				{
					char* buf = gDownloadBuffer;
					processFile(buf,pFile);
				}
				return true;
			}
			else
			{
				__log("cGoogleDriveHandler::updateFile() failed: %s",gDownloadBuffer);
			}
		}
	}
	return false;
#endif
}

bool cGoogleDriveHandler::getAccountInfo()
{
#ifdef OFFLINE_SIMULATION
	return true;
#else
	if ( isAuthed() )
	{
		//
		// get google drive account info
		//

		sprintf(_temp,"https://www.googleapis.com/drive/v2/about?key=%s",API_KEY);
		curl_easy_setopt(_curlCtx, CURLOPT_URL, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_UPLOAD, 0);

		struct curl_slist *_headers = NULL;
		sprintf(_temp,"Authorization: Bearer %s",_accessToken);
		_headers = curl_slist_append(_headers, _temp);
		curl_easy_setopt(_curlCtx, CURLOPT_HTTPHEADER, _headers);

		//gReadCursor = gWriteCursor = 0; 
		//memset(gWriteBuffer,0,gNetworkIOBufferLength);
		__initDownloadToLocalIoBuffer();
		CURLcode curlResult = curl_easy_perform(_curlCtx);

		// cleanup
		curl_slist_free_all(_headers);
		curl_easy_setopt(_curlCtx, CURLOPT_CUSTOMREQUEST, NULL);

		if ( curlResult != CURLE_OK )
		{
			__log("cGoogleDriveHandler::getAccountInfo() curl_easy_perform() failed: %s",curl_easy_strerror(curlResult));
			__log(gDownloadBuffer);
		}
		else
		{
			__log("cGoogleDriveHandler::getAccountInfo() response length:%d",gDownloadCursor);

			char* buf = gDownloadBuffer;
			getJSON_string(buf,"\"emailAddress\":",_accountMailAddress);
			getJSON_int64(buf,"\"quotaBytesTotal\":",_quotaLimit);
			getJSON_int64(buf,"\"quotaBytesUsed\":",_quotaUsed);

			return checkLicence();
		}
	}
	return false;
#endif
}

// globals

cDosHandler* gDosHandlerFactory()
{
	return new cGoogleDriveHandler();
}

const char* gVolumeName()
{
	return "GoogleDrive";
}

const char* gTaskHandlerName()
{
	return "Google-Task-Handler";
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

	cDosHandler* gdh = gDosHandlerFactory();
	gdh->init();

	gdh->__readCodePage("windows-1250");
	gdh->__selectCodePage("windows-1250");
	gdh->auth();

	//const char* ver = curl_version();

	if ( !gdh->isAuthed() )
	{
		return 0;
	}
	else
	{
		gdh->getAccountInfo();
	}

	sTask _task;
	_task.set(sTask::ttLog,""); // dummy
	FileHandle fh;
	memset(&fh,0,sizeof(FileHandle));

	gdh->getFileList("root");

	//gdh->onHandlerCommand("@benchmark");

	//
	// upload test
	//
	/*
	char* _buf = NULL;
	int _len = 0;
	__loadFile("c:/!/me.png",_buf,_len);
	char* _buffer = _buf;
	_task.set(sTask::ttFind, "folder2/me.png", (int)&fh, 0, MODE_NEWFILE );
	gdh->__open(_task);
	cDosFile* file = (cDosFile*) fh.fh_Args;
	if ( file )
	{
		_task.set(sTask::ttWrite, "", (int)file, (int)_buffer, _len );
		gdh->__write(_task);
		gdh->__close(_task);
	}
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
	FileHandle fh;
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
	_task.set(sTask::ttDelete,"folder2/Copy_of_handler38", 0 );
	gdh->__delete(_task);
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
	sTask _task;

	gdh->__checkFolder(&gdh->_root);

	FILE* f = fopen("devs/.cache/picsafust","wb");
	gdh->downloadFileStream(
		"https://docs.google.com/spreadsheets/export?id=1_l9oZY5UvAFc9aD-2b1LjmMgWIWMjXyxYH2VvssRN-A&exportFormat=pdf",f);
	fclose(f);

	FileHandle fh;
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

	delete gdh;

	curl_global_cleanup();

	return 0;
}

#else

// ...

#endif
