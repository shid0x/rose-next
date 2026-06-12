#ifndef __DEF_STB_H
#define __DEF_STB_H
#include "rose/io/stb.h"
//-------------------------------------------------------------------------------------------------

#define SP_LEVEL(L) g_TblSkillPoint.get_int32(L, 0)
#define SP_POINT(L) g_TblSkillPoint.get_int32(L, 1)

extern STBDATA g_TblSkillPoint;

//-------------------------------------------------------------------------------------------------
#endif