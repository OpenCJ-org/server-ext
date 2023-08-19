#ifndef _GSC_CUSTOM_UTILS_HPP_
#define _GSC_CUSTOM_UTILS_HPP_

#include "shared.hpp"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void Gsc_Utils_Void(int);
void Gsc_Utils_ZeroInt(int);
void Gsc_Utils_VoidFunc();
void Gsc_Utils_IsValidInt();
void Gsc_Utils_IsValidFloat();
void Gsc_Utils_ContainsIllegalChars();
void Gsc_Utils_Printf();
void Gsc_Utils_VectorScale();
void Gsc_Utils_IsEntityThinking(int);
void Gsc_Utils_CreateRandomInt();
void Gsc_Utils_HexStringToInt();
void Gsc_Utils_IntToHexString();
void Gsc_Utils_GetCodVersion();
void Gsc_Utils_setConfigStringByIndex();

#ifdef __cplusplus
}
#endif // __cplusplus

#endif
