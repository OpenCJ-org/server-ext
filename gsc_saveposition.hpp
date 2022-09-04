#ifndef _GSC_SAVEPOSITION_HPP_
#define _GSC_SAVEPOSITION_HPP_

#include "shared.hpp"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void gsc_saveposition_initclient(int id);
void gsc_saveposition_save(int id);
void gsc_saveposition_selectsave(int id);
void gsc_saveposition_getangles(int id);
void gsc_saveposition_getorigin(int id);
void gsc_saveposition_getgroundentity(int id);
void gsc_saveposition_getnadejumps(int id);
void gsc_saveposition_getrpgjumps(int id);
void gsc_saveposition_getdoublerpg(int id);
void gsc_saveposition_getcheckpointid(int id);
void gsc_saveposition_getflags(int id);
void gsc_saveposition_getfps(int id);
void gsc_saveposition_getsavenum(int id);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif
