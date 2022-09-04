#ifndef _OPENCJ_DEMO_HPP_
#define _OPENCJ_DEMO_HPP_

//==========================================================================
// Functions that do work for any demo                                        
//==========================================================================

void Gsc_Demo_ClearAllDemos();
void Gsc_Demo_HasKeyFrames();
void Gsc_Demo_NumberOfFrames();
void Gsc_Demo_NumberOfKeyFrames();
void Gsc_Demo_CreateDemo();
void Gsc_Demo_DestroyDemo();
void Gsc_Demo_AddFrame();
void Gsc_Demo_CompleteDemo();

//==========================================================================
// Functions related to playback & control                                        
//==========================================================================

void Gsc_Demo_SelectPlaybackDemo(int playerId);
void Gsc_Demo_ReadFrame_Origin(int playerId);
void Gsc_Demo_ReadFrame_Angles(int playerId);
void Gsc_Demo_ReadFrame_SaveNow(int playerId);
void Gsc_Demo_ReadFrame_LoadNow(int playerId);
void Gsc_Demo_ReadFrame_RPGNow(int playerId);
void Gsc_Demo_SkipFrame(int playerId);
void Gsc_Demo_SkipKeyFrame(int playerId);
void Gsc_Demo_NextFrame(int playerId);
void Gsc_Demo_PrevFrame(int playerId);
void Gsc_Demo_ReadFrame_NextKeyFrame(int playerId);
void Gsc_Demo_ReadFrame_PrevKeyFrame(int playerId);
void Gsc_Demo_ReadFrame_Flags(int playerId);
void Gsc_Demo_ReadFrame_FPS(int playerId);

#endif // _OPENCJ_DEMO_HPP_
