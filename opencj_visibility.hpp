#ifndef _OPENCJ_VISIBILITY_HPP_
#define _OPENCJ_VISIBILITY_HPP_

void Gsc_Vis_HideFromPlayer(int toHideId);
void Gsc_Vis_ShowToAllPlayers(int toShowId);
void Gsc_Vis_AddPlayerToHideList(int clientNum);
void Gsc_Vis_RemovePlayerFromHideList(int clientNum);
void Gsc_Vis_HideForAll(int toHideId);
void Gsc_Vis_SetHideRadius(int playerId);
void Gsc_Vis_SetHideModeNone(int id);
void Gsc_Vis_SetHideModeNear(int id);
void Gsc_Vis_SetHideModeAll(int id);
void Gsc_Vis_InitVisibility(int playerId);

void Gsc_Vis_UpdatePlayerVisibility();

#endif // _OPENCJ_VISIBILITY_HPP_
