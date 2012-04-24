#ifndef CLIENT_APP_LAYER_H
#define CLIENT_APP_LAYER_H

#include <pthread.h>
#include <queue>
#include <fstream>
#include "datalink_layer.h"
using namespace std;

#define CLIENT_OUT(lock, msg) 	if(pthread_mutex_lock(&lock) == 0)\
								{\
									cout << msg;\
									pthread_mutex_unlock(&lock);\
								}\
								else\
								{\
									cout << "ERROR: Unable to get the IO lock";\
								}



class ClientApp
{
public:
	static void *CAPCreate(void* thisPtr) { reinterpret_cast<ClientApp*>(thisPtr)->startIOControlLoop(); return NULL; };
	ClientApp(void* dlPtr);
	
	static void* startBackgroundControlLoopHelper(void*);
	void startBackgroundControlLoop(void);
	
	void startIOControlLoop(void);
		
private:
	void tryToSend(void);
	void tryToRecv(void);
	void handleDownloadStartBG(string);
	
	
	void waitForResponse(void);
	void loginHandler(void);
	void loginResponseHandler(string);
	void downloadStartHandler();
	void downloadStartResponseHandler(string);
	void downloadPauseHandler();
	void downloadPauseResponseHandler(string);
	void downloadResumeHandler();
	void downloadResumeResponseHandler(string);
	void downloadCancelHandler();
	void downloadCancelResponseHandler(string);
	void listHandler();
	void listResponseHandler(string);
	void logoutHandler();
	void logoutResponseHandler(string);
	
	pthread_mutex_t ioLock; // used to lock output to the console
	pthread_mutex_t commandSendLock; // used to lock commands read in from the console between the IO thread and background thread
	pthread_mutex_t commandRecvLock;
	pthread_t backgroundThread;
	pthread_cond_t recvdMsg;
	
	queue<string> sendMessageQueue;
	queue<string> recvMessageQueue;
	
	static const uint8_t MaxUserNameSize = 30;
	static const uint8_t MaxPasswordSize = 30;
	
	DL_Layer* dl_layer;
	
	bool isDownloadPending;
	ofstream* downloadFS;
	string downloadFileName;
	uint32_t downloadFileSize;
};


#endif //CLIENT_APP_LAYER_H
