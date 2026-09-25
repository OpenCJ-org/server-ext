///
/// Player demos, shared between libcod and cod4x
///

/**************************************************************************
 * Includes                                                               *
 **************************************************************************/

#include "shared.hpp"

#include <cstring>
#include <map>
#include <new>
#include <cmath>
#include <algorithm>
#include <string>
#ifdef COD4
extern "C" {
#include "../cod4x-server/src/filesystem.h"
#include "../cod4x-server/src/bg.h"
}
#endif
#ifdef COD4
#include "../cod4x-server/src/zlib/zlib.h"
#else
#include <zlib.h>
#endif

/**************************************************************************
 * Defines                                                                *
 **************************************************************************/

// Max number of demos per map that are available for playback
#define MAX_NR_DEMOS_PER_MAP            128

// Magic number to know if a demo is initialized / used for validity checking
#define DEMO_MAGIC_NUMBER               (uint32_t)0x7fd126ea

/**************************************************************************
 * Types                                                                  *
 **************************************************************************/

struct DemoVisual
{
    bool valid;
    uint16_t weapon, weaponTime, weaponDelay, animation;
    uint8_t weaponState, bobCycle;
    float viewHeight, ads;
    int16_t velocity[3], ladder[3];
    uint16_t sprintElapsed, mantleTimer, mantleYaw;
    uint8_t mantleTransition, mantleFlags;
    uint8_t landingEvent, landingImpact;
};

#ifdef COD4
static playerState_t *demoReturnVisual[MAX_CLIENTS];
static int demoPresentedWeapon[MAX_CLIENTS];
static int demoPresentedFrame[MAX_CLIENTS];
static qboolean demoReturnFrozen[MAX_CLIENTS];
static_assert(__builtin_offsetof(gclient_t, bFrozen)==0x3080, "CoD4 freeze-controls layout mismatch");
static const char *demoWeaponName(unsigned weapon)
{
    if(!weapon)return "none";
    const WeaponDef *def=BG_GetWeaponDef(weapon);
    return def ? def->szInternalName : "none";
}
static std::map<unsigned,std::string> demoFireSounds;
static const char *demoFireSound(unsigned weapon)
{
    auto found=demoFireSounds.find(weapon);
    if(found!=demoFireSounds.end())return found->second.c_str();
    // Dedicated-server weapon defs omit sound assets; use the same weapon file
    // that supplied the gameplay definition. Cache once per weapon/map.
    std::string alias;
    std::string path="weapons/mp/";path+=demoWeaponName(weapon);
    void *buffer=nullptr;int length=FS_ReadFile(path.c_str(),&buffer);
    if(length>0 && buffer)
    {
        std::string data(static_cast<char *>(buffer),length);
        const std::string key="\\fireSoundPlayer\\";
        size_t begin=data.find(key);
        if(begin!=std::string::npos)
        {
            begin+=key.size();size_t end=data.find('\\',begin);
            if(end!=std::string::npos && end-begin<128)alias=data.substr(begin,end-begin);
        }
    }
    if(buffer)FS_FreeFile(buffer);
    return demoFireSounds.emplace(weapon,alias).first->second.c_str();
}
static uint16_t demoU16(int value) { return std::max(0, std::min(65535, value)); }
static DemoVisual captureDemoVisual(const playerState_t &ps, bool rpg, int previousEventSequence)
{
    DemoVisual v = {};
    v.valid=true;v.weapon=ps.weapon;v.weaponTime=demoU16(ps.weaponTime);
    v.weaponDelay=demoU16(ps.weaponDelay);v.animation=ps.weapAnim;
    v.weaponState=ps.weaponstate;v.bobCycle=ps.bobCycle;
    v.viewHeight=ps.viewHeightCurrent;v.ads=ps.fWeaponPosFrac;
    for(int i=0;i<3;++i)
    {
        v.velocity[i]=std::max(-32767,std::min(32767,int(ps.velocity[i]*4)));
        v.ladder[i]=std::max(-32767,std::min(32767,int(ps.vLadderVec[i]*32767)));
    }
    v.sprintElapsed=demoU16(ps.commandTime-ps.sprintState.lastSprintStart);
    v.mantleTimer=demoU16(ps.mantleState.timer);
    v.mantleYaw=uint16_t(int(ps.mantleState.yaw*65536.0f/360.0f));
    v.mantleTransition=ps.mantleState.transIndex;v.mantleFlags=ps.mantleState.flags;
    // Only new engine events since the preceding recorded frame. The ring and
    // sequence wrap at 4 and 256 respectively. Never replay fall damage.
    int count=std::min(4,(ps.eventSequence-previousEventSequence)&255);
    for(int i=count;i>0;--i)
    {
        int slot=(ps.eventSequence-i)&3,event=ps.events[slot];
        int impact=ps.eventParms[slot];
        if(event>=EV_LANDING_PAIN_DEFAULT && event<=EV_LANDING_PAIN_PAINTEDMETAL)
        {
            float minimum=Cvar_VariableValue("bg_fallDamageMinHeight");
            float height=minimum+(Cvar_VariableValue("bg_fallDamageMaxHeight")-minimum)*impact*0.01f;
            impact=height>12 ? std::min(24,int(4+(height-12)*4/26)) : 0;
            event-=EV_LANDING_PAIN_DEFAULT-EV_LANDING_DEFAULT;
        }
        if(event>=EV_LANDING_DEFAULT && event<=EV_LANDING_PAINTEDMETAL && impact>0)
        {v.landingEvent=event;v.landingImpact=std::min(24,impact);}
    }
    // WEAPON_FIRING is 5. Do not retain ordinary gun/pistol firing animations.
    if(!rpg && ps.weaponstate==5)
    {v.weaponState=0;v.weaponTime=0;v.weaponDelay=0;v.animation=0;}
    return v;
}
#endif

typedef struct
{
    DemoVisual visual;
    float origin[3];    // Origin of the player at this frame
    float angles[3];    // Angles of the player at this frame
    short flags;		// flags that store button/stance/weapon stuff
    int checkpointId;
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
    int captureEventSequence, captureTime, captureTeleportBit;
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

        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            if (opencj_playback[i].pDemo == pDemo)
                memset(&opencj_playback[i], 0, sizeof(opencj_playback[i]));
        }
        memset(pDemo, 0, sizeof(*pDemo));
    }
}

static void clearAllDemos()
{
    printf("Clearing all demos\n");
#ifdef COD4
    for(int i=0;i<MAX_CLIENTS;++i){delete demoReturnVisual[i];demoReturnVisual[i]=nullptr;}
    demoFireSounds.clear();
#endif
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

        pDemo->nrAllocatedFrames = 1024;
        pDemo->pDemoFrames = new (std::nothrow) sDemoFrame_t[pDemo->nrAllocatedFrames]();
        if (!pDemo->pDemoFrames)
        {
            clearDemoById(demoId);
            return NULL;
        }
    }

    return pDemo;
}

// Bound process-wide playback/recording memory on the 32-bit server.
static const size_t demoMemoryLimit = 128 * 1024 * 1024;
static bool appendDemoFrame(sDemo_t *demo, const sDemoFrame_t &frame)
{
    if (demo->isComplete) return false;
    if (demo->size == demo->nrAllocatedFrames)
    {
        const int capacity = demo->nrAllocatedFrames * 2;
        size_t allocated = 0;
        for (int i = 0; i < MAX_NR_DEMOS_PER_MAP; ++i)
            allocated += opencj_demos[i].nrAllocatedFrames * sizeof(sDemoFrame_t);
        if (allocated + capacity * sizeof(sDemoFrame_t) > demoMemoryLimit) return false;
        sDemoFrame_t *frames = new (std::nothrow) sDemoFrame_t[capacity]();
        if (!frames) return false;
        memcpy(frames, demo->pDemoFrames, demo->size * sizeof(*frames));
        delete[] demo->pDemoFrames;
        demo->pDemoFrames = frames;
        demo->nrAllocatedFrames = capacity;
    }
    const int index = demo->size;
    sDemoFrame_t &next = demo->pDemoFrames[index];
    next = frame;
    next.nextKeyFrame = index;
    next.prevKeyFrame = index ? demo->lastKeyFrame : 0;
    if (next.isKeyFrame)
    {
        for (int i = demo->lastKeyFrame; i < index; ++i)
            demo->pDemoFrames[i].nextKeyFrame = index;
        demo->lastKeyFrame = index;
    }
    demo->size++;
    demo->currentFrame = index;
    demo->isFirstFrameFilled = true;
    return true;
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
    const int nrExpectedArgs = Scr_GetNumParam();
    if (nrExpectedArgs != 9 && nrExpectedArgs != 10 && nrExpectedArgs != 11)
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

    sDemoFrame_t frame = {};
    memcpy(frame.origin, origin, sizeof(origin));
    memcpy(frame.angles, angles, sizeof(angles));
    frame.isKeyFrame = keyFrame != 0;
    frame.flags = flags;
    frame.fps = fps;
    frame.saveNow = saveNow != 0;
    frame.loadNow = loadNow != 0;
    frame.rpgNow = rpgNow != 0;
    if (nrExpectedArgs >= 10) stackGetParamInt(9, &frame.checkpointId);
#ifdef COD4
    if(nrExpectedArgs==11)
    {
        int client;stackGetParamInt(10,&client);
        if(client<0 || client>=MAX_CLIENTS || !g_entities[client].client)
        {stackPushUndefined();return;}
        const playerState_t &ps=*SV_GameClientNum(client);
        bool continuous=pDemo->size && ps.commandTime>=pDemo->captureTime && ps.commandTime-pDemo->captureTime<=100;
        if(continuous && (ps.eFlags&2)!=pDemo->captureTeleportBit)frame.loadNow=true;
        frame.visual=captureDemoVisual(ps,(flags&4096)!=0,
            continuous && !frame.loadNow ? pDemo->captureEventSequence : ps.eventSequence);
        pDemo->captureEventSequence=ps.eventSequence;pDemo->captureTime=ps.commandTime;
        pDemo->captureTeleportBit=ps.eFlags&2;
    }
#endif
    if (appendDemoFrame(pDemo, frame)) stackPushInt(demoId);
    else stackPushUndefined();
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
        sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame]; // CoD2 stock Scr_AddVector doesn't like const here
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
        sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame];
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
        sDemoFrame_t *pDemoFrame = &pDemo->pDemoFrames[pPlayback->selectedFrame]; // CoD2 stock Scr_AddVector doesn't like const here
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


// OCJ1: 27 bytes/frame. OCJ2: optional 31-byte presentation and weapon names
// only on changes (plus a chunk-start anchor). Both formats use zlib.
// Hex is only the GSC/SQL transport; MySQL stores UNHEX(payload) as binary.
static void putDemoInt(unsigned char *p, uint32_t n, int bytes)
{
    for (int i=0; i<bytes; ++i) p[i] = (n >> (8*i)) & 255;
}
static uint32_t getDemoInt(const unsigned char *p, int bytes)
{
    uint32_t n=0;
    for (int i=0; i<bytes; ++i) n |= uint32_t(p[i]) << (8*i);
    return n;
}
static const int demoChunkFrames = 64;
static const int demoWireFrameSize = 27;
static const int demoVisualWireSize = 31;
static const int demoChunkBufferSize = 8192;

void Gsc_Demo_EncodeChunk()
{
    int id, first, count;
    if (!Base_Gsc_GetValidDemoId(&id, 3)) return;
    stackGetParamInt(1, &first); stackGetParamInt(2, &count);
    sDemo_t *demo=findDemoById(id);
    if (!demo || first<0 || count<1 || count>demoChunkFrames || first>demo->size-count)
    { stackPushUndefined(); return; }
    unsigned char raw[demoChunkBufferSize] = {'O','C','J','3'};
    size_t used=8;uint16_t previousWeapon=0;bool haveWeapon=false;
    putDemoInt(raw+4,count,4);
    for (int i=0; i<count; ++i)
    {
        const sDemoFrame_t &frame=demo->pDemoFrames[first+i];
        unsigned char *p=raw+used;used+=demoWireFrameSize;
        for (int axis=0; axis<3; ++axis)
        {
            uint32_t bits; memcpy(&bits,&frame.origin[axis],4);
            putDemoInt(p+axis*4,bits,4);
            putDemoInt(p+12+axis*2,uint16_t(int(frame.angles[axis]*65536.0f/360.0f)),2);
        }
        putDemoInt(p+18,uint16_t(frame.flags),2);
        putDemoInt(p+20,uint16_t(frame.fps),2);
        putDemoInt(p+22,frame.checkpointId,4);
        p[26]=(frame.saveNow?1:0)|(frame.loadNow?2:0)|(frame.rpgNow?4:0);
#ifdef COD4
        if(frame.visual.valid)
        {
            p[26]|=8;
            const DemoVisual &v=frame.visual;unsigned char *q=raw+used;used+=demoVisualWireSize;
            putDemoInt(q,v.weaponTime,2);putDemoInt(q+2,v.weaponDelay,2);putDemoInt(q+4,v.animation,2);
            q[6]=v.weaponState;q[7]=v.bobCycle;putDemoInt(q+8,int(v.viewHeight*256),2);
            q[10]=std::max(0,std::min(255,int(std::lround(v.ads*255))));
            for(int a=0;a<3;++a){putDemoInt(q+11+a*2,uint16_t(v.velocity[a]),2);putDemoInt(q+17+a*2,uint16_t(v.ladder[a]),2);}
            putDemoInt(q+23,v.sprintElapsed,2);putDemoInt(q+25,v.mantleTimer,2);putDemoInt(q+27,v.mantleYaw,2);
            q[29]=v.mantleTransition;q[30]=v.mantleFlags;
            const char *name=(!haveWeapon||previousWeapon!=v.weapon)?demoWeaponName(v.weapon):"";
            size_t length=strlen(name);
            if(length>63 || used+1+length>sizeof(raw)){stackPushUndefined();return;}
            raw[used++]=length;memcpy(raw+used,name,length);used+=length;
            previousWeapon=v.weapon;haveWeapon=true;
            if(v.landingEvent)
            {p[26]|=16;raw[used++]=v.landingEvent;raw[used++]=v.landingImpact;}
        }
#endif
    }
    unsigned char packed[demoChunkBufferSize]; uLongf size=sizeof(packed);
    if (compress2(packed,&size,raw,used,6)!=Z_OK)
    { stackPushUndefined(); return; }
    const char *digits="0123456789abcdef";
    char hex[demoChunkBufferSize*2+1];
    for (uLongf i=0;i<size;++i) {hex[2*i]=digits[packed[i]>>4];hex[2*i+1]=digits[packed[i]&15];}
    hex[2*size]=0; stackPushString(hex);
}
static int demoHexDigit(char c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
void Gsc_Demo_DecodeChunk()
{
    int id; const char *hex;
    if (!Base_Gsc_GetValidDemoId(&id,2)) return;
    stackGetParamString(1,&hex);
    sDemo_t *demo=findDemoById(id);
    size_t length=strlen(hex);
    if (!demo || demo->isComplete || !length || length>demoChunkBufferSize*2 || length%2)
    { stackPushUndefined(); return; }
    unsigned char packed[demoChunkBufferSize],raw[demoChunkBufferSize];
    for(size_t i=0;i<length;i+=2)
    {
        int a=demoHexDigit(hex[i]),b=demoHexDigit(hex[i+1]);
        if(a<0||b<0){stackPushUndefined();return;}
        packed[i/2]=(a<<4)|b;
    }
    uLongf size=sizeof(raw);
    if(uncompress(raw,&size,packed,length/2)!=Z_OK || size<8 || (memcmp(raw,"OCJ1",4) && memcmp(raw,"OCJ2",4) && memcmp(raw,"OCJ3",4)))
    {stackPushUndefined();return;}
    uint32_t count=getDemoInt(raw+4,4);
    bool modern=raw[3]!='1';size_t used=8;uint16_t weapon=0;bool haveWeapon=false;
    if(!count || count>demoChunkFrames || (!modern && size!=8+count*demoWireFrameSize))
    {stackPushUndefined();return;}
    sDemoFrame_t frames[demoChunkFrames] = {};
    for(uint32_t i=0;i<count;++i)
    {
        if(used+demoWireFrameSize>size){stackPushUndefined();return;}
        unsigned char *p=raw+used;used+=demoWireFrameSize;
        sDemoFrame_t &frame=frames[i];
        for(int axis=0;axis<3;++axis)
        {
            uint32_t bits=getDemoInt(p+axis*4,4);memcpy(&frame.origin[axis],&bits,4);
            if(!std::isfinite(frame.origin[axis]) || fabs(frame.origin[axis])>131072)
            {stackPushUndefined();return;}
            frame.angles[axis]=getDemoInt(p+12+axis*2,2)*360.0f/65536.0f;
        }
        frame.flags=getDemoInt(p+18,2);frame.fps=getDemoInt(p+20,2);
        frame.checkpointId=getDemoInt(p+22,4);
        frame.saveNow=p[26]&1;frame.loadNow=p[26]&2;frame.rpgNow=p[26]&4;
        frame.isKeyFrame=true;
        if(modern && (p[26]&8))
        {
            if(used+demoVisualWireSize+1>size){stackPushUndefined();return;}
            unsigned char *q=raw+used;used+=demoVisualWireSize;
            DemoVisual &v=frame.visual;v.valid=true;
            v.weaponTime=getDemoInt(q,2);v.weaponDelay=getDemoInt(q+2,2);v.animation=getDemoInt(q+4,2);
            v.weaponState=q[6];v.bobCycle=q[7];v.viewHeight=int16_t(getDemoInt(q+8,2))/256.0f;v.ads=q[10]/255.0f;
            for(int a=0;a<3;++a){v.velocity[a]=getDemoInt(q+11+a*2,2);v.ladder[a]=getDemoInt(q+17+a*2,2);}
            v.sprintElapsed=getDemoInt(q+23,2);v.mantleTimer=getDemoInt(q+25,2);v.mantleYaw=getDemoInt(q+27,2);
            v.mantleTransition=q[29];v.mantleFlags=q[30];
            unsigned length=raw[used++];
            if(length>63 || used+length>size || (!length&&!haveWeapon) || v.viewHeight<0 || v.viewHeight>100 || v.weaponState>32)
            {stackPushUndefined();return;}
#ifdef COD4
            if(length)
            {
                char name[64];memcpy(name,raw+used,length);name[length]=0;
                if(strlen(name)!=length){stackPushUndefined();return;}
                weapon=!strcmp(name,"none")?0:G_GetWeaponIndexForName(name);haveWeapon=true;
            }
#endif
            used+=length;v.weapon=weapon;
            if(raw[3]=='3' && (p[26]&16))
            {
                if(used+2>size){stackPushUndefined();return;}
                v.landingEvent=raw[used++];v.landingImpact=raw[used++];
                if(v.landingEvent<77 || v.landingEvent>105 || !v.landingImpact || v.landingImpact>24)
                {stackPushUndefined();return;}
            }
        }
    }
    if(used!=size){stackPushUndefined();return;}
    for(uint32_t i=0;i<count;++i)
        if(!appendDemoFrame(demo,frames[i])) {stackPushUndefined();return;}
    stackPushInt(count);
}
void Gsc_Demo_Truncate()
{
    int id,count;
    if(!Base_Gsc_GetValidDemoId(&id,2))return;
    stackGetParamInt(1,&count);
    sDemo_t *demo=findDemoById(id);
    if(!demo || demo->isComplete || count<0 || count>demo->size)
    {stackPushUndefined();return;}
    demo->size=count;demo->currentFrame=count?count-1:0;
    demo->isFirstFrameFilled=count!=0;demo->lastKeyFrame=0;
    for(int i=0;i<count;++i)
    {
        sDemoFrame_t &f=demo->pDemoFrames[i];
        f.isKeyFrame=true;f.prevKeyFrame=i?i-1:0;f.nextKeyFrame=i+1<count?i+1:i;
        demo->lastKeyFrame=i;
    }
    stackPushInt(count);
}
void Gsc_Demo_FindSegment()
{
    int id,cp,any;vec3_t origin;
    if(!Base_Gsc_GetValidDemoId(&id,4))return;
    stackGetParamInt(1,&cp);stackGetParamVector(2,origin);stackGetParamInt(3,&any);
    sDemo_t *demo=findDemoById(id);
    if(!demo || demo->size<2){stackPushUndefined();return;}
    int first=-1,last=-1;
    float nearest=256.0f*256.0f;
    if(!any)
    {
        for(int i=0;i<demo->size;++i)
        {
            if(demo->pDemoFrames[i].checkpointId==cp)
            {
                if(first<0)first=i;
                last=i;
            }
            else if(first>=0){last=i;break;}
        }
        // Alternative platforms are only suitable if their setup is nearby.
        if(first>=0)
        {
            float d=0;for(int a=0;a<3;++a){float x=demo->pDemoFrames[first].origin[a]-origin[a];d+=x*x;}
            if(d>nearest)first=-1;
        }
    }
    else
    {
        int begin=0,grounded=0;bool airborne=false;
        float distance=nearest;
        for(int i=0;i<demo->size;++i)
        {
            const sDemoFrame_t &f=demo->pDemoFrames[i];
            float d=0;for(int a=0;a<3;++a){float x=f.origin[a]-origin[a];d+=x*x;}
            if(d<distance)distance=d;
            if(f.flags&8192)++grounded;else {grounded=0;airborne=true;}
            if((airborne&&grounded>=10)||i==demo->size-1)
            {
                if(distance<nearest){first=begin;last=i;nearest=distance;}
                begin=i;grounded=0;airborne=false;distance=256.0f*256.0f;
            }
        }
        // No nearby passage: play the whole route.
        if(first<0){first=0;last=demo->size-1;}
    }
    if(first<0 || last<=first){stackPushUndefined();return;}
    stackMakeArray();stackPushInt(first);stackPushArrayNext();stackPushInt(last);stackPushArrayNext();
}

// Presentation only: never replay input through weapon simulation or spawn missiles.
void Gsc_Demo_BeginPresentation(int client)
{
#ifdef COD4
    if(Base_Gsc_IsValidClientNum(client))return;
    delete demoReturnVisual[client];
    demoReturnVisual[client]=new(std::nothrow) playerState_t(*SV_GameClientNum(client));
    demoPresentedWeapon[client]=-1;
    demoPresentedFrame[client]=-1;
    demoReturnFrozen[client]=g_entities[client].client->bFrozen;
    stackPushBool(demoReturnVisual[client]!=nullptr);
#else
    stackPushBool(false);
#endif
}

void Gsc_Demo_ApplyPresentation(int client)
{
#ifdef COD4
    if(Base_Gsc_IsValidClientNum(client))return;
    sDemoPlayback_t &play=opencj_playback[client];
    if(!demoReturnVisual[client] || !play.pDemo){stackPushBool(false);return;}
    const sDemoFrame_t &frame=play.pDemo->pDemoFrames[play.selectedFrame];
    const DemoVisual &v=frame.visual;
    if(!v.valid){stackPushBool(false);return;}
    playerState_t &ps=*SV_GameClientNum(client);
    if(v.weapon>=128){stackPushBool(false);return;}
    if(demoPresentedFrame[client]!=play.selectedFrame)
    {
        bool cut=frame.loadNow;
        int previous=demoPresentedFrame[client];
        if(previous>=0 && previous<play.pDemo->size)
        {
            const sDemoFrame_t &before=play.pDemo->pDemoFrames[previous];
            float distance=0,speed=0;
            for(int i=0;i<3;++i)
            {
                float delta=frame.origin[i]-before.origin[i];distance+=delta*delta;
                float velocity=v.velocity[i]/4.0f;speed+=velocity*velocity;
            }
            // Older recordings lack teleport markers. Do not blend map portals
            // across the map; allow for genuine high-speed movement.
            cut |= distance>std::max(256.0f*256.0f,speed*0.01f);
        }
        if(cut)ps.eFlags ^= 2; // EF_TELEPORT_BIT: discard interpolation across a cut.
        demoPresentedFrame[client]=play.selectedFrame;
    }
    // Match the existing seamless-switch protocol so client prediction does not
    // keep re-equipping the viewer's own weapon. Restore inventory on exit.
    if(v.weapon)ps.weapons[v.weapon/32] |= 1u << (v.weapon%32);
    if(demoPresentedWeapon[client]!=v.weapon)
    {
        SV_GameSendServerCommand(client,1,va("a %u",unsigned(v.weapon)));
        demoPresentedWeapon[client]=v.weapon;
    }
    // disableWeapons makes PM_Weapon lower the weapon to "none". Freeze
    // live input instead, and drive the animation directly from the recording.
    // Never feed recorded fire delays/states back into weapon simulation:
    // a delayed RPG shot could otherwise spawn a real missile.
    g_entities[client].client->bFrozen=qtrue;
    ps.pm_flags |= 0x800;
    // Use the client's spectator interpolation path. Local prediction would
    // zero velocity for frozen controls, suppressing recorded weapon bob/sway.
    ps.otherFlags |= 2;
    ps.weapFlags &= ~0x80;
    ps.weapon=v.weapon;ps.weaponTime=1000;ps.weaponDelay=0;
    ps.weapAnim=v.animation;ps.weaponstate=0;ps.bobCycle=v.bobCycle;
    ps.viewHeightCurrent=v.viewHeight;ps.viewHeightTarget=int(v.viewHeight);ps.viewHeightLerpTime=0;
    ps.fWeaponPosFrac=v.ads;
    if(!(frame.flags&8192) && ps.groundEntityNum!=ENTITYNUM_NONE)
    {ps.jumpTime=ps.commandTime;ps.jumpOriginZ=frame.origin[2];}
    ps.groundEntityNum=(frame.flags&8192)?ENTITYNUM_WORLD:ENTITYNUM_NONE;
    ps.pm_flags=(ps.pm_flags&~12)|((frame.flags&128)?4:0)|((frame.flags&256)?8:0);
    for(int i=0;i<3;++i){ps.velocity[i]=v.velocity[i]/4.0f;ps.vLadderVec[i]=v.ladder[i]/32767.0f;}
    ps.sprintState.lastSprintStart=ps.commandTime-v.sprintElapsed;
    ps.sprintState.lastSprintEnd=(frame.flags&64)?ps.sprintState.lastSprintStart-1:ps.commandTime;
    ps.mantleState.timer=v.mantleTimer;ps.mantleState.yaw=v.mantleYaw*360.0f/65536;
    ps.mantleState.transIndex=v.mantleTransition;ps.mantleState.flags=v.mantleFlags;
    stackPushBool(true);
#else
    stackPushBool(false);
#endif
}

// Find checkpoint boundaries in the retained recording, not the map's route graph.
void Gsc_Demo_SeekCheckpoint(int client)
{
    if(Base_Gsc_IsValidClientNum(client))return;
    int direction;
    if(!stackGetParamInt(0,&direction) || (direction!=-1 && direction!=1))
    {stackPushUndefined();return;}
    const sDemoPlayback_t &play=opencj_playback[client];
    if(!play.pDemo){stackPushUndefined();return;}
    int current=play.selectedFrame,target=current;
    const sDemoFrame_t *frames=play.pDemo->pDemoFrames;
    if(direction>0)
    {
        while(target+1<play.pDemo->size && frames[target+1].checkpointId==frames[current].checkpointId)++target;
        if(target+1<play.pDemo->size)++target;
    }
    else
    {
        while(target>0 && frames[target-1].checkpointId==frames[current].checkpointId)--target;
        if(target>0)
        {
            --target;
            while(target>0 && frames[target-1].checkpointId==frames[target].checkpointId)--target;
        }
    }
    stackPushInt(target);
}

void Gsc_Demo_PlayLanding(int client)
{
#ifdef COD4
    if(Base_Gsc_IsValidClientNum(client))return;
    const sDemoPlayback_t &play=opencj_playback[client];
    if(demoReturnVisual[client] && play.pDemo)
    {
        const sDemoFrame_t &frame=play.pDemo->pDemoFrames[play.selectedFrame];
        if(frame.visual.landingEvent && !frame.loadNow)
        {
            // A normal landing event gives the owner the original camera dip;
            // pain events were converted during capture and cannot deal damage.
            BG_AddPredictableEventToPlayerstate(BGEvent(frame.visual.landingEvent),
                frame.visual.landingImpact,g_entities[client].client);
            stackPushBool(true);return;
        }
    }
#endif
    stackPushBool(false);
}

void Gsc_Demo_EndPresentation(int client)
{
#ifdef COD4
    if(Base_Gsc_IsValidClientNum(client))return;
    playerState_t *saved=demoReturnVisual[client];if(!saved)return;
    playerState_t &ps=*SV_GameClientNum(client);
    g_entities[client].client->bFrozen=demoReturnFrozen[client];
    ps.otherFlags=(ps.otherFlags&~2)|(saved->otherFlags&2);
    ps.pm_flags=(ps.pm_flags&~0x800)|(saved->pm_flags&0x800);
    ps.weapFlags=(ps.weapFlags&~0x80)|(saved->weapFlags&0x80);
    memcpy(ps.weapons,saved->weapons,sizeof(ps.weapons));
    memcpy(ps.weaponold,saved->weaponold,sizeof(ps.weaponold));
    memcpy(ps.weaponrechamber,saved->weaponrechamber,sizeof(ps.weaponrechamber));
    memcpy(ps.ammo,saved->ammo,sizeof(ps.ammo));memcpy(ps.ammoclip,saved->ammoclip,sizeof(ps.ammoclip));
    ps.weapon=saved->weapon;ps.weaponTime=saved->weaponTime;ps.weaponDelay=saved->weaponDelay;
    SV_GameSendServerCommand(client,1,va("a %u",saved->weapon));
    ps.weapAnim=saved->weapAnim;ps.weaponstate=saved->weaponstate;ps.bobCycle=saved->bobCycle;
    ps.viewHeightCurrent=saved->viewHeightCurrent;ps.viewHeightTarget=saved->viewHeightTarget;
    ps.viewHeightLerpTime=saved->viewHeightLerpTime;ps.viewHeightLerpTarget=saved->viewHeightLerpTarget;ps.viewHeightLerpDown=saved->viewHeightLerpDown;
    ps.fWeaponPosFrac=saved->fWeaponPosFrac;ps.pm_flags=(ps.pm_flags&~12)|(saved->pm_flags&12);
    memcpy(ps.velocity,saved->velocity,sizeof(ps.velocity));memcpy(ps.vLadderVec,saved->vLadderVec,sizeof(ps.vLadderVec));
    int elapsed=ps.commandTime-saved->commandTime;
    ps.groundEntityNum=saved->groundEntityNum;ps.jumpTime=saved->jumpTime+elapsed;ps.jumpOriginZ=saved->jumpOriginZ;
    ps.sprintState=saved->sprintState;ps.sprintState.lastSprintStart+=elapsed;ps.sprintState.lastSprintEnd+=elapsed;
    ps.mantleState=saved->mantleState;
    delete saved;demoReturnVisual[client]=nullptr;
#endif
}

void Gsc_Demo_ReadFrame_Weapon(int client)
{
#ifdef COD4
    if(Base_Gsc_IsValidClientNum(client))return;
    const sDemoPlayback_t &play=opencj_playback[client];
    if(play.pDemo){const DemoVisual &v=play.pDemo->pDemoFrames[play.selectedFrame].visual;if(v.valid){stackPushString(demoWeaponName(v.weapon));return;}}
#endif
    stackPushUndefined();
}

void Gsc_Demo_ReadFrame_RPGSound(int client)
{
#ifdef COD4
    if(Base_Gsc_IsValidClientNum(client))return;
    const sDemoPlayback_t &play=opencj_playback[client];
    if(play.pDemo)
    {
        const sDemoFrame_t &f=play.pDemo->pDemoFrames[play.selectedFrame];
        if(f.visual.valid && f.rpgNow && (f.flags&4096))
        {
            const char *sound=demoFireSound(f.visual.weapon);
            if(*sound){stackPushString(sound);return;}
        }
    }
#endif
    stackPushUndefined();
}
