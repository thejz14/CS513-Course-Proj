#ifndef OUTPUT_MACROS_H
#define OUTPUT_MACROS_H

#include <iostream>
using namespace std;


extern bool gLayerOutput;
#define LAYER_OUTPUT(x)		if(gLayerOutput) cout << x;

extern bool gDebugOutput;
#define DEBUG_OUTPUT(x)		if(gDebugOutput) cout << x;

#endif
