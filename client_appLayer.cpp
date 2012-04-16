#include "client_appLayer.h"

#include <iostream>
#include <stdlib.h>
using namespace std;

ClientApp::ClientApp(void* dlPtr) : dl_layer(reinterpret_cast<DL_Layer*>(dlPtr))
{
	
	DL_Layer::disableSigalrm();
	
	pthread_create(&backgroundThread, NULL, startBackgroundControlLoopHelper, this);
	
	startIOControlLoop();
}


void ClientApp::startIOControlLoop()
{
	//todo
}

void* ClientApp::startBackgroundControlLoopHelper(void* thisPtr)
{
	reinterpret_cast<ClientApp*>(thisPtr)->startBackgroundControlLoop();
}

void ClientApp::startBackgroundControlLoop()
{
	//TODO
}