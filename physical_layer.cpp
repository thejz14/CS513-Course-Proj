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
#include "Output_macros.h"
using namespace std;

/*
 * Constat Defs
 */
const char* PH_Layer::serverPort = "2012";


PH_Layer::PH_Layer(int32_t sockFD, double errRate, bool isServ, uint8_t timeoutLength, uint16_t slidingWindowSize) : socketFD(sockFD), errorRate(errRate)
{
	
	//check error rate
	if(errorRate > 100.0 || errorRate < 0.0)
	{
		cout << "Error: Command line error rate is invalid (must be between 0 and 100). Using default error rate of 50%.\n";
		errorRate = DefaultErrorRate;
	}
	
	//initialize locks
	pthread_mutex_init(&sendLock, NULL);
	pthread_mutex_init(&recvLock, NULL);

	//set socket FD to non blocking for recv()
	fcntl(socketFD, F_SETFL, fcntl(socketFD, F_GETFL) | O_NONBLOCK);
	
	//initialize rand()
	srand(time(0));

	//make sure the physical layer doesn't receive the timeout signal
	DL_Layer::disableSigalrm();

	DL_Layer::ThreadParams_t* dlParams = new DL_Layer::ThreadParams_t;
	dlParams->thisPtr = this;
	dlParams->isServer = isServ;
	dlParams->timeoutLen = timeoutLength;
	dlParams->sWindowSize = slidingWindowSize;

	DL_Layer* dl_layer = new DL_Layer(reinterpret_cast<void*>(dlParams));
	
	pthread_create(&dl_thread, NULL, DL_Layer::DLCreate, reinterpret_cast<void*>(dl_layer));
	
	startControlLoop();
}


void PH_Layer::startServer(double errRate, uint8_t timeoutLength, uint16_t slidingWindowSize)
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

	if(getaddrinfo(NULL, PH_Layer::serverPort, &socketPref, &sockInfo))	//returns 0 on success, -1 error
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
			currentPHInstance = new PH_Layer(clientSD, errRate, true, timeoutLength, slidingWindowSize);

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

void PH_Layer::startClient(const char * serverAddress, double errRate, uint8_t timeoutLength, uint16_t slidingWindowSize)
{
	struct addrinfo socketPref, *servInfo;
	int32_t sockFD = 0;

	memset(&socketPref, 0x00, sizeof(socketPref));
	socketPref.ai_family = AF_INET; //IPV4
	socketPref.ai_socktype = SOCK_STREAM; //TCP
	socketPref.ai_flags = AI_PASSIVE;

	if(getaddrinfo(serverAddress, serverPort, &socketPref, &servInfo))	//returns 0 on success, -1 error
	{
		cout << "Error getting address info: " << strerror(errno) << endl;
		exit(0);
	}

	if((sockFD  = socket(servInfo->ai_family, servInfo->ai_socktype, servInfo->ai_protocol)) < 0)
	{
		cout << "Error creating socket: " << strerror(errno) << endl;
		exit(0);
	}

	if(connect(sockFD, servInfo->ai_addr, servInfo->ai_addrlen))
	{
		cout << "Error connecting to server: " << strerror(errno) << endl;
		exit(0);
	}

	DEBUG_OUTPUT("Connection established\n");

	PH_Layer* phLayer = new PH_Layer(sockFD, errRate, false, timeoutLength, slidingWindowSize);
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

	while((lockStatus = pthread_mutex_trylock(&sendLock)) == 0 && (sendQueue.size() > 0 || pthread_mutex_unlock(&sendLock)))//try to lock the sendlock to see if there is anything to send
	{
		string frameStream;
		uint16_t seqNum;
		bool isAck;
		int bytesSent = 0;

		frameStream = sendQueue.front().first.first; //get the frame to be sent
		seqNum = sendQueue.front().first.second;
		isAck = sendQueue.front().second;
		sendQueue.pop(); //pop the message off the queue;
		
		//unlock the sendLock to allow DL layer to access it
		pthread_mutex_unlock(&sendLock);


		if((rand() % 1000) / 10. < errorRate) //Create an error in the frame
		{
			uint16_t byteIndex = frameStream.size() - DL_Layer::DelimSize - 1; //make sure error isn't in delimiters

			//create error (and make sure it doesnt create an unstuffed DLE byte
			frameStream[byteIndex] += 1;
			
			if(isAck)
			{
				LAYER_OUTPUT("Creating an error in the ACK for frame " << seqNum << "\n");
			}
			else
			{
				LAYER_OUTPUT("Creating an error in:" << seqNum << "\n");
			}
		}
		
		if(!frameStream.empty())
		{
			//send the frameStream until the entire frame stream is sent
			while((bytesSent += send(socketFD, frameStream.c_str(), frameStream.size() - bytesSent, 0)) < frameStream.size());
		}
	}
	if(lockStatus != 0 && lockStatus != EBUSY) //error trying to get the lock
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
	uint16_t numDelims = 0;

	//receive all data currently stored on the socket
	while((numBytesRecvd = recv(socketFD, recvBuffer, MaxRecvBufferSize, 0)) > 0 && numBytesRecvd <= MaxRecvBufferSize)
	{
		//add the newly received data to the frame buffer
		frameBuffer.append(recvBuffer, numBytesRecvd);

		//clear out the recv buffer
		memset(recvBuffer, 0x00, MaxRecvBufferSize + 1);
	}

	//parse out all full frames from the frameBuffer, since it is possible to have received a partial frame so far
	if(frameBuffer.find(DL_Layer::StartDelim) == 0)
	{
		
		while((endLoc = frameBuffer.find(DL_Layer::EndDelim, endLoc)) != string::npos)
		{
			numDelims = 0;
			for(uint16_t i = endLoc - 1; i != 0 && frameBuffer[i] == DL_Layer::EndDelim[0]; i--, numDelims++);

			if(numDelims % 2 == 0) // check to make sure it isn't a stuff byte found
			{
				string newFrame = frameBuffer.substr(0, endLoc + DL_Layer::DelimSize);
			
				frameBuffer.erase(0, endLoc + DL_Layer::DelimSize);
			
				//get the recvLock to add new frame
				if(pthread_mutex_lock(&recvLock) == 0)
				{
					//have the lock.. add the new frame to the queue
					recvQueue.push(newFrame);
					pthread_mutex_unlock(&recvLock);
				}
				else
				{
				cout << "ERROR: Unable to get recvlock in trytorecv()\n";
				}

				endLoc = 0;
			}
			else //sequence found was a stuffed sequence
			{
				endLoc += DL_Layer::DelimSize;	//start looking after the found sequence
			}
		}
	}
	else if(frameBuffer.size() > 0)
	{
		cout << "ERROR: The frame buffer in the physical layer is not aligned properly\n";
	}
}

void PH_Layer::ph_send(string frameStream, uint16_t seqNum, bool isAck)
{
	if(pthread_mutex_lock(&sendLock) == 0)
	{
		//add the frame to the physical layers send queue to send
		sendQueue.push(make_pair(make_pair(frameStream, seqNum), isAck));

		pthread_mutex_unlock(&sendLock);
	}
	else
	{
		cout << "ERROR: Unable to get sendLock in ph_send()\n";
	}
}

string PH_Layer::ph_recv()
{
	string frameStream = "";
	int lockReturn = 0;

	if((lockReturn = pthread_mutex_lock(&recvLock)) == 0)
	{
		if(recvQueue.size() > 0)
		{
			frameStream = recvQueue.front();
			recvQueue.pop();
		}
		pthread_mutex_unlock(&recvLock);
	}
	else
	{
		cout << "ERROR: unable to get recvLock in ph_recv()\n";
		switch(lockReturn)
		{
		case EINVAL:
			cout << "1\n";
			break;
		case EAGAIN:
			cout << "2\n";
			break;
		case EDEADLK:
			cout << "3\n";
			break;
		case EPERM:
			cout << "4\n";
			break;
			
		}
	}

	return frameStream;
}
