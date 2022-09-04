///
/// Player demos, shared between libcod and cod4x
///

/**************************************************************************
 * Includes                                                               *
 **************************************************************************/

#include "shared.hpp"

#include <cstring>
#include <map>

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

// Max number of demos per map that are available for playback
#define MAX_NR_DEMOS_PER_MAP            128

// Magic number to know if a demo is initialized / used for validity checking
#define DEMO_MAGIC_NUMBER               (uint32_t)0x7fd126ea

/**************************************************************************
 * Types                                                                  *
 **************************************************************************/

typedef struct
{
    float origin[3];    // Origin of the player at this frame
    float angles[3];    // Angles of the player at this frame
    short flags;		// flags that store button/stance/weapon stuff
    short fps; 			// fps
    bool saveNow;		// if this was a frame on which the player saved
    bool loadNow;		// if this was a frame on which the player loaded
    bool rpgNow;		// if this was a frame on which the player rpg'd
    bool isKeyFrame;    // Is this frame a 'key' frame, i.e. did the player's complete run contain this frame?
                        // This may change during the demo, if the player loads back to before this frame
    int prevKeyFrame;   // When skipping backwards through demos
    int nextKeyFrame;   // When skipping forwards through demos

    // More fields after PoC
} sDemoFrame_t;

typedef struct
{
    uint32_t magic;             // Magic number to hopefully provide clear errors when unexpected memory is accessed
    int id;                     // Unique ID of the demo (== runID of player)
    bool isComplete;            // Whether (or not) this demo has been completed and thus its size will not increase
    bool isFirstFrameFilled;    // Whether or not the first frame is already filled (at the start, currentFrame is 0 but it has not been filled yet)
    int size;                   // Size. This can change if the demo was not yet finished
    sDemoFrame_t *pDemoFrames;  // Pointer to all frames of this demo (not used as handle because can be re-allocated)
    int nrAllocatedFrames;      // Number of currently allocated frames for this demo
    int currentFrame;           // Index of the last frame of this demo (actively updated)
    int lastKeyFrame;           // To remember which demo frame is the current last key frame
} sDemo_t;

typedef struct
{
    const sDemo_t *pDemo;   // The demo that is being watched
    int selectedFrame;      // The last selected frame (i.e. player is watching this frame)
} sDemoPlayback_t;

/**************************************************************************
 * Globals                                                                *
 **************************************************************************/

// For now max. number of demos per map
static sDemo_t opencj_demos[MAX_NR_DEMOS_PER_MAP];
// For faster access every time an id is provided, use a map
static std::map<uint32_t, uint16_t> opencj_demoIdToIdx;

// A player can only watch 1 demo at a time
static sDemoPlayback_t opencj_playback[MAX_CLIENTS];

/**************************************************************************
 * Local functions                                                        *
 **************************************************************************/

static sDemo_t *findDemoById(int demoId)
{
    sDemo_t *pDemo = NULL;
    auto it = opencj_demoIdToIdx.find(demoId);
    if (it == opencj_demoIdToIdx.end())
    {
        printf("Demo with id %d was not found or is empty\n", demoId);
    }
    else
    {
        pDemo = &opencj_demos[it->second];
    }

    return pDemo;
}

static void clearDemoById(int demoId)
{
    auto it = opencj_demoIdToIdx.find(demoId);
    if (it != opencj_demoIdToIdx.end())
    {
        sDemo_t *pDemo = &opencj_demos[it->second];
        opencj_demoIdToIdx.erase(it);

        // Clear demo data, free up this spot
        if ((pDemo->magic != DEMO_MAGIC_NUMBER) || (pDemo->id <= 0))
        {
            printf("Demo is already clear (%p)\n", pDemo);
            return;
        }

        printf("Clearing demo with pointer %p\n", pDemo);
        if (pDemo->pDemoFrames)
        {
            delete[] pDemo->pDemoFrames;
            pDemo->pDemoFrames = nullptr;
        }

        memset(pDemo, 0, sizeof(*pDemo));
    }
}

static void clearAllDemos()
{
    printf("Clearing all demos\n");
    for (int i = 0; i < (int)(sizeof(opencj_demos) / sizeof(opencj_demos[0])); i++)
    {
        clearDemoById(opencj_demos[i].id);
    }
}

static sDemo_t *createDemo(int demoId)
{
    if (findDemoById(demoId) != NULL)
    {
        printf("Demo with id %d already exists!\n", demoId);
        return NULL;
    }

    printf("Creating demo with id %d\n", demoId);

    sDemo_t *pDemo = NULL;
    for (int i = 0; i < (int)(sizeof(opencj_demos) / sizeof(opencj_demos[0])); i++)
    {
        printf("Checking for free slot at %d\n", i);
        if (opencj_demos[i].id <= 0)
        {
            // Free slot
            pDemo = &opencj_demos[i];
            opencj_demoIdToIdx[demoId] = i;
            break;
        }
    }

    if (pDemo)
    {
        memset(pDemo, 0, sizeof(*pDemo));

        pDemo->magic = DEMO_MAGIC_NUMBER;
        pDemo->id = demoId;

        pDemo->pDemoFrames = new sDemoFrame_t[INITIAL_DEMO_FRAME_ALLOCATION];
        memset(pDemo->pDemoFrames, 0, INITIAL_DEMO_FRAME_ALLOCATION);
    }

    return pDemo;
}

/**************************************************************************
 * Helper functions                                                       *
 **************************************************************************/

static bool Base_Gsc_IsValidClientNum(int clientNum)
{
    if ((clientNum < 0) || (clientNum >= MAX_CLIENTS))
    {
        stackError("clientNum %d out of range", clientNum);
        return true;
    }

    return false;
}

static bool Base_Gsc_GetValidDemoId(int *pDemoId, int nrArgsExpected)
{
    if (!pDemoId) return false;

    int nrReceivedArgs = Scr_GetNumParam();
    if (nrReceivedArgs != nrArgsExpected)
    {
        stackError("Expected %d arguments, but got %d", nrArgsExpected, nrReceivedArgs);
        stackPushUndefined();
        return false;
    }

    int demoId = -1;
    if (stackGetParamType(0) != STACK_INT)
    {
        stackError("Argument 1 (demoId) is not an int");
        stackPushUndefined();
        return false;
    }
    stackGetParamInt(0, &demoId);

    if (demoId <= 0)
    {
        stackError("Argument 1 (demoId) is not > 0");
        stackPushUndefined();
        return false;
    }

    *pDemoId = demoId;
    return true;
}

static void Base_Gsc_Demo_FrameSkip(int playerId, int nrToSkip, bool areKeyFrames) // Helper function for skipping frames and keyframes
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

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
                //printf("Trying to skip %d keyFrames\n", nrToSkip);
                int nrKeyFramesToSkip = abs(nrToSkip);      // The total number of key frames we need to skip
                int nrKeyFramesSkipped = 0;                 // The number of key frames skipped so far
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
                        break;
                    }

                    currFrame = nextKeyFrame;

                    // Check if we skipped enough key frames
                    if (++nrKeyFramesSkipped >= nrKeyFramesToSkip)
                    {
                        //printf("[%d] skipped enough keyframes (%d)\n", playerId, nrKeyFramesSkipped);
                        break;
                    }
                }

                // We overwrite this with the number of frames to skip (rather than number of key frames), so we can re-use the code below
                // No check for reverse, because we need this to be negative if skipping backwards, and positive if skipping forward
                nrToSkip = (currFrame - pPlayback->selectedFrame);
            }
        }

        int requestedFrame = pPlayback->selectedFrame + nrToSkip;
        if (requestedFrame > (pDemo->size - 1)) // - 1 because we're comparing an index to a size
        {
            printf("[%d] can't select next frame, demo is finished\n", playerId);
            requestedFrame = pDemo->size - 1;
        }
        else if (requestedFrame < 0) // TODO: start & end, 0 may not be begin
        {
            printf("[%d] can't select previous frame, demo is at start\n", playerId);
            requestedFrame = 0;
        }
        else
        {
            // This is fine, not out of bounds
        }

        pPlayback->selectedFrame = requestedFrame;
        stackPushInt(requestedFrame);

        //printf("Requested frame skip %d -> %d, returned %d\n", pPlayback->selectedFrame, pPlayback->selectedFrame + nrToSkip, requestedFrame);
    }
}

//==========================================================================
// Functions that do work for any demo                                        
//==========================================================================

void Gsc_Demo_ClearAllDemos()
{
    clearAllDemos();
}

void Gsc_Demo_HasKeyFrames()
{
    int demoId = -1;
    if (!Base_Gsc_GetValidDemoId(&demoId, 1)) return;

    const sDemo_t *pDemo = findDemoById(demoId);
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        //printf("Returning %d numberOfFrames for demo %d\n", pDemo->size, demoId);
        stackPushInt((pDemo->lastKeyFrame == 0) ? 0 : 1);
    }
}

void Gsc_Demo_NumberOfFrames()
{
    int demoId = -1;
    if (!Base_Gsc_GetValidDemoId(&demoId, 1)) return;

    const sDemo_t *pDemo = findDemoById(demoId);
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        //printf("Returning %d numberOfFrames for demo %d\n", pDemo->size, demoId);
        stackPushInt(pDemo->size);
    }
}
void Gsc_Demo_NumberOfKeyFrames()
{
    int demoId = -1;
    if (!Base_Gsc_GetValidDemoId(&demoId, 1)) return;

    const sDemo_t *pDemo = findDemoById(demoId);
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
        printf("Returning %d numberOfKeyFrames for demo %d\n", nrOfKeyFrames, demoId);
        stackPushInt(nrOfKeyFrames);
    }
}

void Gsc_Demo_CreateDemo()
{
    int demoId = -1;
    if (!Base_Gsc_GetValidDemoId(&demoId, 1)) return;

    sDemo_t *pDemo = findDemoById(demoId);
    if (pDemo)
    {
        printf("Demo with id %d already loaded!\n", demoId);
        stackPushInt(-1);
        return;
    }

    pDemo = createDemo(demoId);
    if (!pDemo)
    {
        printf("No free demo slots available (wow)!\n");
        stackPushInt(-2);
        return;
    }

    stackPushInt(demoId);
    printf("Created demo with id %d\n", demoId);
}

void Gsc_Demo_DestroyDemo()
{
    int demoId = -1;
    if (!Base_Gsc_GetValidDemoId(&demoId, 1)) return;

    printf("Destroying demo with id %d\n", demoId);
    clearDemoById(demoId);
}

void Gsc_Demo_AddFrame()
{
    const int nrExpectedArgs = 9;
    if (Scr_GetNumParam() != nrExpectedArgs)
    {
        stackPushUndefined();
        stackError("AddFrame expects 9 arguments: demoId, origin, angles, isKeyFrame, flags, saveNow, loadNow, rpgNow, fps");
        return;
    }

    // Argument 1: demoId
    int demoId = -1;
    if (!Base_Gsc_GetValidDemoId(&demoId, nrExpectedArgs)) return;

    // Argument 2: origin
    if (stackGetParamType(1) != STACK_VECTOR)
    {
        stackPushUndefined();
        stackError("Argument 2 (origin) is not a vector");
        return;
    }
    vec3_t origin;
    stackGetParamVector(1, origin);

    // Argument 3: angles
    if (stackGetParamType(2) != STACK_VECTOR)
    {
        stackPushUndefined();
        stackError("Argument 3 (angles) is not a vector");
        return;
    }
    vec3_t angles;
    stackGetParamVector(2, angles);

    // Argument 4: isKeyFrame
    int keyFrame = -1;
    if (stackGetParamType(3) != STACK_INT)
    {
        stackPushUndefined();
        stackError("Argument 4 (keyFrame) is not an int");
        return;
    }
    stackGetParamInt(3, &keyFrame);

    if (keyFrame < 0)
    {
        stackPushUndefined();
        stackError("keyFrame < 0: %d", keyFrame);
    }
   
    int flags;
    if (stackGetParamType(4) != STACK_INT)
    {
        stackPushUndefined();
        stackError("Argument 5 (flags) is not an int");
        return;
    }
    stackGetParamInt(4, &flags);
   
    int saveNow;
    if (stackGetParamType(5) != STACK_INT)
    {
        stackPushUndefined();
        stackError("Argument 6 (saveNow) is not an int");
        return;
    }
    stackGetParamInt(5, &saveNow);
	int loadNow;
    if (stackGetParamType(6) != STACK_INT)
    {
        stackPushUndefined();
        stackError("Argument 7 (loadNow) is not an int");
        return;
    }
    stackGetParamInt(6, &loadNow);
    int rpgNow;
    if (stackGetParamType(7) != STACK_INT)
    {
        stackPushUndefined();
        stackError("Argument 8 (rpgNow) is not an int");
        return;
    }
    stackGetParamInt(7, &rpgNow);

     int fps;
    if (stackGetParamType(8) != STACK_INT)
    {
        stackPushUndefined();
        stackError("Argument 9 (fps) is not an int");
        return;
    }
    stackGetParamInt(8, &fps);

    sDemo_t *pDemo = findDemoById(demoId);
    if (!pDemo)
    {
        stackPushUndefined();
        printf("Demo with id %d was not found.. stop adding frames please\n", demoId);
        return;
    }

    bool isFirstFrame = !pDemo->isFirstFrameFilled;
    int idxLastExistingFrame = pDemo->currentFrame;
    int idxNewFrame = pDemo->currentFrame;
    if (!isFirstFrame) // First frame might not be filled in yet
    {
        idxNewFrame++;
    }

    sDemoFrame_t *pLastExistingFrame = &pDemo->pDemoFrames[idxLastExistingFrame];
    sDemoFrame_t *pNewFrame = &pDemo->pDemoFrames[idxNewFrame];

    // Fill in new frame
    memcpy(pNewFrame->origin, origin, sizeof(pNewFrame->origin));
    memcpy(pNewFrame->angles, angles, sizeof(pNewFrame->angles));

    // Set key frame info for new frame
    pNewFrame->isKeyFrame = (keyFrame > 0);
    pNewFrame->saveNow = (saveNow != 0);
    pNewFrame->loadNow = (loadNow != 0);
    pNewFrame->rpgNow = (rpgNow != 0);
    pNewFrame->flags = flags & 0xFFFF;
    pNewFrame->fps = fps & 0xFFFF;
    if (isFirstFrame)
    {
        pNewFrame->prevKeyFrame = idxLastExistingFrame;
    }
    else
    {
        if (pLastExistingFrame->isKeyFrame)
        {
            pNewFrame->prevKeyFrame = idxLastExistingFrame;
        }
        else
        {
            pNewFrame->prevKeyFrame = pLastExistingFrame->prevKeyFrame;
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

    // We added 1 frame, so demo size increases by 1
    pDemo->size++;
    pDemo->currentFrame++;
    pDemo->isFirstFrameFilled = true;

    // TODO: key frame branching (player loads)
    // TODO: re-allocate pDemoFrames if we're over halfway

    //printf("Added frame %d to demo of player %d\n", idxNewFrame, playerId);
    stackPushInt(demoId);
}

void Gsc_Demo_CompleteDemo()
{
    int demoId = -1;
    if (!Base_Gsc_GetValidDemoId(&demoId, 1)) return;

    sDemo_t *pDemo = findDemoById(demoId);
    if (!pDemo)
    {
        printf("Demo with id %d not found, can't complete..\n", demoId);
        return;
    }

    pDemo->isComplete = true;

    // TODO: keyframe branching?
}

//==========================================================================
// Functions related to playback & control                                        
//==========================================================================

void Gsc_Demo_SelectPlaybackDemo(int playerId)
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

    int demoId = -1;
    if (!Base_Gsc_GetValidDemoId(&demoId, 1)) return;

    printf("[%d] requesting playback demoId %d\n", playerId, demoId);

    // Clear the player's current playback state
    sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    pPlayback->selectedFrame = 0;
    pPlayback->pDemo = findDemoById(demoId);
    if (!pPlayback->pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        printf("[%d] requested playback demoId %d was found\n", playerId, demoId);
        stackPushInt(1);
    }
}

void Gsc_Demo_ReadFrame_Origin(int playerId)
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

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

void Gsc_Demo_ReadFrame_SaveNow(int playerId)
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

    const sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    const sDemo_t *pDemo = pPlayback->pDemo;
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        const sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame];
        stackPushInt(pDemoFrame->saveNow);
    }
}

void Gsc_Demo_ReadFrame_LoadNow(int playerId)
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

    const sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    const sDemo_t *pDemo = pPlayback->pDemo;
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        const sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame];
        stackPushInt(pDemoFrame->loadNow);
    }
}

void Gsc_Demo_ReadFrame_FPS(int playerId)
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

    const sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    const sDemo_t *pDemo = pPlayback->pDemo;
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        const sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame];
        stackPushInt(pDemoFrame->fps);
    }
}

void Gsc_Demo_ReadFrame_Flags(int playerId)
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

    const sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    const sDemo_t *pDemo = pPlayback->pDemo;
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        const sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame];
        stackPushInt(pDemoFrame->flags);
    }
}

void Gsc_Demo_ReadFrame_RPGNow(int playerId)
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

    const sDemoPlayback_t *pPlayback = &opencj_playback[playerId];
    const sDemo_t *pDemo = pPlayback->pDemo;
    if (!pDemo)
    {
        stackPushUndefined();
    }
    else
    {
        const sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame];
        stackPushInt(pDemoFrame->rpgNow);
    }
}

void Gsc_Demo_ReadFrame_Angles(int playerId)
{
    if (Base_Gsc_IsValidClientNum(playerId)) return;

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
void Gsc_Demo_SkipFrame(int playerId)
{
    if (Scr_GetNumParam() != 1)
    {
        stackError("Expected 1 argument: nrFramesToSkip");
        return;
    }

    int nrFramesToSkip = 0;
    if (stackGetParamType(0) != STACK_INT)
    {
        stackError("Argument 1 (nrFramesToSkip) is not an int");
        return;
    }
    stackGetParamInt(0, &nrFramesToSkip);

    Base_Gsc_Demo_FrameSkip(playerId, nrFramesToSkip, false);
}
void Gsc_Demo_SkipKeyFrame(int playerId)
{
    if (Scr_GetNumParam() != 1)
    {
        stackError("Expected 1 argument: nrKeyFramesToSkip");
        return;
    }

    int nrFramesToSkip = 0;
    if (stackGetParamType(0) != STACK_INT)
    {
        stackError("Argument 1 (nrFramesToSkip) is not an int");
        return;
    }
    stackGetParamInt(0, &nrFramesToSkip);

    Base_Gsc_Demo_FrameSkip(playerId, nrFramesToSkip, true);
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
