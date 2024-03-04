// dropbox_handler.h - anchor

#ifndef __DROPBOX_HANDLER__H
#define __DROPBOX_HANDLER__H

#include "../shared/dos_handler.h"

#define APP_KEY "YOUR_APP_KEY"
#define APP_SECRET "YOUR_APP_SECRET"
//#define DROPBOX_APIv2

class cDropBoxHandler : public cDosHandler
{
public:
	cDropBoxHandler();
	~cDropBoxHandler();

	virtual void init();
	//virtual void onHandlerCommand(const stringc& pStr);
	virtual void auth(bool reAuth=false);
	virtual bool getFileList(const stringc& pFolderId);

	void processPage(char* pBuf, u32 pLen, const char* pParentId);
	void processFile(char* pBuf, cDosFile* pFile, bool pForceFile);

	// user interactions
	virtual bool downloadFile(const stringc& pFile, int pStart, int pCount, u8* pBuffer);
	virtual bool exportFile(const stringc& pFileURL, cDosFile* pFile, cFileOpen* pHandle) { return false; }
	virtual bool createDirectory(cDosFile* pFile);
	virtual bool beginUpload(cDosFile* pFile);
	virtual bool uploadFile(cDosFile* pFile, cFileOpen* pHandle, bool pFinal);
	virtual bool updateFile(cDosFile* pFile, bool pNameAndParent, bool pDate, bool pTrashed);
	virtual bool getAccountInfo();
};

#endif // __DROPBOX_HANDLER__H

