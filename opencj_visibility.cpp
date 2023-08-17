///
/// Player & item visibility (hiding from players/everyone etc), shared between libcod and cod4x
///

/**************************************************************************
 * Includes                                                               *
 **************************************************************************/

#include "shared.hpp"

#include <string.h>

/**************************************************************************
 * Defines                                                                *
 **************************************************************************/


/**************************************************************************
 * Types                                                                  *
 **************************************************************************/

typedef enum
{
    HIDE_NONE = 0,  // Don't hide players (except ignored and muted ones)
    HIDE_NEAR = 1,  // Hide near players
    HIDE_ALL  = 2,  // Hide all players
} eHideMode_t;

typedef struct
{
    int radiusSquared;  // To prevent expensive operations every frame
    int height;         // Cylinder pattern for hiding
} sHideRadiusInfo_t;

/**************************************************************************
 * Globals                                                                *
 **************************************************************************/

// This array contains the players that are hidden for everyone (i.e. muted)
static bool opencj_globalHideList[MAX_CLIENTS];

// This array is a list for each entity, containing space for each clientNum to be ignored
// [dimension1][dimension2] -> dimension1 is the entity for whose list is it
static bool opencj_playerHideList[MAX_CLIENTS][MAX_CLIENTS];

// This array contains the hide radius + Z distance (cylinder shape) for each player
static sHideRadiusInfo_t opencj_hideCylinder[MAX_CLIENTS];

// This array contains the visibility configuration for each player
static eHideMode_t opencj_hideModeConfig[MAX_CLIENTS];

/**************************************************************************
 * Local functions                                                        *
 **************************************************************************/

static int getFirstParamEntityNum()
{
    if (Scr_GetNumParam() < 1)
    {
        stackError("Expected (at least) 1 argument: entityNum");
        return -1;
    }

    int entNum = -1;
    stackGetParamInt(0, &entNum);
    if (entNum == -1)
    {
        stackError("Entity to ignore is NULL");
        return -1;
    }

    return entNum;
}

static void setEntityVisibilityForPlayer(gentity_t *ent, gentity_t *player, bool shouldHide)
{
    ent->flags &= (~ENTFLAG_INVISIBLE);
    int clientMaskIdx = player->s.number / (sizeof(uint32_t) * 8);
    if (shouldHide)
    {
        ent->r.clientMask[clientMaskIdx] |=  (1L << ((uint8_t)player->s.number & 0x1f));
    }
    else
    {
        ent->r.clientMask[clientMaskIdx] &= ~(1L << ((uint8_t)player->s.number & 0x1f));
    }
}

static void clearEntityVisibilityForAllPlayers(gentity_t *ent)
{
    ent->flags &= (~ENTFLAG_INVISIBLE);
    ent->r.clientMask[0] = ent->r.clientMask[1] = 0;
}

/**************************************************************************
 * API functions                                                          *
 **************************************************************************/

// For entities/items
void Gsc_Vis_HideFromPlayer(int toHideId)
{
    int clientNum = getFirstParamEntityNum();
    if (clientNum < 0) return;

    gentity_t *ent = &g_entities[toHideId];
    gentity_t *player = &g_entities[clientNum];
    setEntityVisibilityForPlayer(ent, player, true);
}

void Gsc_Vis_ShowToAllPlayers(int toShowId)
{
    gentity_t *ent = &g_entities[toShowId];
    clearEntityVisibilityForAllPlayers(ent);
}


// For players
void Gsc_Vis_AddPlayerToHideList(int clientNum)
{
    int toHideId = getFirstParamEntityNum();
    if (toHideId < 0) return;

    opencj_playerHideList[clientNum][toHideId] = true;
}

void Gsc_Vis_RemovePlayerFromHideList(int clientNum)
{
    int toShowId = getFirstParamEntityNum();
    if (toShowId < 0) return;

    opencj_playerHideList[clientNum][toShowId] = false;
}

void Gsc_Vis_HideForAll(int toHideId)
{
    int shouldBeHidden = 0;
    if (Scr_GetNumParam() >= 1)
    {
        stackGetParamInt(0, &shouldBeHidden);
    }

    opencj_globalHideList[toHideId] = (shouldBeHidden > 0);
}

void Gsc_Vis_SetHideRadius(int playerId)
{
    int radius = 0;
    if ((Scr_GetNumParam() < 1) || !stackGetParamInt(0, &radius))
    {
        stackError("Expected 1 argument: radius (int)");
        return;
    }

    opencj_hideCylinder[playerId].radiusSquared = (radius * radius);
    opencj_hideCylinder[playerId].height = (radius * 2);
}

// Separate functions are slightly cleaner for GSC so it doesn't have to remember ints or strings that match the hide mode
void Gsc_Vis_SetHideModeNone(int id)
{
    opencj_hideModeConfig[id] = HIDE_NONE;
}
void Gsc_Vis_SetHideModeNear(int id)
{
    opencj_hideModeConfig[id] = HIDE_NEAR;
}
void Gsc_Vis_SetHideModeAll(int id)
{
    opencj_hideModeConfig[id] = HIDE_ALL;
}

// Called by GSC upon initialization before it pushes the new config for a player
void Gsc_Vis_InitVisibility(int playerId)
{
    memset(opencj_playerHideList[playerId], false, sizeof(opencj_playerHideList[playerId]));
    opencj_hideCylinder[playerId].radiusSquared = 0;
    opencj_hideCylinder[playerId].height = 0;
    opencj_hideModeConfig[playerId] = HIDE_NONE;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        opencj_playerHideList[i][playerId] = false;
    }
}

// Called from GSC loop for each player
void Gsc_Vis_UpdatePlayerVisibility()
{
    // i = player that wants to hide a player
    for (int i = 0; i < sv_maxclients->integer; i++)
    {
        gentity_t *wantsToHide = &g_entities[i];
        if (!wantsToHide->client || (svs.clients[i].state != CS_ACTIVE)) continue;

        if (!svs.clients[i].gentity || !svs.clients[i].gentity->client)
        {
            continue;
        }

        // j = player that will be hidden
        for (int j = 0; j < sv_maxclients->integer; j++)
        {
            if (j == i) continue; // Don't do this for themself

            gentity_t *willBeHidden = &g_entities[j];
            if (!willBeHidden->client || (svs.clients[j].state != CS_ACTIVE)) continue;

            if(svs.clients[i].gentity->client->sess.sessionState == SESS_STATE_SPECTATOR)
            {
                setEntityVisibilityForPlayer(willBeHidden, wantsToHide, false);
            }
            else if (opencj_globalHideList[j])
            {
                setEntityVisibilityForPlayer(willBeHidden, wantsToHide, true);
            }
            else if (opencj_playerHideList[i][j])
            {
                setEntityVisibilityForPlayer(willBeHidden, wantsToHide, true);
            }
            else if (opencj_hideModeConfig[i] == HIDE_NONE)
            {
                setEntityVisibilityForPlayer(willBeHidden, wantsToHide, false);
            }
            else if (opencj_hideModeConfig[i] == HIDE_ALL)
            {
                setEntityVisibilityForPlayer(willBeHidden, wantsToHide, true);
            }
            else /* HIDE_NEAR */
            {
                int diffOriginX = (willBeHidden->r.currentOrigin[0] - wantsToHide->r.currentOrigin[0]) + 0.5;
                int diffOriginY = (willBeHidden->r.currentOrigin[1] - wantsToHide->r.currentOrigin[1]) + 0.5;
                int diffOriginZ = (willBeHidden->r.currentOrigin[2] - wantsToHide->r.currentOrigin[2]) + 0.5;

                if (abs(diffOriginZ) > opencj_hideCylinder[i].height)
                {
                    setEntityVisibilityForPlayer(willBeHidden, wantsToHide, false);
                }
                else
                {
                    int squared2DDistanceBetweenPlayers = (diffOriginX * diffOriginX) + (diffOriginY * diffOriginY);
                    if (squared2DDistanceBetweenPlayers > opencj_hideCylinder[i].radiusSquared)
                    {
                        setEntityVisibilityForPlayer(willBeHidden, wantsToHide, false);
                    }
                    else
                    {
                        setEntityVisibilityForPlayer(willBeHidden, wantsToHide, true);
                    }
                }
            }
        }
    }
}