#ifndef SERVER_APPLAYER_H
#define SERVER_APPLAYER_H

#include "datalink_layer.h"
#include <string>
#include <vector>
#include <fstream>
using namespace std;


class ServerApp
{
public:
	static void *SAPCreate(void* thisPtr) { reinterpret_cast<ServerApp*>(thisPtr)->startControlLoop();; return NULL; };
	ServerApp(void* dlPtr);
	
	typedef enum {eLogin = 0x01, eLogout = 0x02, eList = 0x03, eDownloadStart = 0x04, eDownloadPause = 0x05, eDownloadResume = 0x06, eDownloadCancel = 0x07,
					  eUploadStart = 0x08, eUploadPause = 0x09, eUploadResume = 0x0A, eUploadCancel = 0x0B, eRemove = 0x0C, eData = 0x0D,
					  eUploadComplete = 0x0E} Command_t; 
					  
	enum {eLoginSuccess = 0x00, eInvalidUserName = 0x01, eInvalidPassword = 0x02, eUserLoggedIn =0x03, eUserNotLoggedIn};
	enum {eDownloadStartSuccess = 0x00, eInvalidFileName = 0x01, eDownloadPending};
	enum {eDownloadPauseSuccess = 0x00, eDownloadPauseError = 0x01};
	enum {eDownloadResumeSuccess = 0x00, eDownloadResumeInvalid = 0x01, eDownloadResumeBusy = 0x02, eDownloadResumeSizeError};
	enum {eDownloadCancelSuccess = 0x00, eDownloadCancelDLSuccess = 0x01, eDownloadCancelFNF = 0x02};
	
	void startControlLoop();
	
private:	
	void handleLogin(string);
	void handleDownloadStart(string);
	void handleNotLoggedIn(void);
	void handleDownloadPause(void);
	void handleDownloadResume(string);
	void handleDownloadCancel(string);
	void handleList();
	
	bool userLoggedIn;
	string loggedInUser;
	
	vector<pair<pair<string, string>, uint32_t > > pausedDownloadQueue;
	
	static const char* UserDBFileName;
	static const char* MD5SumFileName;
	
	DL_Layer* dl_layer;
	
	bool isDownloadPending;
	ifstream* downloadFS;
	uint32_t downloadFileSize;
	string downloadFileName;
};


#endif //SERVER_APP_LAYER_H
