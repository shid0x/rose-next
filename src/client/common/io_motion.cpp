#include "stdAFX.h"

#include "IO_Motion.h"
#include "Game.h"
#include "..\\Util\\VFSManager.h"

#define FRAME_ATTACK

tagMOTION::tagMOTION() {
    m_hMotion = NULL;
    m_wFPS = 1;
    m_wTotalFrame = 0;
    m_nActionIdx = 0;
    m_pFrameEvent = NULL;
    m_nActionPointCNT = 0;
    m_wTatalAttackFrame = 0;
    m_iInterpolationInterval = 500;
}

bool
tagMOTION::LoadZMO(char* szFileName) {

    CFileSystem* pFileSystem = (CVFSManager::GetSingleton()).GetFileSystem();
    if (pFileSystem->OpenFile(szFileName) == false) {
        char* szStr = CStr::Printf("File [%s] open error ", szFileName);
        g_pCApp->ErrorBOX(szStr, "ERROR", MB_OK);
        return false;
    }

    // header section
    char* szTag = CGameStr::ReadString(pFileSystem);
    if (strncmp(szTag, "ZMO0002", 7)) {
        pFileSystem->CloseFile();
        (CVFSManager::GetSingleton()).ReturnToManager(pFileSystem);
        return false;
    }

    UINT uiValue;
    // read the speed of frames
    pFileSystem->ReadUInt32(&uiValue);
    m_wFPS = uiValue;

    // read the number of frames
    pFileSystem->ReadUInt32(&uiValue);
    m_wTotalFrame = uiValue;

    WORD wTotalFrames;

    char szTAG[4];
    pFileSystem->Seek(-4, FILE_POS_END);

    pFileSystem->Read(szTAG, sizeof(char) * 4);

    int iExtendedVersion = -1;

    if (strncmp(szTAG, "EZMO", 4) == 0)
        iExtendedVersion = 2;
    else if (strncmp(szTAG, "3ZMO", 4) == 0)
        iExtendedVersion = 3;

    if (iExtendedVersion >= 2) {
        // extanded zmo file...
        long lFileSize = sizeof(long);
        pFileSystem->Seek(-4 - lFileSize, FILE_POS_END); // eof-8
        pFileSystem->Read(&lFileSize, sizeof(long));
        pFileSystem->Seek(lFileSize, FILE_POS_SET);

        // read total frame
        pFileSystem->ReadUInt16(&wTotalFrames);
        _ASSERT(wTotalFrames == m_wTotalFrame);

        // read frame event
        m_pFrameEvent = new short[wTotalFrames];

        _ASSERT(m_pFrameEvent);

        pFileSystem->Read(m_pFrameEvent, sizeof(short) * wTotalFrames);

        short nF;
        for (nF = 0; nF < wTotalFrames; nF++) {
            if (m_pFrameEvent[nF]) {
                m_nActionPointCNT++;

                switch (m_pFrameEvent[nF]) {
                    case 21:
                    case 22:
                    case 23:
                    case 24:
                    case 25:
                    case 26:
                    case 27:
                    case 28:
                    case 10:
                    case 20:
                    case 56:
                    case 66:
                    case 57:
                    case 67:
                        m_wTatalAttackFrame++;
                        break;
                }
            }
        }
    }

    LogString(LOG_NORMAL, " %s MOTION FRAME%d\n", szFileName, m_wTatalAttackFrame);

    if (iExtendedVersion >= 3) {
        // added
        pFileSystem->Read(&m_iInterpolationInterval, sizeof(int));
    }

    if (!m_nActionPointCNT) {
        int aaa = 0;
    }

    // 진행된 프레임과 맞추기 위해서..
    /// 클라이언트는 토탈프레임을 있는 그대로 받아 들이시오!@@@@@@@!!!!!!!!
    /// m_wTotalFrame --;

    pFileSystem->CloseFile();
    (CVFSManager::GetSingleton()).ReturnToManager(pFileSystem);

    return true;
}

//-------------------------------------------------------------------------------------------------
CMotionLIST::~CMotionLIST() {
    this->Free();
}
void
CMotionLIST::Free() {
#pragma message("TODO:: SAFE_DELETE( pData->m_DATA );  --> " __FILE__)

    CFileLIST<tagMOTION*>::Free();
}

//-------------------------------------------------------------------------------------------------
HNODE
CMotionLIST::KEY_GetZMOTION(unsigned int uiKEY) {
    if (uiKEY) {
        m_pTmpMotion = KEY_GetMOTION(uiKEY);
        if (m_pTmpMotion)
            return m_pTmpMotion->m_hMotion;
    }

    return NULL;
}

//-------------------------------------------------------------------------------------------------
tagMOTION*
CMotionLIST::IDX_GetMOTION(short nIndex, bool bIsFemale) {
    tagMOTION* pMotion;
    if (bIsFemale) {
        // 여자 모션
        pMotion = this->Get_DATAUseIDX(nIndex + m_nFemaleIndex);
        if (pMotion)
            return pMotion;
    }

    return this->Get_DATAUseIDX(nIndex);
}

//-------------------------------------------------------------------------------------------------
bool
CMotionLIST::Load(char* szSTBFile, short nFileNameColNO, char* szBaseDIR) {
    CGameSTB fSTB;
    char* szFileName;

    szFileName = (!szBaseDIR) ? szSTBFile : CStr::Printf("%s%s", szBaseDIR, szSTBFile);

    if (fSTB.Open(szFileName)) {
        m_nFemaleIndex = fSTB.GetRowCount();
        Alloc_FILES(m_nFemaleIndex * 2); // 남/여

        for (short nSex = 0; nSex < 2; nSex++) {
            for (short nI = 0; nI < m_nFemaleIndex; nI++) {
                if (fSTB.GetString(nFileNameColNO + nSex, nI)) {
                    if (szBaseDIR) {
                        szFileName = CStr::Printf("%s%s",
                            szBaseDIR,
                            fSTB.GetString(nFileNameColNO + nSex, nI));
                    } else {
                        szFileName = fSTB.GetString(nFileNameColNO + nSex, nI);
                    }
                    this->Add_FILE(szFileName, nI + nSex * m_nFemaleIndex);
                }
            }
        }

        fSTB.Close();
        return true;
    }

    return false;
}

//-------------------------------------------------------------------------------------------------
bool
CMotionLIST::Load_FILE(tagFileDATA<tagMOTION*>* pData) {
    SAFE_DELETE(pData->m_DATA);
    pData->m_DATA = new tagMOTION;
    if (pData->m_DATA->LoadZMO(pData->m_FileName.Get())) {
        // LogString (LOG_DEBUG_, "Load Motion[ %s ] file \n", pData->m_FileName.Get() );

#ifndef __SERVER
        char* szName = CStr::Printf(NAME_MOTION, pData->m_nTag);
        int ZZ_INTERP_SQUAD = 3, ZZ_INTERP_CATMULLROM = 4;

        pData->m_DATA->m_hMotion = ::loadMotion(szName,
            pData->m_FileName.Get(),
            1,
            ZZ_INTERP_CATMULLROM,
            ZZ_INTERP_SQUAD,
            1,
            1);
        if (pData->m_DATA->m_hMotion) {
            // 보간 시간 설정
            ::setMotionInterporationInterval(pData->m_DATA->m_hMotion,
                pData->m_DATA->m_iInterpolationInterval);
            // LoadZMO에서 구해진다.
            // pData->m_DATA->m_wTotalFrame = ::getMotionTotalFrame( pData->m_DATA->m_hMotion );
            return true;
        }
#else
        return true;
#endif
    }
    SAFE_DELETE(pData->m_DATA);

    return false;
}
void
CMotionLIST::Free_FILE(tagFileDATA<tagMOTION*>* pData) {
    if (pData->m_DATA) {
        // LogString (0x0ffff, "Free_Motion FILE[ %s ] \n", pData->m_FileName.Get() );

#ifndef __SERVER
        if (pData->m_DATA->m_hMotion) {
            ::unloadMotion(pData->m_DATA->m_hMotion);
            pData->m_DATA->m_hMotion = NULL;
        }
#else
        pData->m_DATA->m_nActionPointCNT = 0;
        SAFE_DELETE_ARRAY(pData->m_DATA->m_pActionPoint);
#endif
        pData->m_DATA->m_wTotalFrame = 0;
        SAFE_DELETE_ARRAY(pData->m_DATA->m_pFrameEvent);
        SAFE_DELETE(pData->m_DATA);
    }
}

//-------------------------------------------------------------------------------------------------
