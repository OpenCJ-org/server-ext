#ifndef _GSC_CUSTOM_PLAYER_HPP_
#define _GSC_CUSTOM_PLAYER_HPP_

#include "shared.hpp"

#include "opencj_fps.hpp"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void Gsc_Player_SetPMFlags(int id);
void Gsc_Player_GetPMFlags(int id);
void Gsc_Player_GetPMTime(int id);
void Gsc_Player_JumpClearStateExtended(int id);
void Gsc_Player_GetGroundEntity(int id);
void Gsc_player_GetJumpSlowdownTimer(int id);
void Gsc_Player_setWeaponAmmoClip(int id);
void Gsc_Player_switchToWeaponSeamless(int id);
void Gsc_Player_SV_GameSendServerCommand(int id);
void Gsc_Player_GetQueuedReliableMessages(int id);
void Gsc_Player_ClearFPSFilter(int id);
void Gsc_Player_setOriginAndAngles(int id);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif
