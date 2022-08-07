#ifndef __METHODS_HPP_
#define __METHODS_HPP_

{"saveposition_initclient", gsc_saveposition_initclient, 0},
{"saveposition_save", gsc_saveposition_save, 0},
{"saveposition_selectsave", gsc_saveposition_selectsave, 0},
{"saveposition_getangles", gsc_saveposition_getangles, 0},
{"saveposition_getorigin", gsc_saveposition_getorigin, 0},
{"saveposition_getgroundentity", gsc_saveposition_getgroundentity, 0},
#ifndef COD4 // CoD4 actually has these functions
{"setactionslot", Gsc_Utils_Void, 0},
{"setperk", Gsc_Utils_Void, 0},
#endif
{"jumpclearstateextended", Gsc_Player_JumpClearStateExtended, 0},
{"getgroundentity", Gsc_Player_GetGroundEntity, 0},
{"allowelevate", Gsc_Utils_Void, 0},
{"getjumpslowdowntimer", Gsc_player_GetJumpSlowdownTimer, 0},
{"saveposition_getnadejumps", gsc_saveposition_getnadejumps, 0},
{"saveposition_getrpgjumps", gsc_saveposition_getrpgjumps, 0},
{"saveposition_getdoublerpgs", gsc_saveposition_getdoublerpg, 0},
{"saveposition_getcheckpointid", gsc_saveposition_getcheckpointid, 0},
{"saveposition_getflags", gsc_saveposition_getflags, 0},
{"isthinking", Gsc_Utils_IsEntityThinking, 0},

#endif // __METHODS_HPP_
