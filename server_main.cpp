#include "physical_layer.h"

#include <iostream>
#include <stdlib.h>
#include <stdint.h>
using namespace std;

/*
 * Global Defs
 */
bool gLayerOutput = false;
bool gDebugOutput = false;

/*
 * static variables
 */
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
	parseArgs(argc, argv);
	
	PH_Layer::startServer(errorRate, timeoutLength, slidingWindowSize);
}