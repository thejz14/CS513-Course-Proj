#ifndef SERVER_APPLAYER_H
#define SERVER_APPLAYER_H

#include "datalink_layer.h"
#include <string>
using namespace std;


class ServerApp
{
public:
	static void *SAPCreate(void* dlPtr) { new ServerApp(dlPtr); };
	ServerApp(void* dlPtr);
	
private:
	void startControlLoop();
	void startBackgroundControlLoop();
	
	void handleLogin(string);
	
	typedef enum {eLogin = 0x01, eLogout = 0x02, eList = 0x03, eDownloadStart = 0x04, eDownloadPause = 0x05, eDownloadResume = 0x06, eDownloadCancel = 0x07,
				  eUploadStart = 0x08, eUploadPause = 0x09, eUploadResume = 0x0A, eUploadCancel = 0x0B, eRemove = 0x0C, eData = 0x0D,
				  eUploadComplete = 0x0E} Command_t; 
				  
	enum {eLoginSuccess = 0x00, eInvalidUserName = 0x01, eInvalidPassword = 0x02, eUserLoggedIn};
	
	bool userLoggedIn;
	string loggedInUser;
	
	static const char* UserDBFileName;
	
	DL_Layer* dl_layer;
	
};


#endif //SERVER_APP_LAYER_H