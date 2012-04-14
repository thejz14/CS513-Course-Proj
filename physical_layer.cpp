#include "physical_layer.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <cstring>
#include <stdlib.h>
#include <iostream>
#include <fcntl.h>
#include "datalink_layer.h"
using namespace std;

/*
 * Constat Defs
 */
const char* PH_Layer::port = "2012";

int main(int argc, char *argv[])
{

	if(argc != 2)
	{
		cout << "Incorrect parameters: not sure what they are yet" << endl;
		exit(0);
	}
	PH_Layer::startServer(atof(argv[1]));
}

PH_Layer::PH_Layer(int32_t sockFD, double errRate) : socketFD(sockFD), errorRate(errRate)
{
	//check error rate
	if(errorRate > 1.0 || errorRate < 0.0)
	{
		cout << "Error: Command line error rate is invalid (must be between 0 and 1). Using default error rate of 50%.\n";
		errorRate = DefaultErrorRate;
	}
	
	//initialize locks
	pthread_mutex_init(&sendLock, NULL);
	pthread_mutex_init(&recvLock, NULL);

	//set socket FD to non blocking for recv()
	fcntl(socketFD, F_SETFL, fcntl(socketFD, F_GETFL) | O_NONBLOCK);
	
	pthread_create(&dl_thread, NULL, DL_Layer::DLCreate, reinterpret_cast<void*>(this));
	
	startControlLoop();
}


void PH_Layer::startServer(double errRate)
{
	struct addrinfo socketPref, *sockInfo;
	int32_t listenSD = 0, clientSD = 0;
	socklen_t sizeOfHostAddr;
	struct sockaddr_storage hostAddress;\
	char clientIPAddr[INET_ADDRSTRLEN] = {0};
	uint32_t pid;
	PH_Layer* currentPHInstance;


	memset(&socketPref, 0x00, sizeof(socketPref));
	socketPref.ai_family = AF_INET; //IPV4
	socketPref.ai_socktype = SOCK_STREAM; //TCP
	socketPref.ai_flags = AI_PASSIVE;

	if(getaddrinfo(NULL, PH_Layer::port, &socketPref, &sockInfo))	//returns 0 on success, -1 error
	{
		cout << "Error getting address info: " << strerror(errno) << endl;
		exit(0);
	}

	if((listenSD = socket(sockInfo->ai_family, sockInfo->ai_socktype, sockInfo->ai_protocol)) < 0)
	{
		cout << "Error creating socket: " << strerror(errno) << endl;
		exit(0);
	}

	if(bind(listenSD, sockInfo->ai_addr, sockInfo->ai_addrlen))
	{
		cout << "Error binding socket: " << strerror(errno) << endl;
		exit(0);
	}

	if(listen(listenSD, MaxPendingConnections))
	{
		cout << "Error listening on socket: " << strerror(errno) << endl;
		exit(0);
	}

	sizeOfHostAddr = sizeof(hostAddress);
	while(1)
	{
		clientSD = accept(listenSD, (struct sockaddr *)&hostAddress, &sizeOfHostAddr);

		//get the client IP
		inet_ntop(AF_INET, &(reinterpret_cast<struct sockaddr_in *>(&hostAddress)->sin_addr), clientIPAddr,INET_ADDRSTRLEN);
		cout << "Connection accepted from: " << clientIPAddr << endl;
		
		if((pid = fork()) == 0)
		{
			close(listenSD);
			
			//should create layers and then start the control loop of recv/send
			currentPHInstance = new PH_Layer(clientSD, errRate);

			exit(0);

		}
		else if(pid > 0)
		{
			//reap zombies
			while(waitpid(-1, NULL, WNOHANG) > 0);
		}
		else
		{
			cout << "Error forking child: " << strerror(errno) << endl;
			exit(0);
		}
		
	}
}

void PH_Layer::startControlLoop()
{
	while(1)
	{
		tryToSend();
		tryToRecv();

	}
}

void PH_Layer::tryToSend()
{
	int lockStatus = 0;

	if((lockStatus = pthread_mutex_trylock(&sendLock)) == 0)//try to lock the sendlock to see if there is anything to send
	{
		Frame_t* frame;
		int bytesSent = 0;

		if(sendQueue.size()) // is greater than 0
		{
			frame = sendQueue.front();
			sendQueue.pop(); //pop the message off the queue;
		}

		//unlock the sendLock to allow DL layer to access it
		pthread_mutex_unlock(&sendLock);

		if(frame != NULL)
		{
			//send the frame header section
			while((bytesSent += send(socketFD, &(frame->sec.header[bytesSent]), FRAME_HEADER_SIZE - bytesSent, 0)) < FRAME_HEADER_SIZE);
			
			//reset bytesSent
			bytesSent = 0;
			
			//send the frame payload
			while((bytesSent += send(socketFD, &(frame->sec.payloadPtr[bytesSent]), frame->fld.len - bytesSent, 0)) < frame->fld.len);
			
			//reset bytesSent
			bytesSent = 0;
			
			//sent the frame trailer section
			while((bytesSent += send(socketFD, &(frame->sec.trailer[bytesSent]), FRAME_TRAILER_SIZE - bytesSent, 0)) < FRAME_TRAILER_SIZE);
		}
	}
	else if(lockStatus != EBUSY) //error trying to get the lock
	{
		cout << "Error locking the sending lock in the physical layer\n";
	}
}

void PH_Layer::tryToRecv()
{
	static string frameBuffer = "";

	uint32_t numBytesRecvd = 0;
	char recvBuffer[MaxRecvBufferSize + 1] = {0};
	size_t endLoc = 0;

	//receive all data currently stored on the socket
	while((numBytesRecvd = recv(socketFD, recvBuffer, MaxRecvBufferSize, 0)) > 0)
	{
		//add the newly received data to the frame buffer
		frameBuffer.append(recvBuffer);

		//clear out the recv buffer
		memset(recvBuffer, 0x00, MaxRecvBufferSize + 1);
	}

	//parse out all full frames from the frameBuffer, since it is possible to have received a partial frame so far
	if(frameBuffer.find(DL_Layer::StartDelim) == 0)
	{
		while((endLoc = frameBuffer.find(DL_Layer::EndDelim)) != string::npos)
		{
			Frame_t* newFrame = new Frame;
			
			//copy over header
			memcpy(&(newFrame->sec.header[DL_Layer::DelimSize]), &(frameBuffer.c_str()[DL_Layer::DelimSize]), FRAME_HEADER_SIZE - DL_Layer::DelimSize);
			
			//setup frame payload
			newFrame->fld.payload = new char[newFrame->fld.len];
			
			//copy over payload
			memcpy(newFrame->sec.payloadPtr, &(frameBuffer.c_str()[FRAME_HEADER_SIZE]), newFrame->fld.len);
			
			memcpy(&(newFrame->sec.trailer[0]), &(frameBuffer.c_str()[FRAME_HEADER_SIZE + newFrame->fld.len]), FRAME_TRAILER_SIZE - DL_Layer::DelimSize);
			
			frameBuffer.erase(0, endLoc + DL_Layer::DelimSize);
			
			//get the recvLock to add new frame
			if(pthread_mutex_lock(&recvLock) == 0)
			{
				//have the lock.. add the new frame to the queue
				recvQueue.push(newFrame);
			}
			else
			{
				cout << "ERROR: Unable to get recvlock in trytorecv()\n";
			}
		}
	}
	else //error
	{
		cout << "ERROR: The frame buffer in the physical layer is not aligned properly\n";
	}
}

void PH_Layer::ph_send(Frame* frame)
{
	if(pthread_mutex_lock(&sendLock) == 0)
	{
		//add the frame to the physical layers send queue to send
		sendQueue.push(frame);
	}
	else
	{
		cout << "ERROR: Unable to get sendLock in ph_send()\n";
	}
}