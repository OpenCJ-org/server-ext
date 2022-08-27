///
/// Player demos, shared between libcod and cod4x
///

/**************************************************************************
 * Includes                                                               *
 **************************************************************************/

#include "shared.hpp"

#include <string.h>

/**************************************************************************
 * Defines                                                                *
 **************************************************************************/

// Useful time constants
#define SECOND  1UL
#define MINUTE  (SECOND * 60)
#define HOUR    (MINUTE * 60)

// We can hard define this because no other value may be used for CJ
#define SERVER_FRAMES_PER_SECOND        20

// In seconds. Initially, a player is allocated this amount of time worth of demo frames.
// This is re-allocated upon more than half being used
#define INITIAL_DEMO_TIME_ALLOCATION    (1 * HOUR) 
// Calculate the number of initial demos frames to be allocated based on the above specified duration and server fps
#define INITIAL_DEMO_FRAME_ALLOCATION   (INITIAL_DEMO_TIME_ALLOCATION * SERVER_FRAMES_PER_SECOND)
// How many extra frames a demo should get on re-allocation
// Example: initially a demo gets to be up to 1 hour, but if more than half of that is used, then it is extended by 1 hour up to 2 hours
//#define BLOCK_DEMO_FRAME_ALLOCATION     INITIAL_DEMO_FRAME_ALLOCATION

// Max demos a player can record during a session. For example different runs in the same session.
// Keep this semi-low, because each demo gets a decent chunk of allocated memory based on constants above
//#define MAX_NR_DEMOS_PER_PLAYER         10 

// Map-specific demos. We know the number of frames these demos have, so no excessive memory needed for these
//#define MAX_NR_GLOBAL_DEMOS             512

#define DEMO_MAGIC_NUMBER               (uint32_t)0x7fd126ea

/**************************************************************************
 * Types                                                                  *
 **************************************************************************/

typedef struct
{
    float origin[3];    // Origin of the player at this frame
    float angles[3];    // Angles of the player at this frame
    bool isKeyFrame;    // Is this frame a 'key' frame, i.e. did the player's complete run contain this frame?
                        // This may change during the demo, if the player loads back to before this frame
    int prevKeyFrame;   // When skipping backwards through demos
    int nextKeyFrame;   // When skipping forwards through demos
    // More fields after PoC
} sDemoFrame_t;

typedef struct
{
    uint32_t magic;             // Magic number to hopefully provide clear errors when unexpected memory is accessed

    bool isFirstFrameFilled;    // Whether or not the first frame is already filled (at the start, currentFrame is 0 but it has not been filled yet)
    int size;                   // Size. This can change if the demo was not yet finished

    char szName[64];            // Name of the demo
    sDemoFrame_t *pDemoFrames;  // Pointer to all demo frames (not used as handle because can be re-allocated)
    int nrAllocatedFrames;      // Number of currently allocated frames for this demo
    int currentFrame;           // Index of the last frame of this demo (actively updated)
    int lastKeyFrame;           // To remember which demo frame is the current last key frame
    //uint32_t handle;          // Unique number for this demo
} sDemo_t;

typedef struct
{
    const sDemo_t *pDemo;   // The demo that is being watched
    int selectedFrame;      // The last selected frame (i.e. player is watching this frame)
} sDemoPlayback_t;

/**************************************************************************
 * Globals                                                                *
 **************************************************************************/

// For now 1 demo per player
static sDemo_t opencj_demos[MAX_CLIENTS];

// A player can only watch 1 demo at a time
static sDemoPlayback_t opencj_playback[MAX_CLIENTS];

//static sDemo_t opencj_globalDemos[MAX_NR_GLOBAL_DEMOS];

// Incremented every time a new demo is created
// No need to have complex logic for when a demo handle is no longer valid and a spot is freed up...
// ..because uint32_t is gigantic
//static uint32_t opencj_nextFreeUniqueId = 0;

/**************************************************************************
 * Local functions                                                        *
 **************************************************************************/

static void clearDemoByPointer(sDemo_t *pDemo)
{
    printf("Clearing demo with pointer %p\n", pDemo);
    if (pDemo->magic != DEMO_MAGIC_NUMBER)
    {
        printf("Attempted to clear (uninitialized?) demo %p\n", pDemo);
        return;
    }

    delete[] pDemo->pDemoFrames;
    pDemo->pDemoFrames = nullptr;

    memset(pDemo, 0, sizeof(*pDemo));
}

static void destroyDemoForPlayer(int clientNum)
{
    printf("Destroying demo for player with clientNum %d\n", clientNum);
    sDemo_t *pDemo = &opencj_demos[clientNum];
    if ((pDemo->magic == DEMO_MAGIC_NUMBER) && pDemo->pDemoFrames) // Check if this demo is in fact allocated
    {
        clearDemoByPointer(pDemo);
    }
}

static void clearAllDemos()
{
    printf("Clearing all demos\n");
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        destroyDemoForPlayer(i);
    }
}

static bool isClientNumOutOfRange(int playerId)
{
    if ((playerId < 0) || (playerId >= MAX_CLIENTS))
    {
        stackError("clientNum %d out of range", playerId);
        return true;
    }

    return false;
}

static void createDemo(sDemo_t *pDemo, const char *szName)
{
    pDemo->magic = DEMO_MAGIC_NUMBER;

    pDemo->pDemoFrames = new sDemoFrame_t[INITIAL_DEMO_FRAME_ALLOCATION];
    memset(pDemo->pDemoFrames, 0, INITIAL_DEMO_FRAME_ALLOCATION);

    strcpy(pDemo->szName, szName);
}

static const sDemo_t *findDemoByName(const char *szDemoName)
{
    const sDemo_t *pDemo = NULL;
    for (int i = 0; i < (int)(sizeof(opencj_demos) / sizeof(opencj_demos[0])); i++)
    {
        if (opencj_demos[i].size == 0) continue;

        if (strcmp(opencj_demos[i].szName, szDemoName) == 0)
        {
            pDemo = &opencj_demos[i];
            break;
        }
    }

    if (pDemo == NULL)
    {
        printf("demo with name %s was not found or is empty\n", szDemoName);
    }
    return pDemo;
}

/**************************************************************************
 * API functions                                                          *
 **************************************************************************/

//==========================================================================
// Functions that do work for any demo                                        
//==========================================================================

void Gsc_Demo_ClearAllDemos()
{
    clearAllDemos();
}

void Gsc_Demo_NumberOfFrames()
{
    if (Scr_GetNumParam() != 1)
    {
        stackError("Expected 1 param: demoName");
        return;
    }

    const char *szDemoName = NULL;
    stackGetParamString(0, &szDemoName);
    if (!szDemoName)
    {
        stackError("Demo name is empty");
        return;
    }

    const sDemo_t *pDemo = findDemoByName(szDemoName);
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        printf("Returning %d numberOfFrames for demo %s\n", pDemo->size, szDemoName);
        stackPushInt(pDemo->size);
    }
}
void Gsc_Demo_NumberOfKeyFrames()
{
    if (Scr_GetNumParam() != 1)
    {
        stackError("Expected 1 param: demoName");
        return;
    }

    const char *szDemoName = NULL;
    stackGetParamString(0, &szDemoName);
    if (!szDemoName)
    {
        stackError("Demo name is empty");
        return;
    }

    const sDemo_t *pDemo = findDemoByName(szDemoName);
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        int nrOfKeyFrames = 0;
        int iter = 0;
        while (iter < pDemo->size)
        {
            sDemoFrame_t *pFrame = &pDemo->pDemoFrames[iter];
            if (pFrame->isKeyFrame)
            {
                nrOfKeyFrames++;
            }

            // Sanity check for development
            int nextKeyFrame = pFrame->nextKeyFrame;
            if (nextKeyFrame < iter)
            {
                printf("Key frame %d goes backwards to key frame %d??\n", iter, nextKeyFrame);
                stackPushUndefined();
                return;
            }

            // Check if we found the last key frame
            if (nextKeyFrame == iter)
            {
                break;
            }

            // Skip to next key frame. TODO: how will this go with keyframe branching?
            iter = nextKeyFrame;
        }
        printf("Returning %d numberOfKeyFrames for demo %s\n", nrOfKeyFrames, szDemoName);
        stackPushInt(nrOfKeyFrames);
    }
}

//==========================================================================
// Functions related to recording for specific demos                                        
//==========================================================================

void Gsc_Demo_CreateDemoForPlayer(int playerId)
{
    if (Scr_GetNumParam() != 1)
    {
        stackError("Expected 1 param: demoName");
        return;
    }

    const char *szDemoName = NULL;
    stackGetParamString(0, &szDemoName);
    if (!szDemoName)
    {
        stackError("Demo name is empty");
        return;
    }

    printf("Creating demo for player %d\n", playerId);

    sDemo_t *pDemo = &opencj_demos[playerId];
    createDemo(pDemo, szDemoName);

    printf("Demo created: %s\n", szDemoName);
}

void Gsc_Demo_DestroyDemoForPlayer(int playerId)
{
    /*
    int demoPointer = 0;
    stackGetParamInt(0, &demoPointer);
    if (demoPointer == 0)
    {
        stackError("Missing demo pointer");
        return;
    }
    */
    if (isClientNumOutOfRange(playerId)) return;

    printf("Destroying demo for player with clientNum %d\n", playerId);
    destroyDemoForPlayer(playerId);
}

void Gsc_Demo_AddFrame(int playerId)
{
    if (isClientNumOutOfRange(playerId)) return;

    if (Scr_GetNumParam() != 3)
    {
        stackError("AddFrame expects 3 arguments: origin, angles, isKeyFrame");
        return;
    }

    if (stackGetParamType(0) != STACK_VECTOR)
    {
        stackError("Argument 1 (origin) is not a vector");
        return;
    }
    vec3_t origin;
    stackGetParamVector(0, origin);

    if (stackGetParamType(1) != STACK_VECTOR)
    {
        stackError("Argument 2 (angles) is not a vector");
        return;
    }
    vec3_t angles;
    stackGetParamVector(1, angles);

    int keyFrame = 0;
    if (!stackGetParamInt(2, &keyFrame))
    {
        stackError("Argument 3 (isKeyFrame) is not an integer");
        return;
    }

    printf("[%d] adding frame to demo\n", playerId);

    sDemo_t *pDemo = &opencj_demos[playerId];

    bool isFirstFrame = !pDemo->isFirstFrameFilled;
    int idxCurrentFrame = pDemo->currentFrame;
    int idxNewFrame = pDemo->currentFrame;
    if (!isFirstFrame) // First frame might not be filled in yet
    {
        idxNewFrame++;
    }

    sDemoFrame_t *pCurrentFrame = &pDemo->pDemoFrames[idxCurrentFrame];
    sDemoFrame_t *pNewFrame = &pDemo->pDemoFrames[idxNewFrame];

    // Fill in new frame
    memcpy(pNewFrame->origin, origin, sizeof(pNewFrame->origin));
    memcpy(pNewFrame->angles, angles, sizeof(pNewFrame->angles));

printf("[%d] frame %d has origin (%.1f, %.1f, %.1f)\n", playerId, idxCurrentFrame,
                    origin[0], origin[1], origin[2]);

    pNewFrame->isKeyFrame = (keyFrame > 0);
    if (isFirstFrame)
    {
        pNewFrame->prevKeyFrame = idxCurrentFrame;
    }
    else
    {
        if (pCurrentFrame->isKeyFrame)
        {
            pNewFrame->prevKeyFrame = idxCurrentFrame;
        }
        else
        {
            pNewFrame->prevKeyFrame = pCurrentFrame->prevKeyFrame;
        }
    }

    // If the new frame is a key frame, update all previous non-key frames to point to this frame
    if (!isFirstFrame && pNewFrame->isKeyFrame)
    {
        for (int i = pDemo->lastKeyFrame; i < idxNewFrame; i++)
        {
            sDemoFrame_t *pTmpFrame = &pDemo->pDemoFrames[i];
            pTmpFrame->nextKeyFrame = idxNewFrame;
        }

        // Update the last key frame of the demo
        pDemo->lastKeyFrame = idxNewFrame;
    }
    else
    {
        pNewFrame->nextKeyFrame = idxNewFrame;
    }

    pDemo->size++;
    pDemo->currentFrame++;

    // TODO: key frame branching (player loads)
    // TODO: re-allocate pDemoFrames if we're over halfway

    printf("Added frame %d to demo of player %d\n", idxNewFrame, playerId);
}

void Gsc_Demo_Completed(int playerId)
{
    // TODO: keyframe branching?
}

//==========================================================================
// Functions related to playback & control                                        
//==========================================================================

void Gsc_Demo_SelectPlaybackDemo(int playerId)
{
    if (isClientNumOutOfRange(playerId)) return;

    if (Scr_GetNumParam() != 1)
    {
        stackError("Expected 1 argument: demoName");
        return;
    }

    const char *szDemoName = NULL;
    if (!stackGetParamString(0, &szDemoName) || (szDemoName == NULL))
    {
        stackError("Argument 1 (demoName) is not a string");
        return;
    }

    printf("[%d] requesting playback demo %s\n", playerId, szDemoName);

    // Clear the player's current playback state
    sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    pPlayback->selectedFrame = 0;
    pPlayback->pDemo = findDemoByName(szDemoName);
    if (!pPlayback->pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        printf("[%d] requested playback demo was found\n", playerId);
        stackPushInt(1);
    }
}

void Gsc_Demo_ReadFrame_Origin(int playerId)
{
    if (isClientNumOutOfRange(playerId)) return;

    const sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    const sDemo_t *pDemo = pPlayback->pDemo;
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        const sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame];
        stackPushVector(pDemoFrame->origin);
    }
}
void Gsc_Demo_ReadFrame_Angles(int playerId)
{
    if (isClientNumOutOfRange(playerId)) return;

    const sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    const sDemo_t *pDemo = pPlayback->pDemo;
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        const sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame];
        stackPushVector(pDemoFrame->angles);
    }
}
static void Base_Gsc_Demo_FrameSkip(int playerId, int nrToSkip, bool areKeyFrames) // Helper function for skipping frames and keyframes
{
    if (isClientNumOutOfRange(playerId)) return;

    //printf("[%d] is skipping %d %sframes\n", playerId, nrToSkip, areKeyFrames ? "key" : "");

    sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    const sDemo_t *pDemo = pPlayback->pDemo;
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        // If we're requested to skip n keyframes, then we need to figure out how many real frames that is
        if (areKeyFrames)
        {
            if (nrToSkip != 0)
            {
                int nrKeyFramesToSkip = abs(nrToSkip);      // The total number of key frames we need to skip
                int nrKeyFramesSkipped = 0;                 // The number of key frames skipped so far
                int prevFrame = pPlayback->selectedFrame;   // The previous frame that was processed, used to detect last frame
                int currFrame = pPlayback->selectedFrame;   // The current frame being processed
                bool isReverse = (nrToSkip < 0);            // Whether the skipping needs to be in reverse (true) or forward (false)
                // OK, now for the actual key frame skipping
                for (int i = 0; i < nrKeyFramesToSkip; i++)
                {
                    // First we select the "next" key frame (next can be previous as well if we are searching in reverse)
                    const sDemoFrame_t *pCurrFrame = &pDemo->pDemoFrames[currFrame];
                    int nextKeyFrame = isReverse ? pCurrFrame->prevKeyFrame : pCurrFrame->nextKeyFrame;

                    // Check if we skipped any frames
                    if (nextKeyFrame == currFrame)
                    {
                        // We didn't get anywhere, so this must have been the last key frame
                        printf("[%d] found last keyframe: %d\n", playerId, prevFrame);
                        break;
                    }

                    // Check if we skipped enough key frames
                    if (++nrKeyFramesSkipped >= nrKeyFramesToSkip)
                    {
                        printf("[%d] skipped enough keyframes (%d)\n", playerId, nrKeyFramesSkipped);
                        break;
                    }

                    currFrame = nextKeyFrame;
                }

                // We overwrite this with the number of frames to skip (rather than number of key frames), so we can re-use the code below
                // No check for reverse, because we need this to be negative if skipping backwards, and positive if skipping forward
                nrToSkip = (currFrame - pPlayback->selectedFrame);
            }
        }

        int requestedFrame = pPlayback->selectedFrame + nrToSkip;
        if (requestedFrame >= (pDemo->size - 1)) // - 1 because we're comparing an index to a size
        {
            printf("[%d] can't select next frame, demo is finished\n", playerId);
            stackPushInt(pDemo->size - 1);
        }
        else if (requestedFrame < 0) // TODO: start & end, 0 may not be begin
        {
            printf("[%d] can't select previous frame, demo is at start\n", playerId);
            stackPushInt(0);
        }
        else
        {
            float origin[3];
            memcpy(origin, pPlayback->pDemo->pDemoFrames[requestedFrame].origin, sizeof(origin));

            printf("[%d] selecting frame %d with origin (%.1f, %.1f, %.1f)\n", playerId, requestedFrame,
                    origin[0], origin[1], origin[2]);
            pPlayback->selectedFrame = requestedFrame;
            stackPushInt(requestedFrame);
        }
    }
}
void Gsc_Demo_SkipFrame(int playerId)
{
    if (Scr_GetNumParam() != 1)
    {
        stackError("Expected 1 argument: nrFramesToSkip");
        return;
    }

    int nrFramesToSkip = 0;
    if (!stackGetParamInt(0, &nrFramesToSkip))
    {
        stackError("Argument 1 (nrFramesToSkip) is not an int");
        return;
    }

    Base_Gsc_Demo_FrameSkip(playerId, nrFramesToSkip, false);
}
void Gsc_Demo_SkipKeyFrame(int playerId)
{
    if (Scr_GetNumParam() != 1)
    {
        stackError("Expected 1 argument: nrKeyFramesToSkip");
        return;
    }

    int nrKeyFramesToSkip = 0;
    if (!stackGetParamInt(0, &nrKeyFramesToSkip))
    {
        stackError("Argument 1 (nrKeyFramesToSkip) is not an int");
        return;
    }

    Base_Gsc_Demo_FrameSkip(playerId, nrKeyFramesToSkip, true);
}
void Gsc_Demo_NextFrame(int playerId)
{
    Base_Gsc_Demo_FrameSkip(playerId, 1, false);
}
void Gsc_Demo_PrevFrame(int playerId)
{
    Base_Gsc_Demo_FrameSkip(playerId, -1, false);
}
void Gsc_Demo_ReadFrame_NextKeyFrame(int playerId)
{
    Base_Gsc_Demo_FrameSkip(playerId, 1, true);
}
void Gsc_Demo_ReadFrame_PrevKeyFrame(int playerId)
{
    Base_Gsc_Demo_FrameSkip(playerId, -1, true);
}
