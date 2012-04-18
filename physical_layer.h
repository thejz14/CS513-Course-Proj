#ifndef PHYSICAL_LAYER_H
#define PHYSICAL_LAYER_H

#include <stdint.h>
#include <queue>
#include <string>
#include <pthread.h>
using namespace std;


class PH_Layer
{
public:
	PH_Layer(int32_t sockFD, double errRate, bool isServer, uint8_t, uint16_t);
	
	static void startServer(double errRate, uint8_t, uint16_t);
	static void startClient(const char *, double errRate, uint8_t, uint16_t);
	
	void ph_send(string, uint16_t, bool);
	string ph_recv();
	
private:

	void startControlLoop();
	void tryToSend();
	void tryToRecv();

	queue<pair<pair<string, uint16_t>, bool >  >sendQueue;
	queue<string> recvQueue;

	pthread_mutex_t sendLock;
	pthread_mutex_t recvLock;

	pthread_t dl_thread;

	static const uint16_t MaxPendingConnections = 5;
	static const uint32_t MaxRecvBufferSize = 2048;
	static const double DefaultErrorRate = 50.0;
	static const char* serverPort;

	int32_t socketFD;
	double errorRate;
};


#endif
