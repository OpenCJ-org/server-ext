#include "gsc_custom_player.hpp"

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
