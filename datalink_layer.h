#ifndef DATALINK_LAYER
#define DATALINK_LAYER

#include <stdint.h>
#include <pthread.h>
#include <queue>
#include <string>
#include <utility>
#include <sys/time.h>
using namespace std;

//frame def
typedef struct{
	static const uint8_t startByte1 = 0x10; //DLE
	static const uint8_t startByte2 = 0x02; //STX
	uint8_t type;
	uint16_t seqNum;
	uint16_t len;
	char* payload;
	uint16_t checksum;
	static const uint8_t endByte1 = 0x10; //DLE
	static const uint8_t endByte2 = 0x03; //ETX
} FrameFields_t;

typedef struct{
	uint8_t header[7];
	uint8_t* payloadPtr;
	uint8_t trailer[4];
} FrameSections_t;

typedef union{
	FrameFields_t fld;
	FrameSections_t sec;
} Frame_t;


#define FRAME_HEADER_SIZE	(uint8_t)7
#define FRAME_TRAILER_SIZE	(uint8_t)4

class DL_Layer
{
public:

	DL_Layer(void*);

	static void *DLCreate(void* phPtr) { new DL_Layer(phPtr); };

	static const char* StartDelim;
	static const char* EndDelim;
	static const uint8_t DelimSize = 2;

private:
	void startControlLoop(void);
	void tryToSend(void);
	void tryToRecv(void);

	queue<std::string> sendQueue;
	queue<std::string> recvQueue;

	queue<pair<Frame_t*, timeval> > sendWindow;
	const uint16_t maxSendWindow;
	uint16_t recvWindows;

	void* ph_layer;

	pthread_mutex_t sendLock;
	pthread_mutex_t recvLock;
};

#endif
