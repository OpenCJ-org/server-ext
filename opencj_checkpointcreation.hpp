#ifndef _OPENCJ_CHECKPOINTCREATION_HPP_
#define _OPENCJ_CHECKPOINTCREATION_HPP_

void Gsc_Cpc_Cleanup(int id);
void Gsc_Cpc_GetLastIndex(int id);
void Gsc_Cpc_Confirm(int id);
void Gsc_Cpc_Select(int id);
void Gsc_Cpc_Count(int id);
void Gsc_Cpc_SetRoute(int id);
void Gsc_Cpc_GetRoute(int id);
void Gsc_Cpc_GetClosestCheckpointIdx(int id);
void Gsc_Cpc_ClearAll(int id);
void Gsc_Cpc_GetErrorStr(int id);
void Gsc_Cpc_Import(int id);
void Gsc_Cpc_Export(int id);

void Gsc_Cpc_AddOrg(int id);
void Gsc_Cpc_SetIsInAir(int id);
void Gsc_Cpc_SetHeight(int id);
void Gsc_Cpc_SetParent(int id);
void Gsc_Cpc_SetIsFinish(int id);
void Gsc_Cpc_SetAllowEle(int id);
void Gsc_Cpc_SetAllowAnyFPS(int id);
void Gsc_Cpc_SetAllowDoubleRPG(int id);

// Getter functions for checkpoint properties

void Gsc_Cpc_GetOrgs(int id);
void Gsc_Cpc_GetIsInAir(int id);
void Gsc_Cpc_GetHeight(int id);
void Gsc_Cpc_GetParent(int id);
void Gsc_Cpc_GetIsFinish(int id);
void Gsc_Cpc_GetAllowEle(int id);
void Gsc_Cpc_GetAllowAnyFPS(int id);
void Gsc_Cpc_GetAllowDoubleRPG(int id);

#endif // _OPENCJ_CHECKPOINTCREATION_HPP_
