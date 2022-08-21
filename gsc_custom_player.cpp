#include "gsc_custom_player.hpp"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

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

void Gsc_Player_SV_GameSendServerCommand(int id)
{
	int reliable;
	char *message;

	if ( ! stackGetParams("si", &message, &reliable))
	{
		stackError("Gsc_Player_SV_GameSendServerCommand() one or more arguments is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	SV_GameSendServerCommand(id, reliable, message);
	stackPushBool(qtrue);
}

void Gsc_Player_ClearFPSFilter(int id)
{
	playerState_t *ps = SV_GameClientNum(id);
	opencj_clearPlayerFPS(ps->clientNum);
}

#ifdef __cplusplus
}
#endif // __cplusplus