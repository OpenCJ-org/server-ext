#include <stdlib.h>

#include "gsc_saveposition.hpp"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef enum // GSC hardcodes these values!
{
    FPS_MODE_43 = 0, // CoD2 only
    FPS_MODE_76 = 1, // CoD2 only
    FPS_MODE_125 = 2,
    FPS_MODE_250 = 3, // CoD2 only
    FPS_MODE_333 = 4, // CoD2 only
    FPS_MODE_MIX = 5,
    FPS_MODE_HAX = 6, // CoD4 only
} opencj_fps_mode;

struct opencj_save
{
	opencj_save *prevsave;
	gentity_t *groundentity;
	vec3_t origin;
	vec3_t angles;
    int checkPointId;
    int explosiveJumps;
    int doubleExplosives;
    opencj_fps_mode FPSMode;
    int flags;
    int saveNum;
};

static opencj_save *playersaves[MAX_CLIENTS];
static opencj_save *playersaves_selected[MAX_CLIENTS];

void gsc_saveposition_initclient(int id)
{
	opencj_save *prev = playersaves[id];
	while(prev != NULL)
	{
		opencj_save *tmp = prev->prevsave;
		free(prev);
		prev = tmp;
	}
	playersaves[id] = NULL;
	playersaves_selected[id] = NULL;
}

void gsc_saveposition_save(int id) //player savePosition_save(origin, angles, entity)
{
	opencj_save *newsave = (opencj_save*)malloc(sizeof(opencj_save));
	if(newsave == NULL)
	{
		stackPushUndefined();
		return;
	}
    
	stackGetParamVector(0, newsave->origin);
	stackGetParamVector(1, newsave->angles);
	if(stackGetParamType(2) == STACK_INT)
	{
		int entnum;
		stackGetParamInt(2, &entnum);
		newsave->groundentity = &g_entities[entnum];
	}
	else
    {
		newsave->groundentity = NULL;
    }
    stackGetParamInt(3, &newsave->explosiveJumps);
    stackGetParamInt(4, &newsave->doubleExplosives);
    if(stackGetParamType(5) == STACK_INT)
    {
        stackGetParamInt(5, &newsave->checkPointId);
    }
    else
    {
        newsave->checkPointId = -1;
    }

    int FPSMode;
    stackGetParamInt(6, &FPSMode);
    newsave->FPSMode = (opencj_fps_mode)FPSMode;

    stackGetParamInt(7, &newsave->flags);
    stackGetParamInt(8, &newsave->saveNum);
	newsave->prevsave = playersaves[id];
	playersaves[id] = newsave;
    
	stackPushInt(0);
}

void gsc_saveposition_selectwithoutflag(int id) // player gsc_saveposition_selectwithoutflag(flags, [otherEntityNum])
{
    int flags = 0;
    stackGetParamInt(0, &flags);
    if (flags == 0)
    {
        stackPushUndefined();
        return;
    }

    // Player might be requesting it for a different player's saves (teleporting for example)
    int otherPlayerId = id; // By default, the requesting player
    if (Scr_GetNumParam() > 1)
    {
        stackGetParamInt(1, &otherPlayerId);
    }

    // Select the last save that matches this criteria
    playersaves_selected[id] = playersaves[otherPlayerId];
    int backwardsCount = 0;
    while (playersaves_selected[id] != NULL)
    {
        // Check if this save matches the criteria of not having specific flags
        int saveFlags = playersaves_selected[id]->flags;
        if ((saveFlags & flags) != flags)
        {
            stackPushInt(backwardsCount); // Successfully found
            return;
        }

        // Check if there are any saves left
        if (playersaves_selected[id]->prevsave == NULL)
        {
            stackPushUndefined();
            return;
        }

        // Go back further
        playersaves_selected[id] = playersaves_selected[id]->prevsave;
        backwardsCount++;
    }
}

void gsc_saveposition_selectsave(int id) //player savePosition_selectSave(backwardsCount)
{
	int backwardscount;
	stackGetParamInt(0, &backwardscount);
	if(playersaves[id] == NULL)
	{
		stackPushInt(1);
		return;
	}
	playersaves_selected[id] = playersaves[id];

	while(backwardscount)
	{
		if(playersaves_selected[id]->prevsave != NULL)
		{
			playersaves_selected[id] = playersaves_selected[id]->prevsave;
		}
		else
		{
			stackPushInt(2);
			return;
		}
		backwardscount--;
	}
    
	stackPushInt(0);
}

void gsc_saveposition_getangles(int id)
{
	stackPushVector(playersaves_selected[id]->angles);
}

void gsc_saveposition_getfpsmode(int id)
{
	stackPushInt(playersaves_selected[id]->FPSMode);
}

void gsc_saveposition_getorigin(int id)
{
	stackPushVector(playersaves_selected[id]->origin);
}

void gsc_saveposition_getsavenum(int id)
{
	stackPushInt(playersaves_selected[id]->saveNum);
}

void gsc_saveposition_getflags(int id)
{
	stackPushInt(playersaves_selected[id]->flags);
}

void gsc_saveposition_getcheatstatus(int id)
{
	stackPushVector(playersaves_selected[id]->origin);
}

void gsc_saveposition_getgroundentity(int id)
{
	if(playersaves_selected[id]->groundentity == NULL)
		stackPushUndefined();
	else
		stackPushEntity(playersaves_selected[id]->groundentity);
}

void gsc_saveposition_getexplosivejumps(int id)
{
    stackPushInt(playersaves_selected[id]->explosiveJumps);
}

void gsc_saveposition_getdoubleexplosives(int id)
{
    stackPushInt(playersaves_selected[id]->doubleExplosives);
}

void gsc_saveposition_getcheckpointid(int id)
{
    if(playersaves_selected[id]->checkPointId != -1)
    {
        stackPushInt(playersaves_selected[id]->checkPointId);
    }
    else
    {
        stackPushUndefined();
    }
}

#ifdef __cplusplus
}
#endif // __cplusplus
