#include "datalink_layer.h"


/*
 * Class static definitions
 */
const char* DL_Layer::StartDelim = new char[2]{0x10, 0x02};
const char* DL_Layer::EndDelim = new char[2]{0x10, 0x03};


DL_Layer::DL_Layer(void* phPtr): ph_layer(phPtr), maxSendWindow(4)
{
	//initialize locks
	pthread_mutex_init(&sendLock, NULL);
	pthread_mutex_init(&recvLock, NULL);

	startControlLoop();
}


void DL_Layer::startControlLoop()
{
	while(1)
	{
		tryToSend();
		tryToRecv();

	}
}
