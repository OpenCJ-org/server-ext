// This file handles includes based on CoD version (CoD2 vs CoD4)

#ifndef _SHARED_HPP_
#define _SHARED_HPP_

#ifdef COD2 // ============================================================= COD2

#include "../libcod/gsc.hpp"
#include "../libcod/declarations.hpp"

#define SHARED_CLEARJUMPSTATE_MASK  0xFFF7FFFF

#define Shared_GetPort()                Cvar_FindVar("net_port")->integer

#define Shared_Printf  Com_DPrintf

#define stackMakeArray      stackPushArray
#define stackPushArrayNext  stackPushArrayLast

#define COD_MAX_STRINGLENGTH    COD2_MAX_STRINGLENGTH

#else
#ifdef COD4 // ============================================================= COD4

#include "../cod4x-server/src/scr_vm.h"

#define KEY_MASK_FIRE           1
#define KEY_MASK_MELEE          4
//#define KEY_MASK_USE            0x28 /* Combination */

#define SV_GameClientNum                SV_GameClientNum
#define SHARED_CLEARJUMPSTATE_MASK      (~0x4000)

#define Shared_GetPort()                Cvar_VariableIntegerValue("net_port")

#define Scr_ExecEntThread           Scr_ExecEntThread
#define Scr_ExecThread              Scr_ExecThread
#define Scr_FreeThread              Scr_FreeThread

#define stackGetParamInt(argnum, address)       (*address = Scr_GetInt(argnum))
#define stackGetParamString(argnum, address)    (*address = Scr_GetString(argnum))
#define stackGetParamFloat(argnum, address)     (*address = Scr_GetFloat(argnum))
#define stackGetParamVector(argnum, address)    (Scr_GetVector(argnum, address))
#define stackGetParamFunction(argnum, address)  (*address = Scr_GetFunc(argnum))

#define stackPushUndefined  Scr_AddUndefined
#define stackPushBool       Scr_AddBool
#define stackPushInt        Scr_AddInt
#define stackPushFloat      Scr_AddFloat
#define stackPushString     Scr_AddString
#define stackPushVector     Scr_AddVector
#define stackPushEntity     Scr_AddEntity
#define stackMakeArray      Scr_MakeArray
#define stackPushArrayNext  Scr_AddArray
#define stackGetParams      Scr_GetMultipleValues
#define stackGetParamType   Scr_GetType

#define STACK_UNDEFINED           0x00
#define STACK_BEGIN_REF           0x01
#define STACK_POINTER             0x01
#define STACK_STRING              0x02
#define STACK_ISTRING             0x03
#define STACK_VECTOR              0x04
#define STACK_END_REF             0x05
#define STACK_FLOAT               0x05
#define STACK_INT                 0x06
#define STACK_CODEPOS             0x07
#define STACK_PRECODEPOS          0x08
#define STACK_FUNCTION            0x09
#define STACK_STACK               0x0A
#define STACK_ANIMATION           0x0B
#define STACK_DEVELOPER_CODEPOS   0x0C
#define STACK_INCLUDE_CODEPOS     0x0D
#define STACK_THREAD              0x0E
#define STACK_NOTIFY_THREAD       0x0F
#define STACK_TIME_THREAD         0x10
#define STACK_CHILD_THREAD        0x11
#define STACK_OBJECT              0x12
#define STACK_DEAD_ENTITY         0x13
#define STACK_ENTITY              0x14
#define STACK_ARRAY               0x15
#define STACK_DEAD_THREAD         0x16
#define STACK_COUNT               0x17
#define STACK_THREAD_LIST         0x18
#define STACK_ENDON_LIST          0x19


#define COD_MAX_STRINGLENGTH      MAX_STRING_CHARS

#define stackError Scr_Error

#define Shared_Printf(fmt, ...)   Com_Printf(CON_CHANNEL_SERVER, fmt, ##__VA_ARGS__)

#endif // #else ifdef COD4
#endif // #ifdef COD2

#endif
