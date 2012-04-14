#ifndef PHYSICAL_LAYER_H
#define PHYSICAL_LAYER_H

#include <stdint.h>
#include <queue>
#include <string>
#include <pthread.h>
#include "datalink_layer.h"
using namespace std;


class PH_Layer
{
public:
	PH_Layer(int32_t sockFD, double errRate);
	
	static void startServer(double errRate);
	
	void ph_send(Frame*);
	void ph_recv(Frame*)
	
private:

	void startControlLoop();
	void tryToSend();
	void tryToRecv();

	queue<Frame_t*> sendQueue;
	queue<Frame_t*> recvQueue;

	pthread_mutex_t sendLock;
	pthread_mutex_t recvLock;

	pthread_t dl_thread;

	static const uint16_t MaxPendingConnections = 5;
	static const uint32_t MaxRecvBufferSize = 2048;
	static const double DefaultErrorRate = .5;
	static const char* port;

	int32_t socketFD;
	double errorRate;
};


#endif
