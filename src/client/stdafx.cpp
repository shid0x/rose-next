#include "stdafx.h"

#include "sound\IO_Sound.h"
#include "util\CMessageQ.h"
#include "OBJECT.h"
#include "BULLET.h"
#include "CSkyDOME.h"
#include "rose/io/stb.h"
#include "Common\IO_AI.h"
#include "IO_MESH.h"
#include "IO_MATERIAL.h"
#include "IO_EFFECT.h"
#include "IO_EVENT.h"
#include "CCamera.h"
#include "CViewMSG.h"

CApplication* g_pCApp = NULL;
CSystemMESSAGE g_SysMSG;

CRangeTBL* g_pCRange;
CSoundLIST* g_pSoundLIST = NULL;

// CKeyboard		*g_pCKeybd;
// CMouse			*g_pCMouse;
CTERRAIN* g_pTerrain;

CMatLIST g_MatFILE(NAME_MATERIAL);
CMeshLIST g_MeshFILE;
CMotionLIST g_MotionFILE;
CEffectLIST* g_pEffectLIST;
CEventLIST* g_pEventLIST;

CCamera* g_pCamera = NULL;

CObjectMANAGER* g_pObjMGR = NULL;
CBulletMANAGER* g_pBltMGR = NULL;

CViewMSG* g_pViewMSG;

STBDATA g_TblHAIR;
STBDATA g_TblFACE;
STBDATA g_TblARMOR;
STBDATA g_TblGAUNTLET;
STBDATA g_TblBOOTS;
STBDATA g_TblHELMET;
STBDATA g_TblWEAPON;
STBDATA g_TblSUBWPN;
STBDATA g_TblEFFECT;
STBDATA g_TblNPC;
STBDATA g_TblAniTYPE;
STBDATA g_TblPRODUCT;
STBDATA g_TblNATUAL;
STBDATA g_TblDropITEM;

STBDATA g_TblFACEITEM;
STBDATA g_TblUSEITEM;
STBDATA g_TblBACKITEM;
STBDATA g_TblGEMITEM;
STBDATA g_TblQUESTITEM;
STBDATA g_TblJEWELITEM;
STBDATA g_TblStore;
STBDATA g_PatITEM;

STBDATA g_TblWARP;
STBDATA g_TblEVENT;

STBDATA g_TblSKY;

STBDATA g_TblZONE;

STBDATA* g_pTblSTBs[ITEM_TYPE_RIDE_PART + 1];

STBDATA g_TblString;
STBDATA g_TblHitSound;

STBDATA g_TblAVATAR;
STBDATA g_TblResolution;

STBDATA g_TblSTATE;

STBDATA g_TblCamera;
STBDATA g_TblRangeSet;
STBDATA g_TblUnion;
STBDATA g_TblClass;
STBDATA g_TblStepSound;

STBDATA g_TblItemGRADE;

const float g_fMinHeight = -999999999.f;
const float g_fMaxDistance = 999999999.f;

CAI_LIST g_AI_LIST;
