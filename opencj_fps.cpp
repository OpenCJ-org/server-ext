///
/// FPS determination, shared between libcod and cod4x
///

/**************************************************************************
 * Includes                                                               *
 **************************************************************************/

#include "shared.hpp"

#include <math.h>
#include <string.h>

/**************************************************************************
 * Defines                                                                *
 **************************************************************************/

#define NR_SAMPLES_FPS_AVERAGING 20

/**************************************************************************
 * Globals                                                                *
 **************************************************************************/

// For client FPS calculation
static int opencj_clientFrameTimes[MAX_CLIENTS][NR_SAMPLES_FPS_AVERAGING] = {{0}}; // Client frame times storage, per client, with x samples
static int opencj_clientFrameTimesSampleIdx[MAX_CLIENTS] = {0}; // Index in opencj_clientFrameTimes, per client
static int opencj_prevClientFrameTimes[MAX_CLIENTS] = {0};
static int opencj_avgFrameTimeMs[MAX_CLIENTS] = {0};

/**************************************************************************
 * API fucntions                                                          *
 **************************************************************************/

bool opencj_updatePlayerFPS(int clientNum, int time, int *pAvgFrameTimeMs)
{
	opencj_clientFrameTimes[clientNum][opencj_clientFrameTimesSampleIdx[clientNum]] = time - opencj_prevClientFrameTimes[clientNum];
	opencj_prevClientFrameTimes[clientNum] = time;
	
    // There are x sample slots, if all are used we restart at begin
	if (++opencj_clientFrameTimesSampleIdx[clientNum] >= NR_SAMPLES_FPS_AVERAGING)
    {
		opencj_clientFrameTimesSampleIdx[clientNum] = 0;
    }

    // Sum frame times so we can use it to calculate the average
	float sumFrameTime = 0;
	for (int i = 0; i < NR_SAMPLES_FPS_AVERAGING; i++)
	{
        sumFrameTime += (float)opencj_clientFrameTimes[clientNum][i];
    }

    // Check if client frame time is different from what we previously reported
    bool hasFPSChanged = false;
	int avgFrameTime = (int)round(sumFrameTime / NR_SAMPLES_FPS_AVERAGING);
	if (opencj_avgFrameTimeMs[clientNum] != avgFrameTime)
	{
        // Client FPS changed, report this to GSC via callback
		opencj_avgFrameTimeMs[clientNum] = avgFrameTime;
        if (pAvgFrameTimeMs)
        {
            *pAvgFrameTimeMs = avgFrameTime;
            hasFPSChanged = true;
        }
	}

    return hasFPSChanged;
}

void opencj_clearPlayerFPS(int clientNum)
{
    memset(opencj_clientFrameTimes[clientNum], 0, sizeof(opencj_clientFrameTimes[clientNum]));
    opencj_clientFrameTimesSampleIdx[clientNum] = 0;
    opencj_prevClientFrameTimes[clientNum] = 0;
    opencj_avgFrameTimeMs[clientNum] = 0;
}
