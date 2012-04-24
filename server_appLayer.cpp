#include "server_appLayer.h"
#include "physical_layer.h"

#include <iostream>
#include <errno.h>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <netinet/in.h>
#include <unistd.h>
#include "Output_macros.h"
using namespace std;
/*
 * Class static definitions
 */
const char* ServerApp::UserDBFileName = ".userDB";
const char* ServerApp::MD5SumFileName = ".md5_output";

ServerApp::ServerApp(void* dlPtr) : dl_layer(reinterpret_cast<DL_Layer*>(dlPtr)), 
									userLoggedIn(false)
{
	DEBUG_OUTPUT("Server App layer created\n");	
}
void ServerApp::startControlLoop()
{
	string message;
	
	DL_Layer::disableSigalrm();
	
	while(1)
	{
		//only block when downloading is not pending
		message = dl_layer->dl_recv(!isDownloadPending);
		
		if(message != "")
		{
			switch(message[0])
			{
			case eLogin:
				handleLogin(message);
				break;
			case eDownloadStart:
				if(userLoggedIn)
				{
					handleDownloadStart(message);
				}
				else
				{
					handleNotLoggedIn();
				}
				break;
			case eDownloadPause:
				if(userLoggedIn)
				{
					handleDownloadPause();
				}
				else
				{
					handleNotLoggedIn();
				}
				break;
			case eDownloadResume:
				if(userLoggedIn)
				{
					handleDownloadResume(message);
				}
				else
				{
					handleNotLoggedIn();
				}
				break;
			case eDownloadCancel:
				if(userLoggedIn)
				{
					handleDownloadCancel(message);
				}
				else
				{
					handleNotLoggedIn();
				}
				break;
			case eList:
				if(userLoggedIn)
				{
					handleList();
				}
				else
				{
					handleNotLoggedIn();
				}
				break;
			case eLogout:
				handleLogout();
			}
		}
		
		if(isDownloadPending)
		{
			string dataPacket;
			char data[DL_Layer::MaxMessageSize - 1] = {0};
			
			dataPacket = (char)eData;
			
			downloadFS->read(data, DL_Layer::MaxMessageSize - 1);	
			
			
			
			if(downloadFS->eof())
			{
				if(downloadFS->gcount() > 0)
				{
					dataPacket.append(data, downloadFS->gcount());
					dl_layer->dl_send(dataPacket);
				}
				
				isDownloadPending = false;
				delete(downloadFS);
			}
			else
			{
				dataPacket.append(data, downloadFS->gcount());
				dl_layer->dl_send(dataPacket);				
			}
		}
	}
}

void ServerApp::handleLogin(string message)
{
	string response;
	response = (char)eLogin;
	
	if(userLoggedIn)
	{
		response += (char)eUserLoggedIn;
	}
	else
	{
		bool endOfUsers = false;
		string currentUser, currentPassword, user, password;
		
		//open user/password database file		
		ifstream userDB;
		userDB.exceptions(ifstream::failbit | ifstream::badbit | ifstream::eofbit);
		
		userDB.open(UserDBFileName, ifstream::in);
		
		//extract user/password from message
		user = message.substr(1, message.find_first_of('@') - 1);
		password = message.substr(message.find_first_of('@') + 1, 20);
		
		while(!endOfUsers)
		{
			try
			{
				userDB >> currentUser >> currentPassword;
				
				if(currentUser == user)
				{
					if(currentPassword == password)
					{
						string dir("./");
						
						userLoggedIn = true;
						loggedInUser = user;
						
						dir.append(loggedInUser);
						
						chdir(dir.c_str());
						
						response += (char)eLoginSuccess;
					}
					else
					{
						response += (char)eInvalidPassword;
					}
				}				
			}
			catch(...)
			{
				endOfUsers = true;
				response += (char)eInvalidUserName;
			}
			
		}				
	}
	
	//send response
	dl_layer->dl_send(response);
}

void ServerApp::handleNotLoggedIn()
{
	string response;
	response = (char)eLogin;
	response += (char)eUserNotLoggedIn;
	
	dl_layer->dl_send(response);
}


void ServerApp::handleDownloadStart(string message)
{
	string filename = message.substr(1), response;
	uint32_t fileSize = 0;
	
	if(!isDownloadPending)
	{
		ifstream* inFile = new ifstream;
			
		inFile->open(filename.c_str(), ifstream::in | ifstream::binary);
	
		if(inFile->is_open())	//file exists
		{
			stringstream fileStr;
			//get the file size
			inFile->seekg(0, ios::end);
			fileSize = inFile->tellg();
			//move back to the beginning of the file
			inFile->seekg(0, ios::beg);
			
			//indicate successful response
			response = (char)eDownloadStart;
			response += (char)eDownloadStartSuccess;
			downloadFileSize = fileSize = htonl(fileSize);
			response.append((const char*)&(fileSize), 4);
			
			isDownloadPending = true;
			downloadFileName = filename;
			downloadFS = inFile;		
		}
		else
		{
			response = (char)eDownloadStart;
			response += (char)eInvalidFileName;
			
			//delete ifstream (no longer needed
			delete(inFile);
		}
	}
	else
	{
		response = (char)eDownloadStart;
		response += (char)eDownloadPending;
	}
	//send response
	dl_layer->dl_send(response);
}

void ServerApp::handleDownloadPause()
{
	string response;
	
	if(isDownloadPending)
	{		
		//save download data for later use
		pausedDownloadQueue.push_back(make_pair(make_pair(loggedInUser, downloadFileName), downloadFS->tellg()));
		isDownloadPending = false;
		downloadFS->close();
		delete(downloadFS);
		
		//now send response
		response = (char)eDownloadPause;
		response += (char)eDownloadPauseSuccess;
	}
	else
	{
		response = (char)eDownloadPause;
		response += (char)eDownloadPauseError;
	}
	
	dl_layer->dl_send(response);
}

void ServerApp::handleDownloadResume(string message)
{
	bool fileFound = false;
	uint32_t clientFileSize = ntohl(*((uint32_t*)&(message[1])));
	string filename = message.substr(5), response;
	response = (char)eDownloadResume;
	
	if(!isDownloadPending)
	{
		//search for the file in the pending downloads
		for(vector<pair<pair<string, string>, uint32_t > >::iterator i = pausedDownloadQueue.begin(); i != pausedDownloadQueue.end() && !fileFound; i++)
		{
			if((*i).first.first == loggedInUser)
			{
				if((*i).first.second == filename)
				{
					//check that the clients file size equals the paused position of the download
					if(clientFileSize == (*i).second)
					{
						//found file - restart download
						uint32_t fileSize = 0;
						ifstream* inFile = new ifstream;
						inFile->open(filename.c_str(), ifstream::in | ifstream::binary);
						inFile->seekg(0, ios::end);
						downloadFileSize = fileSize = inFile->tellg();
						inFile->seekg((*i).second, ios::beg);
					
						isDownloadPending = true;
						downloadFileName = filename;
						downloadFS = inFile;
						
						//now setup response
						response += (char)eDownloadResumeSuccess;
						fileSize = htonl(fileSize);
						response.append((const char*)&(fileSize), 4);
						
						fileFound = true;
						
						pausedDownloadQueue.erase(i);
					}
					else
					{
						response += (char)eDownloadResumeSizeError;
					}
				}
			}
		}
		
		if(!fileFound)
		{
			response += (char)eDownloadResumeInvalid;
		}
	}	
	else //Already downloading file (server is busy)
	{
		response += (char)eDownloadResumeBusy;
	}
	
	dl_layer->dl_send(response);
}

void ServerApp::handleDownloadCancel(string message)
{
	bool fileFound = false;
	string filename = message.substr(1), response;
	response = (char)eDownloadCancel;
	
	//first check if it is the current download
	if(isDownloadPending)
	{
		if(downloadFileName == filename) //cancel download
		{
			isDownloadPending = false;
			downloadFS->close();
			delete(downloadFS);
			
			response += (char)eDownloadCancelDLSuccess;
			fileFound = true;
		}
	}
	
	if(!fileFound) //now check pending downloads
	{
		for(vector<pair<pair<string, string>, uint32_t > >::iterator i = pausedDownloadQueue.begin(); i != pausedDownloadQueue.end() && !fileFound; i++)
		{
			if((*i).first.first == loggedInUser)
			{
				if((*i).first.second == filename)
				{
					pausedDownloadQueue.erase(i);
					
					fileFound = true;
					response += (char)eDownloadCancelSuccess;
				}
			}
		}
	}
	
	if(!fileFound) //didn't find file
	{
		response += (char)eDownloadCancelFNF;
	}
	
	//send response
	dl_layer->dl_send(response);
}

void ServerApp::handleList()
{
	uint32_t numEntries = 0, fileSize = 0;
	uint8_t percentComplete = 0;
	bool limitReached = false;
	ifstream* inFile = new ifstream;
	string response, tempResponse;
	response = (char)eList;
	response.append((const char*)&(fileSize), 4);		
	
	if(isDownloadPending)
	{
		response.append(downloadFileName);
		percentComplete = (downloadFS->tellg() * 100) / downloadFileSize;
		response.append((const char*)&(percentComplete), 1);
		
		numEntries++;
	}	
	
	tempResponse = response;

	for(vector<pair<pair<string, string>, uint32_t > >::iterator i = pausedDownloadQueue.begin(); i != pausedDownloadQueue.end() && !limitReached; i++)
	{
		if((*i).first.first == loggedInUser)
		{
			//add file name to list
			tempResponse.append((*i).first.second);
			tempResponse.push_back('@');
			
			//get percent complete
			inFile->open((*i).first.second.c_str(), ifstream::in | ifstream::binary);
			inFile->seekg(0, ios::end);
			fileSize = inFile->tellg();
			
			percentComplete = (fileSize * 100) / (*i).second;
			tempResponse.append((const char*)&(percentComplete), 1);
			
			inFile->close();
			
			if(tempResponse.size() < DL_Layer::MaxMessageSize)
			{
				response = tempResponse;
				numEntries++;
			}
			else
			{
				limitReached = true;
			}
		}
	}
	
	//now copy over real number of entries
	numEntries = ntohl(numEntries);
	response.replace(1, 4, (const char*)&(numEntries));
	
	//send response
	dl_layer->dl_send(response);
}

void ServerApp::handleLogout()
{
	string response;
	response = (char)eLogout;
	
	if(userLoggedIn)
	{
		string filename = loggedInUser;
		filename.append("-Stats");
		
		dl_layer->writeStatsToFile(filename);
		
		response += (char)eLogoutSuccess;
	}
	else
	{
		response += (char)eLogoutNotLoggedIn;
	}
	
	//send response
	dl_layer->dl_send(response);
}