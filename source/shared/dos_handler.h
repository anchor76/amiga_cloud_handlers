// dos_handler.h - anchor

#ifndef __DOS_HANDLER__H
#define __DOS_HANDLER__H

#include "fw_util.h"

//#define CURL_DEBUG_OUTPUT
//#define OFFLINE_SIMULATION
//#define CHECK_LOCK

#include <curl/curl.h>
//#pragma comment(lib,"libcurl.lib")
#pragma comment(lib,"crypt32.lib")
#pragma comment(lib,"libeay32.lib")
#pragma comment(lib,"ssleay32.lib")
#pragma comment(lib,"ws2_32.lib")

#ifdef PLATFORM_AMIGA

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/filehandler.h>
#include <dos/notify.h>
#include <dos/exall.h>
#include <exec/memory.h>
#include <exec/initializers.h>
#include <exec/types.h>
#include <utility/utility.h>
#include <intuition/intuition.h>

#include <proto/exec.h>
#include <proto/dos.h>
#ifndef PLATFORM_AMIGAOS4
# include <proto/alib.h>
#else
# include <dos/obsolete.h>
#endif
#include <proto/locale.h>
#include <proto/utility.h>
#include <proto/intuition.h>

#include <clib/debug_protos.h>

#define DOS_VERSION 39 
#else

#define ERROR_NO_MORE_ENTRIES 1
#define ERROR_OBJECT_IN_USE 1
#define ERROR_DIRECTORY_NOT_EMPTY 1
#define ERROR_OBJECT_WRONG_TYPE 1
#define ERROR_DISK_NOT_VALIDATED 1
#define ERROR_WRITE_PROTECTED		  223 
#define ERROR_SEEK_ERROR 666
#define ERROR_OBJECT_EXISTS		  203 
//#define EXCLUSIVE_LOCK 2
#define ST_ROOT 1
#define ST_USERDIR 1
#define ST_FILE 1
#define DOSTRUE 1
#define DOSFALSE 0
#define ID_VALIDATED 1
#define ID_WRITE_PROTECTED 80	 /* Disk is write protected */ 
#define ID_DOS_DISK 1
#define ACCESS_READ 1
#define ACCESS_WRITE 2
#define MODE_NEWFILE 1
#define MODE_READWRITE 2
#define MODE_OLDFILE 3
#define OFFSET_BEGINNING    -1	    /* relative to Begining Of File */
#define OFFSET_CURRENT	     0	    /* relative to Current file position */
#define OFFSET_END	     1	    /* relative to End Of File	  */

struct FileLock
{
	int		fl_Link;	/* bcpl pointer to next lock */
	int		fl_Key;		/* disk block number */
	int		fl_Access;	/* exclusive or shared */
	int		fl_Task;	/* handler task's port */
	int		fl_Volume;	/* bptr to DLT_VOLUME DosList entry */
};

struct DateStamp
{
	LONG	 ds_Days;	      /* Number of days since Jan. 1, 1978 */
	LONG	 ds_Minute;	      /* Number of minutes past midnight */
	LONG	 ds_Tick;	      /* Number of ticks past minute */
};

struct FileInfoBlock 
{
   LONG	  fib_DiskKey;
   LONG	  fib_DirEntryType;  /* Type of Directory. If < 0, then a plain file.
			      * If > 0 a directory */
   char	  fib_FileName[108]; /* Null terminated. Max 30 chars used for now */
   LONG	  fib_Protection;    /* bit mask of protection, rwxd are 3-0.	   */
   LONG	  fib_EntryType;
   LONG	  fib_Size;	     /* Number of bytes in file */
   LONG	  fib_NumBlocks;     /* Number of blocks in file */
   struct DateStamp fib_Date;/* Date file last changed */
   char	  fib_Comment[80];  /* Null terminated comment associated with file */
   UWORD  fib_OwnerUID;		/* owner's UID */
   UWORD  fib_OwnerGID;		/* owner's GID */

   char	  fib_Reserved[32];
};
 
struct InfoData 
{
	LONG	  id_NumSoftErrors;	/* number of soft errors on disk */
	LONG	  id_UnitNumber;	/* Which unit disk is (was) mounted on */
	LONG	  id_DiskState;		/* See defines below */
	LONG	  id_NumBlocks;		/* Number of blocks on disk */
	LONG	  id_NumBlocksUsed;	/* Number of block in use */
	LONG	  id_BytesPerBlock;
	LONG	  id_DiskType;		/* Disk Type code */
	int 	  id_VolumeNode;	/* BCPL pointer to volume node (see DosList) */
	LONG	  id_InUse;		/* Flag, zero if not in use */
};

struct FileHandle 
{
   char *fh_Link;	 /* EXEC message	      */
   char *fh_Port;	 /* Reply port for the packet */
   char *fh_Type;	 /* Port to do PutMsg() to
				  * Address is negative if a plain file */
   LONG fh_Buf;
   LONG fh_Pos;
   LONG fh_End;
   LONG fh_Funcs;
#define fh_Func1 fh_Funcs
   LONG fh_Func2;
   LONG fh_Func3;
   LONG fh_Args;
#define fh_Arg1 fh_Args
   LONG fh_Arg2;
};
 
#endif

// new actions 
#ifndef ACTION_SHUTDOWN
# define ACTION_SHUTDOWN 3000
#endif
#ifndef ACTION_GET_DISK_FSSM
# define ACTION_GET_DISK_FSSM 4201
#endif
#ifndef ACTION_FREE_DISK_FSSM
# define ACTION_FREE_DISK_FSSM 4202
#endif
#ifndef ACTION_SEEK64
# define ACTION_SEEK64 26400
#endif
#ifndef ACTION_QUERY_ATTR
# define ACTION_QUERY_ATTR 26407
#endif
#ifndef ACTION_EXAMINE_OBJECT64
# define ACTION_EXAMINE_OBJECT64 26408
#endif
#ifndef ACTION_EXAMINE_NEXT64
# define ACTION_EXAMINE_NEXT64 26409
#endif
#ifndef ACTION_EXAMINE_FH64
# define ACTION_EXAMINE_FH64 26410
#endif

//
// DOS_HANDLER
//

#define NETWORK_BUF_LEN 65536
#define MAX_LIST_RESULTS 50
#define UPDIR_SIGN 0xf

#define NO_LICENCE_MSG "Please obtain a licence key to use this funtion!\nContact: anchor@rocketmail.com"

#if defined PLATFORM_AROS || defined PLATFORM_AMIGAOS4 || defined PLATFORM_WINDOWS
typedef const char* EASY_TEXT;
#else
typedef UBYTE* EASY_TEXT;
#endif

class cCodePage
{
public:
	cCodePage(const char* pName, const char* pBufPtr, int pBufLen);
	int translateUnicode(int puc, bool toAmiga);
	stringc _name;
	array<u32> _charPairs; // 16:16 bit raplace pairs
};

struct sTask
{
	enum eTaskType
	{
		ttLog,                   // internal
		ttDie,                   // internal
		ttInhibit,               // internal
		ttLocateObject,          // lock()
		ttFreeLock,              // unlock()
		ttCopyDir,               // duplock()
		ttParent,                // parent()
		ttExamineObject,         // examineObject()
		ttInfo,                  // info()
		ttSameLock,              // sameLock()
		ttFind,                  // open() all three mode
		ttEnd,                   // close()
		ttDelete,                // delete()
		ttSeek,                  // seek()
		ttRead,                  // read()
		ttWrite,                 // write()
		ttCreateDir,             // createDir()
		ttRename,                // rename()
		ttCopyDirFH,			 // duplockFromFH()
		ttFHFromLock,			 // openFromLock()
		ttParentFH,              // parentFH()
		ttExamineFH,             // examineFH()

		// todo
		ttExamineAll,            // exAll()
		ttExamineAllEnd,         // exAllEnd()
		//ttChangeMode,            // changeMode()
		ttSeek64,
		ttQueryAttr,
		ttExamineObject64,

		ttCOUNT
	};

	void set(eTaskType ptt, const char* paramStr, int paramInt1=0, int paramInt2=0, int paramInt3=0)
	{
		_type = ptt;
		_int1 = paramInt1;
		_int2 = paramInt2;
		_int3 = paramInt3;
		strcpy(_str,paramStr);
		_result = 0;
		_error = 0;
	}

	eTaskType _type;
	char _str[STR_LEN*2];
	int _int1,_int2,_int3,_result,_error;
	void* _task; // the task which use the handler
};

class cDosFile;

class cFileOpen
{
public:
	enum eOpenMode
	{
		omDownload, // read only. this allows multiple opening. only for exact files!
		omExport,   // export for zero sized document type files
		omUpload,   // upload
		omCOUNT
	};

	cFileOpen(eOpenMode pMode, FileLock* pLock);
	virtual ~cFileOpen();

	void purgeCache();
	int __storeData(u8* pBuf, int pLen); // atomic
	int storeData(u8* pBuf, int pLen);
	int __readData(u8* pBuf, int pLen); // atomic
	int readData(u8* pBuf, int pLen);

	eOpenMode _openMode;
	array<u8*> _cache;          // NETWORK_IO_BUF_LEN sized chunks for upload/download
	int _cachedFileSize;        // content length in cache
	int _openCursor;
	int _userData;
	FileLock* _lock;
};

class cDosFile
{
public:
	cDosFile();
	~cDosFile();

	// service provider related part
	stringc _id;
	stringc _iconLink;
	stringc _title;             // utf8 -> codepaged
	stringc _titleLC;           // lowercased (codepaged)
	stringc _modDate;
	stringc _parentId;          // the id from link
	stringc _downloadURL;       // where is possible (if filesize not zero)
	array<stringc> _exportURLs; // for zero filesized, documents
	bool _folder;
	bool _contentDownloaded;    // (if folder)
	int _fileSize;              // zero for documents
	//stringc _oldTitle,_oldParentId; // used for rename

	// dos handler related
	bool unlock(FileLock* plock);
	bool close(cFileOpen* popen);
	array<cFileOpen*> _opens;
	array<FileLock*> _locks;
};

class cDosHandler
{
public:
	cDosHandler();
	virtual ~cDosHandler();

	virtual void init();
	virtual void onHandlerCommand(const stringc& pStr);
	virtual void auth(bool reAuth=false) = 0;
	virtual bool isAuthed() { return _authed; }
	virtual bool getFileList(const stringc& pFolderId) = 0;

	void getExpirationData(char* buf);
	void jumpToKey(char*& pBuf, const char* pKey);
	bool moveToChar(char*& pBuf, const char pChar); // returns false if JSON item or file end found
	void getJSON_string(char*& pBuf, const char* pKey, stringc& out);
	void getJSON_stringArray(char*& pBuf, const char* pKey, array<stringc>& out);
	void getJSON_int(char*& pBuf, const char* pKey, int& out);
	void getJSON_int64(char*& pBuf, const char* pKey, long long& out);
	void getJSON_bool(char*& pBuf, const char* pKey, bool& out);
	void messageBox(const char* pMsg, const char* pCaption=NULL);
	bool checkLicence();

	// user interactions
	/*
	bool deleteFile(const stringc& pId); // permanent delete is not working for google drive due to a google drive bug!
	*/
	virtual bool downloadFile(const stringc& pFile, int pStart, int pCount, u8* pBuffer) = 0;
	virtual bool exportFile(const stringc& pFileURL, cDosFile* pFile, cFileOpen* pHandle) = 0;
	virtual bool createDirectory(cDosFile* pFile) = 0;
	virtual bool beginUpload(cDosFile* pFile) = 0;
	virtual bool uploadFile(cDosFile* pFile, cFileOpen* pHandle, bool pFinal) = 0;
	virtual bool updateFile(cDosFile* pFile, bool pNameAndParent, bool pDate, bool pTrashed) = 0;
	virtual bool getAccountInfo() = 0;

	cDosFile _root;

	// config 
	stringc _version;
	stringc _volumeName;
	stringc _accessTokenFile;
	stringc _refreshTokenFile;
	stringc _clientCodePath;
	stringc _codePagesPath;
	stringc _keyfilePath;
	/*stringc _cachePath;*/
	bool _appendUploadSupported;
	//

	bool _authed;
	int _expiresIn;
	long long _tokenExpiresAt; // in seconds
	char* _accessToken;
	char* _refreshToken;
	char* _clientCode;
	CURL* _curlCtx;
	char _temp[4096];
	array<cDosFile*> _fileList;
	//bool _isLicenseValid;
	stringc _licenceMailAddress; // read from keyfile
	stringc _licenceName;        // read from keyfile
	stringc _accountMailAddress; // read from service provider
	long long _quotaLimit;
	long long _quotaUsed;
	int _blockSize;

	// code page related
	int _codePageIndex; // -1 = no codepage translation
	array<cCodePage*> _codePages;
	stringc _replaceDescriptor; // from->to char pairs
	void __readCodePage(const char* pName);
	bool __selectCodePage(const char* pName);
	void __translateString(stringw& pText, bool toAmiga); // to amiga, or from amiga
	void __convertFromUTF8(const stringc& pUTF8, stringc& pAmiga);
	void __convertFromEscaped(const stringc& pEscaped, stringc& pAmiga);
	void __convertFromUnicode(stringw& pUnicode, stringc& pAmiga);
	void __convertToUTF8(const stringc& pAmiga, stringc& pUTF8);
	void __convertToEscaped(const stringc& pAmiga, stringc& pEscaped);
	void __repairName(stringc& pAmiga);

	// low level
#ifdef CHECK_LOCK
	bool __CheckLock(cDosFile* pFile);
#endif
	void __makeDateStamp(const stringc& pDateString, struct DateStamp* pDateStamp);
	cDosFile* __getNextObjectWithSameParent(cDosFile* _from); // for examine loop
	cDosFile* __getObject(const stringc& pName, cDosFile* pParent, int& pError); // wrapper
	cDosFile* __getObject(const stringc& pName, cDosFile* pParent, int& pError, cDosFile*& pDirectParentOut);
	cDosFile* __getFirstChild(cDosFile* pFolder); // only for folders!
	bool __checkFolder(cDosFile* pFolder); // downloads content if neccessary
	FileLock* __lockObject(cDosFile* pFile, int mode); // returns NULL if the file is exclusively locked
	bool __deleteObject(cDosFile* pFile, int& pError);
	cDosFile* __createObject(const char* pName, cDosFile* pParentLock, int& pError);
	void __filePart(const stringc& pFullPath, stringc& pFilePart);
	void __examineObject(cDosFile* _file, struct FileInfoBlock* _fib);
	void __incLockCount();
	void __decLockCount();
	bool __isShutdownPossible();
	bool __shutdown();

	// handler actions
	void __lock(sTask& pTask);
	void __unlock(sTask& pTask);
	void __duplock(sTask& pTask);
	void __parent(sTask& pTask);
	void __examine(sTask& pTask);
	void __info(sTask& pTask);
	void __samelock(sTask& pTask);
	void __open(sTask& pTask);
	void __close(sTask& pTask);
	void __delete(sTask& pTask);
	void __seek(sTask& pTask);
	void __read(sTask& pTask);
	void __write(sTask& pTask); // upload only
	void __createdir(sTask& pTask);
	void __rename(sTask& pTask);
	void __duplockFromFH(sTask& pTask);
	void __openFromLock(sTask& pTask);
	void __parentFH(sTask& pTask);
	void __examineFH(sTask& pTask);
	void __exAll(sTask& pTask);
	void __exAllEnd(sTask& pTask);
	//void __changeMode(sTask& pTask);

	int _lockCount; // volume stat

	// io buffer
	char _iobuf[NETWORK_BUF_LEN]; // for general download/upload
	void __initDownloadToLocalIoBuffer();
	void __initDownloadToCustomBuffer(char* buf, int len);
	void __initDownloadToFileBuffer(cFileOpen* pFile);
	void __initUploadFromFileBuffer(cFileOpen* pHandle);

#ifdef PLATFORM_AMIGAOS4
	struct IntuitionIFace *IIntuition;
#endif
};

// globals
extern cDosHandler* gDosHandlerFactory();
extern const char* gVolumeName();
extern const char* gTaskHandlerName();
extern char* gDownloadBuffer;
extern u32 gDownloadCursor;
extern size_t rdfu(void *ptr, size_t size, size_t nmemb, void *stream);

#endif // __DOS_HANDLER__H
