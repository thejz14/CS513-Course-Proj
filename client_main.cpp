#include "physical_layer.h"

#include <iostream>
#include <stdlib.h>
using namespace std;

int main(int argc, char *argv[])
{

	if(argc != 2)
	{
		//TODO
		cout << "Incorrect parameters: not sure what they are yet" << endl;
		exit(0);
	}
	PH_Layer::startServer(atof(argv[1]));
}