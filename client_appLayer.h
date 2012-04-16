#ifndef CLIENT_APP_LAYER_H
#define CLIENT_APP_LAYER_H

#include <pthread.h>
#include "datalink_layer.h"

class ClientApp
{
public:
	static void *CAPCreate(void* dlPtr) { new ClientApp(dlPtr); };
	ClientApp(void* dlPtr);
	
	static void* startBackgroundControlLoopHelper(void*);
	void startBackgroundControlLoop(void);
		
private:
	
	void startIOControlLoop(void);
	
	pthread_mutex_t ioLock; // used to lock output to the console
	pthread_mutex_t commandLock; // used to lock commands read in from the console between the IO thread and background thread
	pthread_t backgroundThread;
	
	DL_Layer* dl_layer;
};


#endif //CLIENT_APP_LAYER_H