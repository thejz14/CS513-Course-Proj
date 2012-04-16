#include "server_appLayer.h"
#include "physical_layer.h"

#include <fstream>
#include <iostream>
#include <stdlib.h>
using namespace std;
/*
 * Class static definitions
 */
const char* ServerApp::UserDBFileName = ".userDB";


ServerApp::ServerApp(void* dlPtr) : dl_layer(reinterpret_cast<DL_Layer*>(dlPtr)), 
									userLoggedIn(false)
{
	
	DL_Layer::disableSigalrm();
	
	startControlLoop();
}

void ServerApp::startControlLoop()
{
	string message;
	
	while(1)
	{
		message = dl_layer->dl_recv();
		
		switch(message[0])
		{
		case(eLogin):
				handleLogin(message);
				break;
		}
	}
}

void ServerApp::handleLogin(string message)
{
	string response = "";
	
	if(userLoggedIn)
	{
		response = eUserLoggedIn;
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
		user = message.substr(1, message.find_first_of(char(0x00)));
		password = message.substr(message.find_first_of(char(0x00)) + 1, 20);
		
		while(!endOfUsers)
		{
			try
			{
				userDB >> currentUser >> currentPassword;
				
				if(currentUser == user)
				{
					if(currentPassword == password)
					{
						userLoggedIn = true;
						loggedInUser = user;
						
						response = eLoginSuccess;
					}
					else
					{
						response = eInvalidPassword;
					}
				}				
			}
			catch(...)
			{
				endOfUsers = true;
				response = eInvalidUserName;
			}
			
		}				
	}
	
	//send response
	dl_layer->dl_send(response);
}
