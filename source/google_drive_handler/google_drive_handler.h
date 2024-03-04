// google_drive_handler.h - anchor

#ifndef __GOOGLE_DRIVE_HANDLER__H
#define __GOOGLE_DRIVE_HANDLER__H

#include "../shared/dos_handler.h"

// auth macros
#define REDIRECT_URI "urn:ietf:wg:oauth:2.0:oob"
#define CLIENT_ID "YOUR_CLIENT_ID"
#define CLIENT_SECRET "YOUR_CLIENT_SECRET"
#define API_KEY "YOUR_API_KEY"
#define CLIENT_SCOPE "https://www.googleapis.com/auth/drive"
#define FOLDER_MIME_TYPE "application/vnd.google-apps.folder"

class cGoogleDriveHandler : public cDosHandler
{
public:
	cGoogleDriveHandler();
	~cGoogleDriveHandler();

	virtual void init();
	virtual void onHandlerCommand(const stringc& pStr);
	virtual void auth(bool reAuth=false);
	virtual bool isAuthed();
	virtual bool getFileList(const stringc& pFolderId);

	void processPage(char* pBuf, u32 pLen);
	void processFile(char* pBuf, cDosFile* pFile);

	// user interactions
	/*
	virtual bool deleteFile(const stringc& pId); // permanent delete is not working for google drive due to a google drive bug!
	*/
	virtual bool downloadFile(const stringc& pFile, int pStart, int pCount, u8* pBuffer);
	virtual bool exportFile(const stringc& pFileURL, cDosFile* pFile, cFileOpen* pHandle);
	virtual bool createDirectory(cDosFile* pFile);
	virtual bool beginUpload(cDosFile* pFile);
	virtual bool uploadFile(cDosFile* pFile, cFileOpen* pHandle, bool pFinal);
	virtual bool updateFile(cDosFile* pFile, bool pNameAndParent, bool pDate, bool pTrashed);
	virtual bool getAccountInfo();

	bool _ocr;
};

#endif // __GOOGLE_DRIVE_HANDLER__H
