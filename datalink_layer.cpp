#include "datalink_layer.h"

#include <vector>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
using namespace std;

/*
 * Global definitions
 */
void timeoutISR(int);

/*
 * File static definitions
 */
static DL_Layer* dl_layer;

/*
 * Class static definitions
 */
const char* DL_Layer::StartDelim = new char[2]{0x10, 0x02};
const char* DL_Layer::EndDelim = new char[2]{0x10, 0x03};


DL_Layer::DL_Layer(void* phPtr): ph_layer(reinterpret_cast<PH_Layer*>(phPtr)),
								 MaxSendWindow(4),
								 currentSeqNum(0),
								 recvWindow(0),
								 TimeoutDuration(3)
{
	//initialize locks
	pthread_mutex_init(&sendLock, NULL);
	pthread_mutex_init(&recvLock, NULL);

	//timer setup for later use
	initializeTimer();

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

void DL_Layer::tryToSend()
{
	//first check if there is room in the send window and that there are not frames already waiting to be sent
	while(sendWindow.size() < MaxSendWindow && !waitQueue.empty())
	{
		Frame_t* frame = waitQueue.front();
		waitQueue.pop();

		sendFrame(frame);
	}

	//Now process messages from the app layer (create frames and send if possible)
	if(pthread_mutex_lock(&sendLock) == 0)
	{
		if(sendQueue.size() > 0)	//there are messages from the app layer to send
		{
			//get next message
			string message = sendQueue.front();
			sendQueue.pop();

			pthread_mutex_unlock(&sendLock);

			createFrames(message);
		}
	}
	else
	{
		cout << "ERROR: Unable to get the send lock in DL Layer tryToSend()\n";
	}
}

void DL_Layer::tryToRecv()
{
	string frameStream;
	while((frameStream = ph_layer->ph_recv()) != "") //while something is received
	{
		Frame_t* frame = byteUnstuff(frameStream);

		if(verifyChecksum(frame))
		{
			if(frame->fld.type == eAckPacket)
			{
				updateSendWindow(frame->fld.seqNum);
			}
			else
			{
				if(frame->fld.seqNum == recvWindow)
				{
					sendAck(frame);
					recvDataPacket(frame);

					recvWindow++;
				}
				else
				{

				}
			}
		}//else corrupted frame
		delete(frame);
	}
}

void DL_Layer::updateSendWindow(uint16_t recvdSeqNum)
{
	if(recvdSeqNum == )
}

void DL_Layer::recvDataPacket(Frame_t* frame)
{
	static string dataBuffer = "";

	//add the payload of the frame to the databuffer
	dataBuffer.append(frame->fld.payload);

	if(frame->fld.type == eDataEndPacket) //end of packet
	{
		if(pthread_mutex_lock(&recvLock) == 0)
		{
			recvQueue.push(dataBuffer);
			pthread_mutex_unlock(&recvLock);

			dataBuffer.clear(); //erase the message to setup for a new message
		}
		else
		{
			cout << "ERROR: Unable to get the recvLock for recvDataPacket() in DL_Layer\n";
		}
	}//else all done
}


void DL_Layer::createFrames(string message)
{
	if(message.size() > MaxMessageSize)
	{
		cout << "Error: Message received from App layer is large than the maximum message size\n";
		return; //drop the message
	}
	else
	{
		while(message.size() > 0)
		{
			uint16_t payloadSize = message.size() > MaxMessageSize ? MaxMessageSize : message.size();
			Frame_t* frame = new Frame_t;
			char* payloadPtr = new char[MaxMessageSize];

			message.copy(payloadPtr, payloadSize);
			message.erase(0, payloadSize);

			if(message.size() > 0)
			{
				frame->fld.type = htons(eDataPacket);
			}
			else
			{
				frame->fld.type = htons(eDataEndPacket);
			}

			frame->fld.seqNum = htons(currentSeqNum++);
			frame->fld.len = htons(payloadSize);
			frame->fld.payload = payloadPtr;

			frame->fld.checksum = htons(computeChecksum(frame));

			if(sendWindow.size() < MaxSendWindow)	//Room to send
			{
				sendFrame(frame);
			}
			else
			{
				waitQueue.push(frame);
			}
		}
	}
}

void DL_Layer::sendFrame(Frame_t* frame)
{
	timeval timeout;

	//byte stuff the frame
	string frameStream = byteStuff(frame);

	//get the time the frame is being added to the window
	gettimeofday(&timeout, NULL);

	timeout.tv_sec += TimeoutDuration;

	//add the frame to the send window
	sendWindow.push(make_pair(frame,timeout));

	ph_layer->ph_send(frameStream, frame->fld.seqNum);

	if(sendWindow.size() == 1)		//timer is currently not initialized
	{
		startTimer(timeout);
	}
}

void DL_Layer::enableSigalrm()
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGALRM);

	if(pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0)
	{
		cout << "ERROR: Unable to enable SIGALRM correctly\n";
	}
}

void DL_Layer::disableSigalrm()
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGALRM);

	if(pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
	{
		cout << "ERROR: Unable to disable SIGALRM correctly\n";
	}
}

void DL_Layer::initializeTimer()
{
	//enable sigalrm
	enableSigalrm();

	//set up handler for SIGALRM signal
	signal(SIGALRM, timeoutISR);

	//create timer
	timer_create(CLOCK_REALTIME, NULL, &timer_id);

	//set up a reference to the dl_layer for the timeoutISR to use
	dl_layer = this;
}

void DL_Layer::startTimer(timeval timeout)
{
	itimerspec time_val = {0};

	//convert microseconds of timeout to nanoseconds/ timeval struct to itimerspec
	time_val.it_value.tv_sec = timeout.tv_sec;
	time_val.it_value.tv_nsec = timeout.tv_usec * 1000;

	//start timer
	if(timer_settime(timer_id, 0, &time_val, NULL) != 0)
	{
		cout << "ERROR: Unable to start timer in DL Layer restartTimer()\n";
	}
}

void timeoutISR(int signNum)
{
	//resend the frame that caused the timeout
	dl_layer->resendFrame();

	dl_layer->restartTimer();
}

/****************************************************************************************
 *Description: Called from the timeoutISR() to handle retransmission of a timed out frame
 ****************************************************************************************/
void DL_Layer::resendFrame()
{
	Frame_t* frame = sendWindow.top().first;
	sendWindow.pop();

	sendFrame(frame);
}

void DL_Layer::restartTimer()
{
	if(sendWindow.size() > 0)// should always be true
	{
		timeval currentTime = {0};
		timeval newTimeout = {0};
		gettimeofday(&currentTime, NULL);

		//check if any other frames have timed out and resend them as well
		while(timercmp(&currentTime, &(sendWindow.top().second), >=))
		{
			resendFrame();
		}

		//now restart time to next timeout
		timersub(&(sendWindow.top().second), &currentTime, &newTimeout);

		startTimer(newTimeout);
	}
	else
	{
		cout << "Error: Unable to restart timer after time out\n";
	}
}

uint16_t DL_Layer::computeChecksum(Frame_t* frame)
{
	uint16_t chksum = 0;

	//compute checksum over the frame header
	for(int i = 0; i < FRAME_HEADER_SIZE; i++)
	{
		chksum += frame->sec.header[i];
	}

	//compute checksum over the frame payload
	for(int i = 0; i < frame->fld.len; i++)
	{
		chksum += frame->fld.payload[i];
	}

	//computer checksum over the frame trailer(skip over checksum)
	for(int i = 0; i < FRAME_TRAILER_SIZE - 2; i++)
	{
		chksum += frame->sec.trailer[i + 2];
	}

	return chksum;
}

bool DL_Layer::verifyChecksum(Frame_t* frame)
{
	if(frame->fld.checksum == computeChecksum(frame))
	{
		return true;
	}
	else
	{
		return false;
	}
}


string DL_Layer::byteStuff(Frame_t* frame)
{
	//initialize the frame stream (frame + stuffed bytes
	string frameStream(reinterpret_cast<char*>(frame->sec.header), (size_t)FRAME_HEADER_SIZE);
	frameStream.append(reinterpret_cast<char*>(frame->sec.payloadPtr), (size_t)frame->fld.len);
	frameStream.append(reinterpret_cast<char*>(frame->sec.trailer), (size_t)FRAME_TRAILER_SIZE);

	//now sort through and byte stuff the stream (start off after the start delimiters)
	for(int i = DelimSize; i < frameStream.size() - DelimSize; i++)
	{
		if(frameStream[i] == StartDelim[0]) //DLE
		{
			frameStream.insert(i, StartDelim, 1);
			i++; //increment i to step past already stuffed byte
		}
	}

	return frameStream;
}

Frame_t* DL_Layer::byteUnstuff(string frameStream)
{
	Frame_t* frame = new Frame_t;

	for(int i = DelimSize; i < frameStream.size() - DelimSize; i++)
	{
		if(frameStream[i] == StartDelim[0]) //DLE
		{
			frameStream.erase(i, 1);
		}
	}

	//copy over the header
	memcpy(&(frame->sec.header[DelimSize]), &(frameStream.c_str()[DelimSize]), FRAME_HEADER_SIZE - DelimSize);

	//convert header parameters to host endianness
	frame->fld.type = ntohs(frame->fld.type);
	frame->fld.seqNum = ntohs(frame->fld.seqNum);
	frame->fld.len = ntohs(frame->fld.len);

	//copy over the payload
	frame->sec.payloadPtr = new uint8_t[frame->fld.len];
	memcpy(frame->sec.payloadPtr, &(frameStream.c_str()[FRAME_HEADER_SIZE]), frame->fld.len);

	//copy over the trailer
	memcpy(frame->sec.trailer, &(frameStream.c_str()[FRAME_HEADER_SIZE + frame->fld.len]), FRAME_TRAILER_SIZE - DelimSize);

	return frame;
}
