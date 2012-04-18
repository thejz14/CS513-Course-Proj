#include "client_appLayer.h"

#include <iostream>
#include <stdlib.h>
#include <netinet/in.h>
#include <sstream>
#include "server_appLayer.h"
#include "Output_macros.h"
using namespace std;

ClientApp::ClientApp(void* dlPtr) : dl_layer(reinterpret_cast<DL_Layer*>(dlPtr)), isDownloadPending(false)
{
	DEBUG_OUTPUT("Client IO layer launched\n");
	
	pthread_mutex_init(&commandSendLock, NULL);
	pthread_mutex_init(&commandRecvLock, NULL);
	pthread_mutex_init(&ioLock, NULL);
	pthread_cond_init(&recvdMsg, NULL);
	
	pthread_create(&backgroundThread, NULL, startBackgroundControlLoopHelper, this);
}


void* ClientApp::startBackgroundControlLoopHelper(void* thisPtr)
{
	reinterpret_cast<ClientApp*>(thisPtr)->startBackgroundControlLoop();
}

void ClientApp::startBackgroundControlLoop()
{	
	DL_Layer::disableSigalrm(); //should be inherited from parent, but just incase
	
	while(1)
	{
		tryToSend();
		tryToRecv();
	}
}

void ClientApp::tryToSend()
{
	
	if(pthread_mutex_lock(&commandSendLock) == 0)
	{
		if(sendMessageQueue.size() > 0)
		{
			string message = sendMessageQueue.front();
			sendMessageQueue.pop();
			
			pthread_mutex_unlock(&commandSendLock);
		
			DEBUG_OUTPUT("Background sending message\n");
			
			if(message[0] == ServerApp::eDownloadStart)		//save filename for possible later use
			{
				downloadFileName = message.substr(1);
			}
			
			dl_layer->dl_send(message);
		}
		else
		{
			pthread_mutex_unlock(&commandSendLock);
		}
	}
	else
	{
		CLIENT_OUT(ioLock, "ERROR: Unable to get command lock in Client App tryToSend()\n");
	}
}

void ClientApp::tryToRecv()
{
	string message;
	
	if((message = dl_layer->dl_recv(false)) != "")
	{
		if(message[0] == ServerApp::eData)
		{
			if(!isDownloadPending)
			{
				cout << "Error pending\n";
			}
			downloadFS->write(&(message.c_str()[1]), message.size() - 1);
			
			if(downloadFS->tellp() == downloadFileSize)
			{
				CLIENT_OUT(ioLock, "Download complete\n");
				
				isDownloadPending = false;
				downloadFS->close();
				delete(downloadFS);
			}
		}
		else
		{
			//if it is the start of a download - setup the file for receiving data
			if(message[0] == ServerApp::eDownloadStart)
			{
				handleDownloadStartBG(message);
			}
			
			//TODO if upload resume - start sending data
			if(pthread_mutex_lock(&commandRecvLock) == 0)
			{
				recvMessageQueue.push(message);
				
				pthread_mutex_unlock(&commandRecvLock);
				
				pthread_cond_signal(&recvdMsg);	//wake up possible waiting IO thread
			}
			else
			{
				CLIENT_OUT(ioLock, "Error: Unable to get commandRecvLock in tryToRecv\n");
			}
		}
	}
}

void ClientApp::handleDownloadStartBG(string message)
{
	downloadFS = new ofstream;
	downloadFS->open(downloadFileName.c_str(), ofstream::out | ofstream::binary | ofstream::trunc);
	
	//parse out file size and convert it
	downloadFileSize = *((uint32_t*)&(message[2]));
	
	downloadFileSize  = ntohl(downloadFileSize);
	
	isDownloadPending = true;
}

void ClientApp::startIOControlLoop()
{
	string command, subcommand;
	
	DL_Layer::disableSigalrm();
	
	cout << endl;
	
	while(1)
	{
		CLIENT_OUT(ioLock, "Enter command:");
		cin >> command;
		
		if(command == "login")
		{
			DEBUG_OUTPUT("Login command received\n");
			
			loginHandler();
			waitForResponse();
		}
		else if(command == "download")
		{
			cin >>subcommand;
			
			if(subcommand == "start")
			{
				downloadStartHandler();
				waitForResponse();
			}
			else if(subcommand == "pause")
			{
				downloadPauseHandler();
				waitForResponse();
			}
			else if(subcommand == "resume")
			{
				downloadResumeHandler();
				waitForResponse();
			}
			else if(subcommand == "cancel")
			{
				downloadCancelHandler();
				waitForResponse();
			}
			else
			{
				CLIENT_OUT(ioLock, "Unknown sub command\n");
			}
		}
		else if(command == "list")
		{
			listHandler();
			waitForResponse();
		}
		else if(command == "stats")
		{
			if(pthread_mutex_lock(&ioLock) == 0)
			{
				dl_layer->writeStats();
				pthread_mutex_unlock(&ioLock);
			}
			else
			{
				cout << "ERROR: Unable to get the IO lock";
			}
			break;
		}
		else
		{
			CLIENT_OUT(ioLock, "Unknown Command\n");
		}
	}
}

void ClientApp::waitForResponse()
{
	string response;
	
	pthread_mutex_lock(&commandRecvLock);
	
	while(recvMessageQueue.size() == 0)
	{
		pthread_cond_wait(&recvdMsg, &commandRecvLock);
	}
	
	response = recvMessageQueue.front();
	recvMessageQueue.pop();
	
	pthread_mutex_unlock(&commandRecvLock);
	
	switch(response[0])
	{
	case ServerApp::eLogin:
		loginResponseHandler(response);
		break;
	
	case ServerApp::eDownloadStart:
		downloadStartResponseHandler(response);
		break;
		
	case ServerApp::eDownloadPause:
		downloadPauseResponseHandler(response);
		break;
		
	case ServerApp::eDownloadResume:
		downloadResumeResponseHandler(response);
		break;
		
	case ServerApp::eDownloadCancel:
		downloadCancelResponseHandler(response);
		break;
		
	case ServerApp::eList:
		listResponseHandler(response);
		break;
	}
}

void ClientApp::loginHandler()
{
	string username, password;
	cin >> username >> password;
	
	if(username.size() > MaxUserNameSize)
	{
		CLIENT_OUT(ioLock, "Username is greater than max size of 30\n");
	}
	else if (password.size() > MaxPasswordSize)
	{
		CLIENT_OUT(ioLock, "Password is greater than max size of 30\n");
	}
	else
	{
		string message;
		message = (char)(ServerApp::eLogin);
		message.append(username);
		message.push_back('@');
		message.append(password);
		
		if(pthread_mutex_lock(&commandSendLock) == 0)
		{
			DEBUG_OUTPUT("CA: Adding login message to queue\n");
			
			sendMessageQueue.push(message);
			pthread_mutex_unlock(&commandSendLock);
		}
		else
		{
			CLIENT_OUT(ioLock, "ERROR:Unable to get message lock in client App layer\n");
		}
	}
}

void ClientApp::loginResponseHandler(string response)
{
	switch(response[1]) //status byte
	{
	case ServerApp::eLoginSuccess:
		CLIENT_OUT(ioLock, "Login successful\n");
		break;
	
	case ServerApp::eInvalidUserName:
		CLIENT_OUT(ioLock, "Login failure: Unknown username\n");
		break;
		
	case ServerApp::eInvalidPassword:
		CLIENT_OUT(ioLock, "Login Failure: Invalid password\n");
		break;
	
	case ServerApp::eUserLoggedIn:
		CLIENT_OUT(ioLock, "Login Failure: User is already logged in\n");
		break;
		
	case ServerApp::eUserNotLoggedIn:
		CLIENT_OUT(ioLock, "Unable to perform command. You are not logged in\n");
	}
}

void ClientApp::downloadStartHandler()
{
	string filename;
	cin >> filename;
	
	if(filename.size() > 30)
	{
		CLIENT_OUT(ioLock, "File name cannot be greater than max size of 30\n");
	}
	else
	{
		string message;
		message = (char)(ServerApp::eDownloadStart);
		message.append(filename);
		
		if(pthread_mutex_lock(&commandSendLock) == 0)
		{
			DEBUG_OUTPUT("CA: Adding download message to queue\n");
			
			sendMessageQueue.push(message);
			pthread_mutex_unlock(&commandSendLock);
		}
		else
		{
			CLIENT_OUT(ioLock, "ERROR:Unable to get message lock in client App layer\n");
		}
	}	
}

void ClientApp::downloadStartResponseHandler(string response)
{
	if(response[1] == ServerApp::eDownloadStartSuccess)
	{
		CLIENT_OUT(ioLock, "Download started succesfully\n");
	}
	else if(response[1] == ServerApp::eInvalidFileName) //file doesn't exist
	{
		CLIENT_OUT(ioLock, "File does not exist\n");
	}
	else
	{
		CLIENT_OUT(ioLock, "There is currently a download pending\n");
	}
}

void ClientApp::downloadPauseHandler()
{
	string message;
	message = (char)(ServerApp::eDownloadPause);
	
	if(pthread_mutex_lock(&commandSendLock) == 0)
	{	
		sendMessageQueue.push(message);
		pthread_mutex_unlock(&commandSendLock);
	}
	else
	{
		CLIENT_OUT(ioLock, "ERROR:Unable to get message lock in client App layer\n");
	}
}

void ClientApp::downloadPauseResponseHandler(string response)
{
	switch(response[1])
	{
	case ServerApp::eDownloadPauseSuccess:
		isDownloadPending = false;
		downloadFS->close();
		delete(downloadFS);
		
		CLIENT_OUT(ioLock, "The download was paused successfully\n");
		
		break;
	case ServerApp::eDownloadPauseError:
		CLIENT_OUT(ioLock, "There is currently not a download pending to pause\n");
		break;	
	}	
}

void ClientApp::downloadResumeHandler()
{
	string filename;
	cin >> filename;
	
	if(!isDownloadPending)
	{
		if(filename.size() > 30)
		{
			CLIENT_OUT(ioLock, "File name cannot be greater than max size of 30\n");
		}
		else
		{			
			uint32_t currentFileSize;
			ofstream* outFile = new ofstream;
			outFile->open(filename.c_str(), ofstream::out | ofstream::binary | ofstream::app);
			
			if(outFile->is_open())
			{
				uint32_t fileSize;
				string message;
				
				outFile->seekp(0, ios::end);
				fileSize = outFile->tellp();
				fileSize = htonl(fileSize);
				
				message = (char)(ServerApp::eDownloadResume);
				message.append((const char*)&(fileSize), 4);
				message.append(filename);
				
				downloadFS = outFile;
				
				if(pthread_mutex_lock(&commandSendLock) == 0)
				{
					sendMessageQueue.push(message);
					pthread_mutex_unlock(&commandSendLock);
				}
				else
				{
					CLIENT_OUT(ioLock, "ERROR: Unable to get message lock in client App layer\n");
				}
			}
			else
			{
				CLIENT_OUT(ioLock, "File does not exist on this machine. Unable to resume. Please try a download start instead.\n");
			}
		}
	}
	else
	{
		CLIENT_OUT(ioLock, "There is already a file being downloaded.\n");
	}
}

void ClientApp::downloadResumeResponseHandler(string response)
{
	switch(response[1])
	{
	case ServerApp::eDownloadResumeSuccess:
		isDownloadPending = true;
		downloadFileSize = *((uint32_t*)&(response[2]));
		downloadFileSize  = ntohl(downloadFileSize);
		CLIENT_OUT(ioLock, "Download resumed succesfully\n");
		break;
	case ServerApp::eDownloadResumeInvalid:
		CLIENT_OUT(ioLock, "Error: Paused download does not exist.\n");
		delete(downloadFS);
		break;
	case ServerApp::eDownloadResumeSizeError:
		CLIENT_OUT(ioLock, "Error: The position of the paused download on the server and the size on this machine do not match.\n");
		break;
	}
}

void ClientApp::downloadCancelHandler()
{
	string filename, message;
	cin >> filename;
	
	message = (char)(ServerApp::eDownloadCancel);
	message.append(filename);
	
	if(pthread_mutex_lock(&commandSendLock) == 0)
	{	
		sendMessageQueue.push(message);
		pthread_mutex_unlock(&commandSendLock);
	}
	else
	{
		CLIENT_OUT(ioLock, "ERROR:Unable to get message lock in client App layer\n");
	}
}

void ClientApp::downloadCancelResponseHandler(string response)
{
	switch(response[1])
	{
	case ServerApp::eDownloadCancelSuccess:
		CLIENT_OUT(ioLock, "Paused download was canceled successfully.\n");
		break;
	case ServerApp::eDownloadCancelDLSuccess:
		isDownloadPending = false;
		downloadFS->close();
		delete(downloadFS);
		
		CLIENT_OUT(ioLock, "Current download was canceled successfully.\n");
		break;
	case ServerApp::eDownloadCancelFNF:
		CLIENT_OUT(ioLock, "File not found. No download was canceled.\n");
		break;
	}
}

void ClientApp::listHandler()
{
	string message;
	message = (char)(ServerApp::eList);
	
	if(pthread_mutex_lock(&commandSendLock) == 0)
	{	
		sendMessageQueue.push(message);
		pthread_mutex_unlock(&commandSendLock);
	}
	else
	{
		CLIENT_OUT(ioLock, "ERROR:Unable to get message lock in client App layer\n");
	}
}

void ClientApp::listResponseHandler(string response)
{
	uint32_t numEntries = *((uint32_t*)&(response[1]));
	downloadFileSize  = ntohl(numEntries);
	
	if(numEntries == 0)
	{
		CLIENT_OUT(ioLock, "There are no pending downloads to list\n");
	}
	else
	{
		//TODO
	}
}