#ifndef __FUNCTIONS_HPP_
#define __FUNCTIONS_HPP_

{"isvalidint", Gsc_Utils_IsValidInt, 0},
{"isvalidfloat", Gsc_Utils_IsValidFloat, 0},
{"containsillegalchars", Gsc_Utils_ContainsIllegalChars, 0},
{"hexstringtoint", Gsc_Utils_HexStringToInt, 0},
{"inttohexstring", Gsc_Utils_IntToHexString, 0},
{"createrandomint", Gsc_Utils_CreateRandomInt, 0},
{"rand", Gsc_Utils_Rand, 0},
{"isalphanumeric", Gsc_Utils_IsAlphaNumeric, 0},
{"getcodversion", Gsc_Utils_GetCodVersion, 0},
{"mysql_init", gsc_mysql_init},
{"mysql_real_connect", gsc_mysql_real_connect},
{"mysql_close", gsc_mysql_close},
{"mysql_query", gsc_mysql_query},
{"mysql_errno", gsc_mysql_errno},
{"mysql_error", gsc_mysql_error},
{"mysql_affected_rows", gsc_mysql_affected_rows},
{"mysql_store_result", gsc_mysql_store_result},
{"mysql_num_rows", gsc_mysql_num_rows},
{"mysql_num_fields", gsc_mysql_num_fields},
{"mysql_field_seek", gsc_mysql_field_seek},
{"mysql_fetch_field", gsc_mysql_fetch_field},
{"mysql_fetch_row", gsc_mysql_fetch_row},
{"mysql_free_result", gsc_mysql_free_result},
{"mysql_real_escape_string", gsc_mysql_real_escape_string},
{"mysql_async_create_query", gsc_mysql_async_create_query},
{"mysql_async_create_query_nosave", gsc_mysql_async_create_query_nosave},
{"mysql_async_initializer", gsc_mysql_async_initializer},
{"mysql_async_getdone_list", gsc_mysql_async_getdone_list},
{"mysql_async_getresult_and_free", gsc_mysql_async_getresult_and_free},
{"mysql_reuse_connection", gsc_mysql_reuse_connection},
{"mysql_setup_longquery", gsc_mysql_setup_longquery},
{"mysql_free_longquery", gsc_mysql_free_longquery},
{"mysql_append_longquery", gsc_mysql_append_longquery},
{"mysql_async_execute_longquery", gsc_mysql_async_execute_longquery},
{"updateplayervisibility", Gsc_Vis_UpdatePlayerVisibility},
{"discord_connect", Gsc_Discord_Connect},
{"discord_getevent", Gsc_Discord_GetEvent},
{"discord_onevent", Gsc_Discord_OnEvent},
#ifndef COD4
{"setminimap", Gsc_Utils_VoidFunc},
{"getConfigStringByIndex", Gsc_Utils_VoidFunc},
#endif
{"clearAllDemos", Gsc_Demo_ClearAllDemos},
{"demoHasKeyFrames", Gsc_Demo_HasKeyFrames},
{"numberOfDemoFrames", Gsc_Demo_NumberOfFrames},
{"numberOfDemoKeyFrames", Gsc_Demo_NumberOfKeyFrames},
{"createDemo", Gsc_Demo_CreateDemo, 0},
{"destroyDemo", Gsc_Demo_DestroyDemo, 0},
{"completeDemo", Gsc_Demo_CompleteDemo, 0},
{"addFrameToDemo", Gsc_Demo_AddFrame, 0},
{"setconfigstringbyindex", Gsc_Utils_setConfigStringByIndex, 0},

#endif // __FUNCTIONS_HPP_
