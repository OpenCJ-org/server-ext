#include "gsc_custom_player.hpp"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void Gsc_Player_SetPMFlags(int id)
{
    int numParams = Scr_GetNumParam();
    const char *szSyntax = "expected 1-2 arguments: <pm_flags> [pm_time]";
    if ((numParams < 1) || (numParams > 2))
    {
        stackError(szSyntax);
        return;
    }

    if (stackGetParamType(0) != STACK_INT)
    {
        stackError(szSyntax);
        return;
    }

    if ((numParams > 1) && (stackGetParamType(1) != STACK_INT))
    {
        stackError(szSyntax);
        return;
    }

    int flags = 0;
    stackGetParamInt(0, &flags);

    int time = 0;
    if (numParams > 1)
    {
        stackGetParamInt(1, &time);
    }

    playerState_t *ps = SV_GameClientNum(id);
    if (ps != NULL)
    {
        ps->pm_flags = flags;
        if (numParams > 1)
        {
            ps->pm_time = time;
        }
    }
}

void Gsc_Player_GetPMFlags(int id)
{
    playerState_t *ps = SV_GameClientNum(id);
    stackPushInt(ps->pm_flags);
}

void Gsc_Player_GetPMTime(int id)
{
    playerState_t *ps = SV_GameClientNum(id);
    stackPushInt(ps->pm_time);
}

void Gsc_Player_GetGroundEntity(int id)
{
    playerState_t *ps = SV_GameClientNum(id);
    if(ps->groundEntityNum < 1022)
    {
        stackPushEntity(&g_entities[ps->groundEntityNum]);
        return;
    }

    stackPushUndefined();
}

void Gsc_Player_setOriginAndAngles(int id)
{
	//sets origin, angles
	//resets pm_flags and velocity
	//keeps stance
#ifndef COD2
	extern void __cdecl G_EntUnlink(gentity_t *);
	extern void __cdecl SV_UnlinkEntity(gentity_t *);
	extern void __cdecl G_ClientStopUsingTurret(gentity_t *);
	extern level_locals_t level;
	extern void __cdecl SetClientViewAngle(gentity_s *ent, const float *angle);
	extern void __cdecl Pmove(pmove_t *);
#endif

	//identical for cod2 and cod4
	#define PMF_DUCKED 0x2
	#define PMF_PRONE 0x1 //untested
	#define EF_TELEPORT_BIT 0x2

	gentity_s *ent;
	vec3_t origin;
	vec3_t angles;
	ent = &g_entities[id];
	if(!ent->client)
	{
		Scr_ObjectError(va("entity %i is not a player", id));
	}

	Scr_GetVector(0, origin);
	Scr_GetVector(1, angles);

	bool isUsingTurret;
#ifdef COD2
	isUsingTurret = ((ent->client->ps.pm_flags & 0x800000) != 0  && (ent->client->ps.eFlags & 0x300) != 0);
#else
	isUsingTurret = ((ent->client->ps.otherFlags & 4) != 0  && (ent->client->ps.eFlags & 0x300) != 0);
#endif

	//stop using MGs
	if(isUsingTurret)
	{
		G_ClientStopUsingTurret(&g_entities[ent->client->ps.viewlocked_entNum]);
	}

	G_EntUnlink(ent);

	//unlink client from linkto() stuffs

	if (ent->r.linked)
	{
		SV_UnlinkEntity(ent);
	}

	//clear flags
	ent->client->ps.pm_flags &= (PMF_DUCKED | PMF_PRONE);//keep stance
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT; //alternate teleport flag, unsure why

	//set times
	ent->client->ps.pm_time = 0;
	ent->client->ps.jumpTime = 0; //to reset wallspeed effects

	//set origin
	VectorCopy(origin, ent->client->ps.origin);
    G_SetOrigin(ent, origin);


	//reset velocity
	ent->client->ps.velocity[0] = 0;
	ent->client->ps.velocity[1] = 0;
	ent->client->ps.velocity[2] = 0;

#ifdef COD4
	ent->client->ps.sprintState.sprintButtonUpRequired = 0;
	ent->client->ps.sprintState.sprintDelay = 0;
	ent->client->ps.sprintState.lastSprintStart = 0;
	ent->client->ps.sprintState.lastSprintEnd = 0;
	ent->client->ps.sprintState.sprintStartMaxLength = 0;
#endif

	//pretend we're not proning so that prone angle is ok after having called SetClientViewAngle (otherwise it gets a correction)
	int flags = ent->client->ps.pm_flags;
	ent->client->ps.pm_flags &= ~PMF_PRONE;
	ent->client->sess.cmd.serverTime = level.time; //if this isnt set then errordecay takes place

    // Prevent players from being able able to walk around without starting a run (because WASD callback is only called)
    extern void opencj_clearPlayerMovementCheckVars(int);
    opencj_clearPlayerMovementCheckVars(id);

	SetClientViewAngle(ent, angles);

	//create a pmove object and execute to bypass the errordecay thing
	pmove_t pm;
	memset(&pm, 0, sizeof(pm));
	pm.ps = &ent->client->ps;
	memcpy(&pm.cmd, &ent->client->sess.cmd, sizeof(pm.cmd));
    pm.cmd.serverTime = level.time - 50;

    ent->client->sess.oldcmd.serverTime = level.time - 100;
	pm.oldcmd = ent->client->sess.oldcmd;
	pm.tracemask = 42008593;
	pm.handler = 1;
	Pmove(&pm);

	//reset velocity
	ent->client->ps.velocity[0] = 0;
	ent->client->ps.velocity[1] = 0;
	ent->client->ps.velocity[2] = 0;

	//restore prone if any
	ent->client->ps.pm_flags = flags;

	SV_LinkEntity(ent);
}

void Gsc_Player_JumpClearStateExtended(int id)
{
	playerState_t *ps = SV_GameClientNum(id);
	ps->pm_flags &= SHARED_CLEARJUMPSTATE_MASK;
	ps->pm_time = 0;
	ps->jumpTime = 0; //to reset wallspeed effects
	ps->jumpOriginZ = 0.0;
}

void Gsc_player_GetJumpSlowdownTimer(int id)
{
	playerState_t *ps = SV_GameClientNum(id);
	int value = ps->pm_time;
	stackPushInt(value);
}

void Gsc_Player_setWeaponAmmoClip(int id) //pls comment out for cod4
{
}

void Gsc_Player_switchToWeaponSeamless(int id)
{
    const char *szWeaponName = NULL;
    if (stackGetParamString(0, &szWeaponName))
    {
        playerState_t *ps = SV_GameClientNum(id);
        int weaponIdx = G_GetWeaponIndexForName(szWeaponName);
        if (BG_IsWeaponValid(ps, weaponIdx))
        {
            ps->weapon = weaponIdx;
            //ps->weaponstate = 0; // WEAPON_READY
            SV_GameSendServerCommand(id, 1, va("%c %i", 'a', weaponIdx));
        }
    }
}

void Gsc_Player_SV_GameSendServerCommand(int id)
{
	int reliable;
	char *message;

	if (!stackGetParams("si", &message, &reliable))
	{
		stackError("Gsc_Player_SV_GameSendServerCommand() one or more arguments is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	SV_GameSendServerCommand(id, reliable, message);
	stackPushBool(qtrue);
}

void Gsc_Player_GetQueuedReliableMessages(int id)
{
    client_t *pClient = &svs.clients[id];
    if (!pClient)
    {
        stackPushUndefined();
        return;
    }

    int nrQueued = pClient->reliableSequence - pClient->reliableAcknowledge;
    stackPushInt(nrQueued);
}

void Gsc_Player_ClearFPSFilter(int id)
{
	playerState_t *ps = SV_GameClientNum(id);
	opencj_clearPlayerFPS(ps->clientNum);
}

#ifdef __cplusplus
}
#endif // __cplusplus