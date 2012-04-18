#include "datalink_layer.h"

#include <vector>
#include <iostream>
#include <iomanip>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <cstring>
#include <algorithm>
#include "Output_macros.h"
#include "server_appLayer.h"
#include "client_appLayer.h"
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


DL_Layer::DL_Layer(void* params): ph_layer(reinterpret_cast<PH_Layer*>(reinterpret_cast<ThreadParams_t*>(params)->thisPtr)),
								 MaxSendWindow(reinterpret_cast<ThreadParams_t*>(params)->sWindowSize),
								 currentSeqNum(0),
								 recvWindow(0),
								 TimeoutDuration(reinterpret_cast<ThreadParams_t*>(params)->timeoutLen),
								 framesSent(0),
								 retransSent(0),
								 acksSent(0),
								 framesRcvd(0),
								 acksRcvd(0),
								 framesRcvdError(0),
								 acksRcvdError(0),
								 duplicatesRcvd(0),
								 blocks(0)
								 
{								 
	DEBUG_OUTPUT("DL layer created\n");

	//initialize locks
	pthread_mutex_init(&sendLock, NULL);
	pthread_mutex_init(&recvLock, NULL);
	pthread_cond_init(&sendQueueNotFull, NULL);
	pthread_cond_init(&recvdMsg, NULL);

	if(reinterpret_cast<ThreadParams_t*>(params)->isServer)
	{
		DEBUG_OUTPUT("Creating Server Layer\n");
		ServerApp* serverApp = new ServerApp(reinterpret_cast<void*>(this));
		
		pthread_create(&app_thread, NULL, ServerApp::SAPCreate, reinterpret_cast<void*>(serverApp));
	}
	else
	{
		DEBUG_OUTPUT("Creating App Layer\n");
		ClientApp* clientApp = new ClientApp(reinterpret_cast<void*>(this));
		
		pthread_create(&app_thread, NULL, ClientApp::CAPCreate, reinterpret_cast<void*>(clientApp));
	}
}


void DL_Layer::startControlLoop()
{
	//timer setup for later use
	initializeTimer();
		
	while(1)
	{
		
		tryToSend();
		tryToRecv();
	}
}

void DL_Layer::tryToSend()
{
	int lockReturn = 0;

	//first check if there is room in the send window and that there are not frames already waiting to be sent
	while(sendWindow.size() < MaxSendWindow && !waitQueue.empty())
	{
		Frame_t* frame = waitQueue.front();
		waitQueue.pop();

		sendFrame(frame);
	}

	//Now process messages from the app layer (create frames and send if possible)
	while(sendWindow.size() < MaxSendWindow && (lockReturn = pthread_mutex_lock(&sendLock)) == 0 && (sendQueue.size() > 0 || pthread_mutex_unlock(&sendLock)))	//there are messages from the app layer to send
	{
		//get next message
		string message = sendQueue.front();
		sendQueue.pop();

		pthread_mutex_unlock(&sendLock);

		createFrames(message);
	}
	if(lockReturn != 0)
	{
		cout << "ERROR: Unable to get the send lock in DL Layer tryToSend()\n";
	}

	if(sendQueue.size() == 0)
	{
		pthread_cond_signal(&sendQueueNotFull);
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
				acksRcvd++;
				updateSendWindow(frame->fld.seqNum);
				LAYER_OUTPUT("DL layer: ACK received for " << frame->fld.seqNum << "\n");
			}
			else
			{
				framesRcvd++;
				
				if(frame->fld.seqNum == recvWindow)
				{
					sendAck(frame->fld.seqNum);
					recvDataPacket(frame);

					recvWindow++;

					LAYER_OUTPUT("DL layer:Received packet "<< frame->fld.seqNum << ". Sending Ack\n");
				}
				else if(frame->fld.seqNum < recvWindow && frame->fld.seqNum >= (recvWindow - MaxSendWindow))
				{
					sendAck(frame->fld.seqNum);
					
					LAYER_OUTPUT("DL layer: Acking previously ack'd frame " << frame->fld.seqNum << endl);
				}
				else
				{
					duplicatesRcvd++;
					LAYER_OUTPUT("DL layer:Dropping packet " << frame->fld.seqNum << ". Recv window currently is: " << recvWindow << "\n");
				}
			}
		}
		else
		{
			if(frame->fld.type == eAckPacket)
			{
				acksRcvdError++;
				LAYER_OUTPUT("DL layer: Corrupted ack received. Sequence number: " << frame->fld.seqNum << "\n");
			}
			else
			{
				framesRcvdError++;
				LAYER_OUTPUT("DL layer: Corrupted frame received. Sequence number: " << frame->fld.seqNum << "\n");
			}
		}

		delete(frame);
	}
}

void DL_Layer::updateSendWindow(uint16_t recvdSeqNum)
{
	if(recvdSeqNum >= currentSeqNum)	//could not have been sent, ignore
	{
		cout << "Error: ACK received for unsent frame. Received seq # is " << recvdSeqNum << endl;
	}
	else
	{
		//go through the sendWindow and remove ack'd frames
		Frame_t* frame = NULL;
		bool timerUpdateRequired = false;

		for(vector<pair<Frame_t*, timeval> >::iterator i = sendWindow.begin(); i != sendWindow.end(); )
		{
			if(ntohs((*i).first->fld.seqNum) <= recvdSeqNum)
			{
				//remove the frame
				frame = (*i).first;
				sendWindow.erase(i);
				
				delete(frame);

				if(i == sendWindow.begin())
				{
					timerUpdateRequired = true;
				}
			}
			else
			{
				i++;
			}
		}

		if(timerUpdateRequired)
		{
			if(sendWindow.size() > 0)
			{
				restartTimer();
			}
			else //disable timer
			{				
				stopTimer();
			}
		}
	}
}

void DL_Layer::recvDataPacket(Frame_t* frame)
{
	static string dataBuffer = "";

	//add the payload of the frame to the databuffer
	dataBuffer.append(frame->fld.payload, frame->fld.len);

	if(frame->fld.type == eDataEndPacket) //end of packet
	{
		if(pthread_mutex_lock(&recvLock) == 0)
		{
			recvQueue.push(dataBuffer);
			pthread_mutex_unlock(&recvLock);

			//wake a possible waiting app layer
			pthread_cond_signal(&recvdMsg);

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
			
			frame->fld.checksum = htons(computeChecksum(frame, true));

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
	sendWindow.push_back(make_pair(frame,timeout));

	LAYER_OUTPUT("DL Layer: Passing frame " << ntohs(frame->fld.seqNum) << " to physical layer\n");

	framesSent++;
	ph_layer->ph_send(frameStream, ntohs(frame->fld.seqNum), false);
	
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
	timeval currentTime = {0};
	timeval timeoutDuration = {0};

	gettimeofday(&currentTime, NULL);

	if(timercmp(&currentTime, &timeout, <))
	{
		timersub(&timeout , &currentTime, &timeoutDuration);	
		
		//convert microseconds of timeout to nanoseconds/ timeval struct to itimerspec
		time_val.it_value.tv_sec = timeoutDuration.tv_sec;
		time_val.it_value.tv_nsec = timeoutDuration.tv_usec * 1000;

		//start timer
		if(timer_settime(timer_id, 0, &time_val, NULL) != 0)
		{
			cout << "Error starting timer: " << strerror(errno) << endl;
			cout << "tv_sec: " << time_val.it_value.tv_sec << endl;
			cout << "tv_nsec: " << time_val.it_value.tv_nsec << endl;
		}
	}
	else	//timeout occured between setting time and sending frame.. so resend timed out frames
	{
		restartTimer();
	}
	
}

void DL_Layer::stopTimer()
{
	itimerspec time_val = {0};
	
	DEBUG_OUTPUT("Timer stopped\n");
	
	if(timer_settime(timer_id, 0, &time_val, NULL) != 0)
	{
		cout << "ERROR: Unable to stop timer in DL Layer stopTimer()\n";
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
	timeval timeout;
	
	//sort the send window by timestamp
	sort(sendWindow.begin(), sendWindow.end(), compareTimeval());

	Frame_t* frame = sendWindow.front().first;
	sendWindow.erase(sendWindow.begin());
	
	LAYER_OUTPUT("Timeout occured for frame " << ntohs(frame->fld.seqNum) << ". Resending frame\n");

	//byte stuff the frame
	string frameStream = byteStuff(frame);

	//get the time the frame is being added to the window
	gettimeofday(&timeout, NULL);

	timeout.tv_sec += TimeoutDuration;

	//add the frame to the send window
	sendWindow.push_back(make_pair(frame,timeout));

	LAYER_OUTPUT("DL Layer: Passing frame " << ntohs(frame->fld.seqNum) << " to physical layer\n");

	retransSent++;
	ph_layer->ph_send(frameStream, ntohs(frame->fld.seqNum), false);
}

void DL_Layer::restartTimer()
{
	if(sendWindow.size() > 0)// should always be true
	{
		//sort the send window by timestamp
		sort(sendWindow.begin(), sendWindow.end(), compareTimeval());

		timeval currentTime = {0};
		timeval newTimeout = {0};
		gettimeofday(&currentTime, NULL);

		//check if any other frames have timed out and resend them as well
		while(timercmp(&currentTime, &(sendWindow.front().second), >=))
		{
			resendFrame();
		}

		//now restart the timer
		startTimer(sendWindow.front().second);
	}
	else
	{
		cout << "Error: Unable to restart timer after time out\n";
	}
}

uint16_t DL_Layer::computeChecksum(Frame_t* frame, bool lenIsNetworkOrder)
{
	uint16_t chksum = 0;
	uint16_t payloadLen = lenIsNetworkOrder ? ntohs(frame->fld.len): frame->fld.len;

	//compute checksum over the frame header
	for(int i = 0; i < FRAME_HEADER_SIZE; i++)
	{
		chksum += reinterpret_cast<uint8_t*>(&(frame->sec.header))[i];
	}

	//compute checksum over the frame payload
	for(int i = 0; i < payloadLen; i++)
	{
		chksum += frame->fld.payload[i];
	}

	//computer checksum over the frame trailer(skip over checksum)
	for(int i = 0; i < FRAME_TRAILER_SIZE - 2; i++)
	{
		chksum += reinterpret_cast<uint8_t*>(&(frame->sec.trailer))[i + 2];\
	}

	return chksum;
}

bool DL_Layer::verifyChecksum(Frame_t* frame)
{
	if(frame->fld.checksum == computeChecksum(frame, false))
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
	string frameStream;
	frameStream.append(reinterpret_cast<char*>(&(frame->sec.header)), (size_t)FRAME_HEADER_SIZE);
	frameStream.append(reinterpret_cast<char*>(frame->sec.payloadPtr), (size_t)ntohs(frame->fld.len));
	frameStream.append(reinterpret_cast<char*>(&(frame->sec.trailer)), (size_t)FRAME_TRAILER_SIZE);
	
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
	memcpy(&(frame->sec.header), frameStream.c_str(), FRAME_HEADER_SIZE);

	//convert header parameters to host endianness
	frame->fld.type = ntohs(frame->fld.type);
	frame->fld.seqNum = ntohs(frame->fld.seqNum);
	frame->fld.len = ntohs(frame->fld.len);

	//copy over the payload
	frame->sec.payloadPtr = new uint8_t[frame->fld.len];
	memcpy(frame->sec.payloadPtr, &(frameStream.c_str()[FRAME_HEADER_SIZE]), frame->fld.len);

	//copy over the trailer
	memcpy(&(frame->sec.trailer), &(frameStream.c_str()[FRAME_HEADER_SIZE + frame->fld.len]), FRAME_TRAILER_SIZE);

	frame->fld.checksum = ntohs(frame->fld.checksum);
	
	return frame;
}


void DL_Layer::sendAck(uint16_t sNum)
{
	Frame_t* frame = new Frame_t;
	string stuffedFrame;

	frame->fld.type = htons(eAckPacket);
	frame->fld.seqNum = htons(sNum);
	frame->fld.len = htons(0);
	frame->fld.payload = NULL;
	frame->fld.checksum = htons(computeChecksum(frame, true));
	
	stuffedFrame = byteStuff(frame);
	
	acksSent++;
	ph_layer->ph_send(stuffedFrame, ntohs(frame->fld.seqNum), true);
}

void DL_Layer::dl_send(string message)
{
	bool haveIncrementedCounter = false;
	
	pthread_mutex_lock(&sendLock);

	while(sendQueue.size() > 0)
	{
		if(!haveIncrementedCounter)		//incase of uninteded wakeup for another reason
		{
			blocks++;
			haveIncrementedCounter = true;
		}
		pthread_cond_wait(&sendQueueNotFull, &sendLock);
	}

	sendQueue.push(message);

	pthread_mutex_unlock(&sendLock);
}

string DL_Layer::dl_recv(bool block)
{
	string message = "";

	pthread_mutex_lock(&recvLock);

	if(block)
	{
		while(recvQueue.size() == 0)
		{
			pthread_cond_wait(&recvdMsg, &recvLock);
		}

		message = recvQueue.front();
		recvQueue.pop();
	}
	else if(recvQueue.size() > 0)
	{
		message = recvQueue.front();
		recvQueue.pop();
	}

	pthread_mutex_unlock(&recvLock);

	return message;
}

void DL_Layer::writeStats()
{
	cout << "Total number of data frames sent:" << framesSent << endl;
	cout << "Total number of retransmissions sent:" << retransSent << endl;
	cout << "Total number of acknowledgements sent:" << acksSent << endl;
	cout << "Total number of data frames received correctly (inluding duplicates):" << framesRcvd << endl;
	cout << "Total number of acknowledgements received correctly:" << acksRcvd << endl;
	cout << "Total number of data frames received with error:" << framesRcvdError << endl;
	cout << "Total number of acknowledgements received with error:" << acksRcvdError << endl;
	cout << "Total number of duplicate frames received:" << duplicatesRcvd << endl;
	cout << "Total number of times the application layer was blocked in dl_send():" << blocks << endl;
}
