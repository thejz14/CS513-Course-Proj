#ifndef DATALINK_LAYER
#define DATALINK_LAYER

#include <stdint.h>
#include <pthread.h>
#include <queue>
#include <vector>
#include <string>
#include <utility>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include "physical_layer.h"
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

	static void disableSigalrm(void);
	static void enableSigalrm(void);

	void resendFrame(void); //helper method for timerISR() used to resend the frame that just timed out
	void restartTimer(void); //helper method for timerISR() used to restart the timer after a time out

	static const char* StartDelim;
	static const char* EndDelim;
	static const uint8_t DelimSize = 2;

private:

	typedef enum { eDataPacket = 1, eDataEndPacket = 2, eAckPacket = 3} FrameType_t;

	void startControlLoop(void);
	void tryToSend(void);
	void createFrames(string);
	void tryToRecv(void);
	void recvDataPacket(Frame_t*);
	uint16_t computeChecksum(Frame_t*);
	bool verifyChecksum(Frame_t*);
	string byteStuff(Frame_t*);
	Frame_t* byteUnstuff(string);

	void sendFrame(Frame_t*);
	void sendAck(uint16_t);
	void updateSendWindow(uint16_t);


	void initializeTimer(void);
	void startTimer(timeval);


	queue<std::string> sendQueue; //send queue between app and dl layer
	queue<std::string> recvQueue; //recv queue between app and dl layer

	PH_Layer* ph_layer; // used to call ph_send()/ph_recv() for interface between physical and dl layer

	pthread_mutex_t sendLock;// used to lock sendQueue
	pthread_mutex_t recvLock;// used to lock recvQueue

	static const uint16_t MaxMessageSize = 256; //Max size of message contained in sendQueue or recvQueue
	static const uint16_t MaxPayloadSize = 128;

	const uint16_t TimeoutDuration; //Duration since frame sent that causes a timeout

	uint16_t currentSeqNum; //The next available sequence number for a new frame that is going to be sent

	timer_t timer_id; //ID of the timer used for timeouts

	/*
	 * This struct is used in compairing the timestamps of each frame in the sendWindow to order the queue
	 * with the frame with the earliest timeout first
	 */
	struct compareTimeval {
	    bool operator()(const pair<Frame_t*, timeval> t1, const pair<Frame_t*, timeval> t2) // Returns true if t1 is earlier than t2
		{
			if(t1.second.tv_sec > t2.second.tv_sec)
		    {
		    	return true;
			}
		    else if(t1.second.tv_sec == t2.second.tv_sec)
		    {
		    	if(t1.second.tv_usec >= t2.second.tv_usec)
		    	{
		    		return true;
		    	}
		    	else
		    	{
		    		return false;
		    	}
		    }
		    else
		    {
		    	return false;
		    }
		}
	};
	/*
	 * A queue of the frames which have currently been transmitted and for which an ACK has not been received
	 * Note: the frames in this queue are not byte stuffed
	 */
	vector< pair<Frame_t*, timeval> > sendWindow;
	queue<Frame_t*> waitQueue;
	const uint16_t MaxSendWindow; //Maximum size of the sendWindow Queue
	uint16_t recvWindow; //Sequence number of the next expected frame
};

#endif
