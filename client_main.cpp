#include "physical_layer.h"

#include <iostream>
#include <stdlib.h>
using namespace std;

/*
 * Global Defs
 */
bool gLayerOutput = false;
bool gDebugOutput = false;

static uint8_t timeoutLength = 3;
static uint16_t slidingWindowSize = 4;
double errorRate = 0;

void parseArgs(int argc, char* argv[])
{
	for(int i = 1; i < argc; i++)
	{
		string currentArg(argv[i]);
		
		if(currentArg.find("-L") != string::npos)
		{
			gLayerOutput = true;
		}
		else if(currentArg.find("-E=") != string::npos)
		{
			errorRate = atof(&(argv[i][3]));
		}
		else if(currentArg.find("-T=") != string::npos)
		{
			timeoutLength = atof(&(argv[i][3]));
		}
		else if(currentArg.find("-W=") != string::npos)
		{
			slidingWindowSize = atof(&(argv[i][3]));
		}
	}
}

int main(int argc, char *argv[])
{

	if(argc < 2)
	{
		cout <<"Error: Must include server IP/hostname as first parameter";
		exit(0);
	}
	
	parseArgs(argc, argv);
	
	PH_Layer::startClient(argv[1], errorRate, timeoutLength, slidingWindowSize);
}

