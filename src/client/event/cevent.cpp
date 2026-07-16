
#include "stdAFX.h"

#include <fcntl.h>
#include <io.h>

#include "CEvent.h"
#include "Quest_FUNC.h"
#include "Game_FUNC.h"
#include "io_quest.h"
#include "..\IO_EVENT.h"
#include "tgamectrl/TGameCtrl.h"
#include "OBJECT.h"

#include "Util\\VFSManager.h"
#include "Util\\Localizing.h"

#include "..\GameCommon\LngTbl.h"

//-------------------------------------------------------------------------------------------------

#define SC_MSG_CLOSE 0 // Close Message Window
#define SC_MSG_NEXTMSG 1 // Next Message
#define SC_MSG_NPCSAY 2
#define SC_MSG_PLAYERSELECT 3 // Menu Select :: Child데이타의 모든 SubMENU를 출력한다.
#define SC_MSG_JUMPSELECT 4 // Menu Select :: Child데이타의 모든 SubMENU를 출력한다.

//-------------------------------------------------------------------------------------------------

#ifndef __DEFINE_SCRREAD__
    #define __DEFINE_SCRREAD__
    #define SCRREAD(HFILE, BUFFER, SIZE) _ASSERT(read(HFILE, (BUFFER), (SIZE)) == (int)(SIZE))
#endif

#ifndef __DEFINE_SCRWRITE__
    #define __DEFINE_SCRWRITE__
    #define SCRWRITE(HFILE, BUFFER, SIZE) _ASSERT(write(HFILE, (BUFFER), (SIZE)) == (int)(SIZE))
#endif

#define NUM_EVENT 16

// Quest-editor appendix chunk magic: the bytes "QEX1" read as a little-endian DWORD.
// See src/tools/quest-editor/src/convo.rs (appendix format) — keep in sync.
#define QEX_APPENDIX_MAGIC 0x31584551

// Script File Header
typedef struct tag_SSC_FILE_HEADER {
    __int16 EventMask;
    //    long		EventMMT[NUM_EVENT] ;
    char ppFuncName[NUM_EVENT][32];
    DWORD ConvOff;
    DWORD ScriptOff;
} SSC_FILE_HEADER;

typedef struct tag_SSC_CONV_HEADER {
    int m_iMsgNumber; // number of message
    DWORD m_dwMsgStartOff; // offset to message body
    int m_MenuNumber; // number of menu
    DWORD m_dwMenuStartOff; // offset to menu body
} SSC_CONV_HEADER;

// this is base structure of Message
typedef struct tag_SSC_Msg {
    int iSn; // Message SN
    // int	       	iLengthOfString ;	// Message Length
    int iType; // Message Type
    long dwValue; // Value by Message Type
    char szFunc1[32];
    char szFunc2[32];
    /*
    long		dwScript ;          // Script SN			표시되기 전 스크립트...
    long		dwScript2 ;         // Second Script SN		클릭시 실행될 스키립트...
    */
    int iStrID;
    /*
    union {
        char   *pString ;			// Message String
        char    szString[0] ;		// First Element of String
    } ;
    */
} SSC_msg, SSC_Msg, SSC_MsgBase, SSC_MenuItem;

#define MAX_SCRNAME 16 // Max number for script name
////////////////////////////////////////////////////////////////////////////////
// this structure is used for Script-Mapping-Table
typedef struct tag_SSC_SCRMMT {
    char szName[MAX_SCRNAME]; // Script Name
    int iLength; // Length of Script
    DWORD Offset; // Offset for related script
} SSC_SCRMMT;

////////////////////////////////////////////////////////////////////////////////
// Script Header
typedef struct tag_SSC_SCR_HEADER {
    int m_iNumOfScript;
    SSC_SCRMMT m_ScrMMT[0];
} SSC_SCR_HEADER;
#define SIZEOFSCRHEADER (sizeof(SSC_SCR_HEADER) + sizeof(SSC_SCRMMT))
#define MAX_EVENTNANME 16

typedef struct tag_SSC_MENU_COLL {
    int m_iLength;
    int m_iNumSubMenu;
    union {
        DWORD* m_pSubMenuMMT;
        DWORD m_SubMenuMMT[0];
    };
} SSC_MENU_COLL;

char CEvent::m_TempBuffer[MAX_CONV_STR_LEN];
char CEvent::m_MBCSBuffer[MAX_CONV_STR_LEN];

//-------------------------------------------------------------------------------------------------
CEvent::CEvent(int iEventDlgType) {
    m_iScrDataCNT = 0;
    m_pScrDATA = NULL;

    m_iLuaDataLEN = 0;
    m_pLuaDATA = NULL;

    m_iLuaAppendixLEN = 0;
    m_pLuaAppendixDATA = NULL;

    m_bQuestFuncsScanned = false;

    m_pLUA = NULL;

    m_iEventDlgType = iEventDlgType;

    LogString(LOG_DEBUG_, ">>>> Event CREATED ...\n");
}
CEvent::~CEvent() {
    Del_ClickITEMS();

    SAFE_DELETE(m_pLUA);

    // 메모리는 ~xxx에서 풀림..
    SAFE_DELETE_ARRAY(m_pScrDATA);
    SAFE_DELETE_ARRAY(m_pLuaDATA);
    SAFE_DELETE_ARRAY(m_pLuaAppendixDATA);
    SAFE_DELETE_ARRAY(m_pScrMSG);

    LogString(LOG_DEBUG_, "<<<< Event DELETED ...\n");
}

///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifndef __SERVER
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////

bool
CEvent::Load(char* szFileName) {
    CFileSystem* pFileSystem = (CVFSManager::GetSingleton()).GetFileSystem();
    if (pFileSystem->OpenFile(szFileName) == false) {
        return false;
    }

    long lFileSize;
    pFileSystem->Seek(0L, FILE_POS_END);
    lFileSize = pFileSystem->Tell();
    pFileSystem->Seek(0L, FILE_POS_SET);

    this->m_HashKEY = CStr::GetHASH(szFileName);

    SSC_FILE_HEADER FileHeader;
    pFileSystem->Read((void*)&FileHeader, sizeof(SSC_FILE_HEADER));
    /*
    typedef struct tag_SSC_FILE_HEADER
    {
    __int16     EventMask ;
    //    long		EventMMT[NUM_EVENT] ;
    char	    ppFuncName[ NUM_EVENT ][ 32 ];
    DWORD       ConvOff ;
    DWORD       ScriptOff ;
    } SSC_FILE_HEADER ;
    */
    for (short nI = 0; nI < NUM_EVENT; nI++) {
        if (FileHeader.EventMask & (1 << nI)) {
            // 이벤트 함수 이름.
            m_EventFUNC[nI].Set(FileHeader.ppFuncName[nI]);
        }
    }

    SSC_CONV_HEADER ConvHeader;
    pFileSystem->Read((void*)&ConvHeader, sizeof(SSC_CONV_HEADER));

    //////////////////////////////////////////////////////////
    // Loading Message
    DWORD* pMsgMMT = new DWORD[ConvHeader.m_iMsgNumber];
    if (pMsgMMT) {
        pFileSystem->Seek(ConvHeader.m_dwMsgStartOff + FileHeader.ConvOff, FILE_POS_SET);
        pFileSystem->Read((void*)pMsgMMT, sizeof(DWORD) * ConvHeader.m_iMsgNumber);
        // CSC_Msg *pMsg=new CSC_Msg[ConvHeader.m_iMsgNumber] ;
        // int iMsgSize ;

        m_iScrMsgCNT = ConvHeader.m_iMsgNumber;
        m_pScrMSG = new tagSCRIPTMSG[m_iScrMsgCNT];

        for (int i = 0; i < ConvHeader.m_iMsgNumber; i++) {
            pFileSystem->Seek(FileHeader.ConvOff + ConvHeader.m_dwMsgStartOff + pMsgMMT[i],
                FILE_POS_SET);

            SSC_msg tmpMsg;
            pFileSystem->Read((void*)&tmpMsg, sizeof(SSC_msg));
            // pFileSystem->Read( (void *)&tmpMsg, sizeof(SSC_msg)-sizeof(char *) ) ;

            // iMsgSize=sizeof(SSC_msg)-sizeof(char *)+tmpMsg.iLengthOfString ;
            // SSC_msg *pMsg = (SSC_msg *)new char[iMsgSize+1] ;
            // _ASSERT( pMsg!=NULL) ;

            // pFileSystem->Seek( FileHeader.ConvOff+ConvHeader.m_dwMsgStartOff+pMsgMMT[i],
            // FILE_POS_SET ) ; pFileSystem->Read( (void *)pMsg, iMsgSize ) ;

            m_pScrMSG[i].m_CheckFunc.Set(tmpMsg.szFunc1);
            m_pScrMSG[i].m_ClickFunc.Set(tmpMsg.szFunc2);
            /*
            m_pScrMSG[ i ].m_CheckFunc.Set( pMsg->szFunc1 );
            m_pScrMSG[ i ].m_ClickFunc.Set( pMsg->szFunc2 );
            */

            const char* szString = g_LngTBL.GetEventString(tmpMsg.iStrID);
            LogString(LOG_DEBUG_, "MSG %d : %s \n", i, szString /*pMsg->szString*/);
            // SAFE_DELETE_ARRAY( pMsg );
        }
        SAFE_DELETE_ARRAY(pMsgMMT);
    }

    DWORD* pMenuCollMMT = new DWORD[ConvHeader.m_MenuNumber];
    if (pMenuCollMMT) {
        pFileSystem->Seek(FileHeader.ConvOff + ConvHeader.m_dwMenuStartOff, FILE_POS_SET);
        pFileSystem->Read((void*)pMenuCollMMT, ConvHeader.m_MenuNumber * sizeof(DWORD));

        m_iScrDataCNT = ConvHeader.m_MenuNumber;
        m_pScrDATA = new tagSCRIPTDATA[m_iScrDataCNT];
        for (int i = 0; i < ConvHeader.m_MenuNumber; i++) {
            m_pScrDATA[i].m_iScrItemCNT = 0;
            m_pScrDATA[i].m_pScrITEM = NULL;
            SSC_MENU_COLL tmpMenuColl, *pMenuColl;

            pFileSystem->Seek(FileHeader.ConvOff + ConvHeader.m_dwMenuStartOff + pMenuCollMMT[i],
                FILE_POS_SET);
            pFileSystem->Read((void*)&tmpMenuColl, sizeof(SSC_MENU_COLL) - sizeof(DWORD*));

            LogString(LOG_DEBUG_, "MENU %d ,  Alloc: %d \n", i, tmpMenuColl.m_iLength);
            pMenuColl = (SSC_MENU_COLL*)new char[tmpMenuColl.m_iLength];

            pFileSystem->Seek(FileHeader.ConvOff + ConvHeader.m_dwMenuStartOff + pMenuCollMMT[i],
                FILE_POS_SET);
            pFileSystem->Read((void*)pMenuColl, tmpMenuColl.m_iLength);

            Decode((char*)pMenuColl + sizeof(int) * 2,
                pMenuColl->m_iLength - sizeof(int) * 2,
                pMenuColl->m_iNumSubMenu,
                pMenuColl->m_iLength);

            if (pMenuColl) {
                m_pScrDATA[i].m_iScrItemCNT = pMenuColl->m_iNumSubMenu;
                m_pScrDATA[i].m_pScrITEM = new tagSCRIPTITEM[m_pScrDATA[i].m_iScrItemCNT];
                for (int j = 0; j < pMenuColl->m_iNumSubMenu; j++) {
                    LogString(LOG_DEBUG_, "SubMMT %d => %d ", j, pMenuColl->m_SubMenuMMT[j]);

                    SSC_MenuItem* pMenuItem =
                        (SSC_MenuItem*)((char*)pMenuColl + pMenuColl->m_SubMenuMMT[j]);
                    _ASSERT(pMenuItem->iType < 10);

                    m_pScrDATA[i].m_pScrITEM[j].m_iType = pMenuItem->iType;
                    m_pScrDATA[i].m_pScrITEM[j].m_lChildDataIDX = pMenuItem->dwValue;

                    m_pScrDATA[i].m_pScrITEM[j].m_CheckFunc.Set(pMenuItem->szFunc1);
                    m_pScrDATA[i].m_pScrITEM[j].m_ClickFunc.Set(pMenuItem->szFunc2);

                    const char* szMSG = g_LngTBL.GetEventString(pMenuItem->iStrID);
                    m_pScrDATA[i].m_pScrITEM[j].m_Message.Set((char*)szMSG);

                    /*
                    LogString (LOG_DEBUG_, "	SubMENU %d, Type:%d, Child:%d, %d CheckFunc:%s, %d
                    ClickFunc:%s : %s \n", j, m_pScrDATA[ i ].m_pScrITEM[ j ].m_iType, m_pScrDATA[ i
                    ].m_pScrITEM[ j ].m_lChildDataIDX, m_pScrDATA[ i ].m_pScrITEM[ j
                    ].m_CheckFunc.BuffLength (), m_pScrDATA[ i ].m_pScrITEM[ j ].m_CheckFunc.Get(),
                        m_pScrDATA[ i ].m_pScrITEM[ j ].m_ClickFunc.BuffLength (),
                        m_pScrDATA[ i ].m_pScrITEM[ j ].m_ClickFunc.Get(),
                        m_pScrDATA[ i ].m_pScrITEM[ j ].m_Message.Get ());
                    */
                }
            }

            SAFE_DELETE_ARRAY(pMenuColl);
        }
        SAFE_DELETE_ARRAY(pMenuCollMMT);
    }

    ////////////////////////////////////////////////////////////////////////
    // Loading Script Data
    pFileSystem->Seek(FileHeader.ScriptOff, FILE_POS_SET);
    pFileSystem->Read(&m_iLuaDataLEN, sizeof(int));
    m_pLuaDATA = new char[m_iLuaDataLEN];

    pFileSystem->Read(m_pLuaDATA, m_iLuaDataLEN);
    Decode(m_pLuaDATA, m_iLuaDataLEN, m_iLuaDataLEN, lFileSize); //_tell(hf) + 1 + m_iLuaDataLEN) ;

    ////////////////////////////////////////////////////////////////////////
    // Optional quest-editor appendix: [u32 'QEX1'][i32 len][XOR'd Lua source].
    // Sits after the retail Lua blob; retail files simply end here.
    long lAppendixOff = (long)FileHeader.ScriptOff + (long)sizeof(int) + m_iLuaDataLEN;
    if (lAppendixOff + (long)(sizeof(DWORD) + sizeof(int)) <= lFileSize) {
        DWORD dwMagic = 0;
        pFileSystem->Seek(lAppendixOff, FILE_POS_SET);
        pFileSystem->Read(&dwMagic, sizeof(DWORD));
        if (dwMagic == QEX_APPENDIX_MAGIC) {
            pFileSystem->Read(&m_iLuaAppendixLEN, sizeof(int));
            if (m_iLuaAppendixLEN > 0
                && lAppendixOff + (long)(sizeof(DWORD) + sizeof(int)) + m_iLuaAppendixLEN
                    <= lFileSize) {
                m_pLuaAppendixDATA = new char[m_iLuaAppendixLEN];
                pFileSystem->Read(m_pLuaAppendixDATA, m_iLuaAppendixLEN);
                Decode(m_pLuaAppendixDATA, m_iLuaAppendixLEN, m_iLuaAppendixLEN, lFileSize);
            } else {
                m_iLuaAppendixLEN = 0;
            }
        }
    }

    pFileSystem->CloseFile();
    (CVFSManager::GetSingleton()).ReturnToManager(pFileSystem);

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#else
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////

bool
CEvent::Load(char* szFileName) {
    int hf /*iC*/;

    if ((hf = open(szFileName, O_RDONLY | O_BINARY)) < 0) {
        // g_CApp.ErrorBOX( szFileCantOpen , "Warning" , MB_OK) ;
        return false;
    }
    long lFileSize;
    lseek(hf, 0L, SEEK_END);
    lFileSize = _tell(hf);
    lseek(hf, 0L, SEEK_SET);

    this->m_HashKEY = CStr::GetHASH(szFileName);

    SSC_FILE_HEADER FileHeader;
    SCRREAD(hf, (void*)&FileHeader, sizeof(SSC_FILE_HEADER));
    /*
        typedef struct tag_SSC_FILE_HEADER
        {
            __int16     EventMask ;
        //    long		EventMMT[NUM_EVENT] ;
            char	    ppFuncName[ NUM_EVENT ][ 32 ];
            DWORD       ConvOff ;
            DWORD       ScriptOff ;
        } SSC_FILE_HEADER ;
    */
    for (short nI = 0; nI < NUM_EVENT; nI++) {
        if (FileHeader.EventMask & (1 << nI)) {
            // 이벤트 함수 이름.
            m_EventFUNC[nI].Set(FileHeader.ppFuncName[nI]);
        }
    }

    SSC_CONV_HEADER ConvHeader;
    SCRREAD(hf, (void*)&ConvHeader, sizeof(SSC_CONV_HEADER));

    //////////////////////////////////////////////////////////
    // Loading Message
    DWORD* pMsgMMT = new DWORD[ConvHeader.m_iMsgNumber];
    if (pMsgMMT) {
        lseek(hf, ConvHeader.m_dwMsgStartOff + FileHeader.ConvOff, SEEK_SET);
        SCRREAD(hf, (void*)pMsgMMT, sizeof(DWORD) * ConvHeader.m_iMsgNumber);
        // CSC_Msg *pMsg=new CSC_Msg[ConvHeader.m_iMsgNumber] ;
        int iMsgSize;

        m_iScrMsgCNT = ConvHeader.m_iMsgNumber;
        m_pScrMSG = new tagSCRIPTMSG[m_iScrMsgCNT];

        for (int i = 0; i < ConvHeader.m_iMsgNumber; i++) {
            lseek(hf, FileHeader.ConvOff + ConvHeader.m_dwMsgStartOff + pMsgMMT[i], SEEK_SET);

            SSC_msg tmpMsg;
            SCRREAD(hf, (void*)&tmpMsg, sizeof(SSC_msg) /*-sizeof(char *)*/);

            /*
            iMsgSize=sizeof(SSC_msg)-sizeof(char *)+tmpMsg.iLengthOfString ;
            SSC_msg *pMsg = (SSC_msg *)new char[iMsgSize+1] ;
            _ASSERT( pMsg!=NULL) ;
            */

            /*
            lseek(hf,FileHeader.ConvOff+ConvHeader.m_dwMsgStartOff+pMsgMMT[i],SEEK_SET) ;
            SCRREAD(hf,(void *)pMsg,iMsgSize) ;
            */

            m_pScrMSG[i].m_CheckFunc.Set(tmpMsg.szFunc1);
            m_pScrMSG[i].m_ClickFunc.Set(tmpMsg.szFunc2);

            const char* szMSG = g_LngTBL.GetEventString(tmpMsg.iStrID);
            LogString(LOG_DEBUG_, "MSG %d : %s \n", i, szMSG /*pMsg->szString*/);
            // SAFE_DELETE_ARRAY( pMsg );
        }
        SAFE_DELETE_ARRAY(pMsgMMT);
    }

    DWORD* pMenuCollMMT = new DWORD[ConvHeader.m_MenuNumber];
    if (pMenuCollMMT) {
        lseek(hf, FileHeader.ConvOff + ConvHeader.m_dwMenuStartOff, SEEK_SET);
        SCRREAD(hf, (void*)pMenuCollMMT, ConvHeader.m_MenuNumber * sizeof(DWORD));

        m_iScrDataCNT = ConvHeader.m_MenuNumber;
        m_pScrDATA = new tagSCRIPTDATA[m_iScrDataCNT];
        for (int i = 0; i < ConvHeader.m_MenuNumber; i++) {
            m_pScrDATA[i].m_iScrItemCNT = 0;
            m_pScrDATA[i].m_pScrITEM = NULL;
            SSC_MENU_COLL tmpMenuColl, *pMenuColl;

            lseek(hf, FileHeader.ConvOff + ConvHeader.m_dwMenuStartOff + pMenuCollMMT[i], SEEK_SET);
            SCRREAD(hf, (void*)&tmpMenuColl, sizeof(SSC_MENU_COLL) - sizeof(DWORD*));

            LogString(LOG_DEBUG_, "MENU %d ,  Alloc: %d \n", i, tmpMenuColl.m_iLength);
            pMenuColl = (SSC_MENU_COLL*)new char[tmpMenuColl.m_iLength];

            lseek(hf, FileHeader.ConvOff + ConvHeader.m_dwMenuStartOff + pMenuCollMMT[i], SEEK_SET);
            SCRREAD(hf, (void*)pMenuColl, tmpMenuColl.m_iLength);

            Decode((char*)pMenuColl + sizeof(int) * 2,
                pMenuColl->m_iLength - sizeof(int) * 2,
                pMenuColl->m_iNumSubMenu,
                pMenuColl->m_iLength);

            if (pMenuColl) {
                m_pScrDATA[i].m_iScrItemCNT = pMenuColl->m_iNumSubMenu;
                m_pScrDATA[i].m_pScrITEM = new tagSCRIPTITEM[m_pScrDATA[i].m_iScrItemCNT];
                for (int j = 0; j < pMenuColl->m_iNumSubMenu; j++) {
                    LogString(LOG_DEBUG_, "SubMMT %d => %d ", j, pMenuColl->m_SubMenuMMT[j]);

                    SSC_MenuItem* pMenuItem =
                        (SSC_MenuItem*)((char*)pMenuColl + pMenuColl->m_SubMenuMMT[j]);
                    _ASSERT(pMenuItem->iType < 10);

                    m_pScrDATA[i].m_pScrITEM[j].m_iType = pMenuItem->iType;
                    m_pScrDATA[i].m_pScrITEM[j].m_lChildDataIDX = pMenuItem->dwValue;

                    m_pScrDATA[i].m_pScrITEM[j].m_CheckFunc.Set(pMenuItem->szFunc1);
                    m_pScrDATA[i].m_pScrITEM[j].m_ClickFunc.Set(pMenuItem->szFunc2);

                    const char* szMSG = g_LngTBL.GetEventString(pMenuItem->iStrID);
                    m_pScrDATA[i].m_pScrITEM[j].m_Message.Set(szMSG /* pMenuItem->szString */);

                    LogString(LOG_DEBUG_,
                        "	SubMENU %d, Type:%d, Child:%d, %d CheckFunc:%s, %d ClickFunc:%s : %s "
                        "\n",
                        j,
                        m_pScrDATA[i].m_pScrITEM[j].m_iType,
                        m_pScrDATA[i].m_pScrITEM[j].m_lChildDataIDX,
                        m_pScrDATA[i].m_pScrITEM[j].m_CheckFunc.Length(),
                        m_pScrDATA[i].m_pScrITEM[j].m_CheckFunc.Get(),
                        m_pScrDATA[i].m_pScrITEM[j].m_ClickFunc.Length(),
                        m_pScrDATA[i].m_pScrITEM[j].m_ClickFunc.Get(),
                        m_pScrDATA[i].m_pScrITEM[j].m_Message.Get());
                }
            }

            SAFE_DELETE_ARRAY(pMenuColl);
        }
        SAFE_DELETE_ARRAY(pMenuCollMMT);
    }

    ////////////////////////////////////////////////////////////////////////
    // Loading Script Data
    lseek(hf, FileHeader.ScriptOff, SEEK_SET);
    SCRREAD(hf, &m_iLuaDataLEN, sizeof(int));
    m_pLuaDATA = new char[m_iLuaDataLEN];
    // m_pLuaDATA[ m_iLuaDataLEN ] =0;
    SCRREAD(hf, m_pLuaDATA, m_iLuaDataLEN);
    Decode(m_pLuaDATA,
        m_iLuaDataLEN,
        m_iLuaDataLEN,
        lFileSize); //_tell(hf) + 1 + m_iLuaDataLEN) ;
    #ifdef _DEBUG
                    // m_pLuaDATA 길이가 길어서 -_-; LogString (LOG_DEBUG_, "%s \n", m_pLuaDATA);
                    //::OutputDebugString( m_pLuaDATA );
    #endif

    close(hf);
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#endif
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
classDLLNODE<tagEventITEM>*
CEvent::Add_ClickITEM(tagSCRIPTITEM* pScrITEM) {
    tagEventITEM ClickITEM;
    classDLLNODE<tagEventITEM>* pNode;

    ClickITEM.m_pEVENT = this;
    ClickITEM.m_pScrITEM = pScrITEM;

    pNode = m_EventITEM.AllocNAppend(ClickITEM);

    return pNode;
}
void
CEvent::Del_ClickITEMS(void) {
    return;

    classDLLNODE<tagEventITEM>* pNode;

    pNode = m_EventITEM.GetHeadNode();
    while (pNode) {
        LogString(LOG_DEBUG_, "del click items : %d \n", pNode->DATA.m_pScrITEM->m_lChildDataIDX);
        m_EventITEM.DeleteNFree(pNode);
        pNode = m_EventITEM.GetHeadNode();
    }
}

char*
CEvent::ParseMESSAGE(char* szMSG) {
    if (szMSG == NULL)
        return NULL;

    char *pBegin = NULL, *pEnd = NULL;

    m_TempBuffer[0] = '\0';

    if (szMSG[0] == '<') {
        pEnd = CStr::GetTokenFirst(szMSG, "<>");
        pBegin = "";
    } else
        pBegin = CStr::GetTokenFirst(szMSG, "<>");

    while (pBegin) {
        strcat(m_TempBuffer, pBegin);

        if (!pEnd)
            pEnd = CStr::GetTokenNext("<>");

        if (!pEnd)
            break;

        if (strcmp(pEnd, "WORLD_RATE") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getWorldRate());
        } else if (strcmp(pEnd, "TOWN_RATE") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getTownRate());
        } else if (strcmp(pEnd, "USER_MONEY") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_checkUserMoney());
        } else if (strcmp(pEnd, "ITEM_RATE0") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(0));
        } else if (strcmp(pEnd, "ITEM_RATE1") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(1));
        } else if (strcmp(pEnd, "ITEM_RATE2") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(2));
        } else if (strcmp(pEnd, "ITEM_RATE3") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(3));
        } else if (strcmp(pEnd, "ITEM_RATE4") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(4));
        } else if (strcmp(pEnd, "ITEM_RATE5") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(5));
        } else if (strcmp(pEnd, "ITEM_RATE6") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(6));
        } else if (strcmp(pEnd, "ITEM_RATE7") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(7));
        } else if (strcmp(pEnd, "ITEM_RATE8") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(8));
        } else if (strcmp(pEnd, "ITEM_RATE9") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_getItemRate(9));
        }

        else if (strcmp(pEnd, "NAME") == 0) {
            sprintf(m_TempBuffer, "%s%s", m_TempBuffer, g_pAVATAR->Get_NAME());
        } else if (strcmp(pEnd, "SEX") == 0) {
            sprintf(m_TempBuffer, "%s%s", m_TempBuffer, g_pAVATAR->Get_SEX() == 1 ? "여" : "남");
        } else if (strcmp(pEnd, "RANK") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_RANK());
        }

        else if (strcmp(pEnd, "UNION") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_UNION());
        } else if (strcmp(pEnd, "FAME") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_FAME());
        } else if (strcmp(pEnd, "JOB") == 0) {
            sprintf(m_TempBuffer,
                "%s%s",
                m_TempBuffer,
                CStringManager::GetSingleton().GetJobName(g_pAVATAR->Get_JOB()));
        } else if (strcmp(pEnd, "EXP") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_EXP());
        } else if (strcmp(pEnd, "POINT") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_BonusPOINT());
        } else if (strcmp(pEnd, "STR") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_STR());
        } else if (strcmp(pEnd, "INT") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_INT());
        } else if (strcmp(pEnd, "DEX") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_DEX());
        } else if (strcmp(pEnd, "CON") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_CON());
        } else if (strcmp(pEnd, "MAXHP") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_MaxHP());
        } else if (strcmp(pEnd, "MAXWEIGHT") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_MaxWEIGHT());
        } else if (strcmp(pEnd, "MAXMP") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->Get_MaxMP());
        } else if (strcmp(pEnd, "MONEY") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, GF_checkUserMoney());
        } else if (strcmp(pEnd, "LEVEL") == 0) {
            sprintf(m_TempBuffer, "%s%d", m_TempBuffer, g_pAVATAR->GetCur_LEVEL());
        } else if (strcmp(pEnd, "REVIVE_ZONE") == 0) {
            sprintf(m_TempBuffer, "%s%s", m_TempBuffer, GF_getReviveZoneName());
        }

        // else if(strcmp(pEnd, "") == 0)			{ sprintf (m_TempBuffer, "%s%d", m_TempBuffer,
        // 0);
        // }

        pBegin = CStr::GetTokenNext("<");
        pEnd = NULL;
    }

    _ASSERT(strlen(m_TempBuffer) < MAX_CONV_STR_LEN);

    return m_TempBuffer;
}

short
CEvent::Conversation(int iMenuIDX) {
    if (iMenuIDX < 0)
        return 0;

    short nValiedCNT = 0;
    char *fpCheck = NULL, *szMessage = NULL;

    for (short nI = 0; nI < m_pScrDATA[iMenuIDX].m_iScrItemCNT; nI++) {
        /// 각 대사 라인당 할당된 체크 스크립트
        fpCheck = m_pScrDATA[iMenuIDX].m_pScrITEM[nI].m_CheckFunc.Get();

        if (fpCheck) {
            int iResult =
                lua_CallIntFUNC(m_pLUA->m_pState, fpCheck, ZZ_PARAM_INT, this, ZZ_PARAM_END);

            if (iResult < 1) {
                LogString(LOG_DEBUG_,
                    "SKIP MENU %d / %d :: [%s], [%s] \n",
                    iMenuIDX,
                    nI,
                    m_pScrDATA[iMenuIDX].m_pScrITEM[nI].m_Message.Get(),
                    fpCheck);
                continue;
            }
        } // else 실행할 함수가 없으면 항상 출력...

        LogString(LOG_DEBUG_,
            "%d MENU %d / %d :: [%s], [%s] \n",
            m_pScrDATA[iMenuIDX].m_pScrITEM[nI].m_iType,
            iMenuIDX,
            nI,
            m_pScrDATA[iMenuIDX].m_pScrITEM[nI].m_Message.Get(),
            fpCheck);

        switch (m_pScrDATA[iMenuIDX].m_pScrITEM[nI].m_iType) {
            case SC_MSG_CLOSE:
            case SC_MSG_PLAYERSELECT:
            case SC_MSG_JUMPSELECT: {
                classDLLNODE<tagEventITEM>* pClickItemNODE;
                pClickItemNODE = Add_ClickITEM(&m_pScrDATA[iMenuIDX].m_pScrITEM[nI]);

                szMessage = this->ParseMESSAGE(m_pScrDATA[iMenuIDX].m_pScrITEM[nI].m_Message.Get());
                if (szMessage == NULL)
                    break;

                g_itMGR.QueryDLG_AppendExam(szMessage, // char* szExam,
                    (int)pClickItemNODE, // int iEventID,
                    CEvent::Click_ITEM, // void (*fpExamEvent)(int iEventID) );
                    GetQueryType());
                break;
            }

            case SC_MSG_NPCSAY:
            case SC_MSG_NEXTMSG:
                Del_ClickITEMS();

                g_itMGR.CloseQueryDlg();

                szMessage = this->ParseMESSAGE(m_pScrDATA[iMenuIDX].m_pScrITEM[nI].m_Message.Get());

                if (szMessage == NULL)
                    break;

                g_itMGR.OpenQueryDLG(300,
                    100,
                    szMessage, // char* szQuery,
                    strlen(szMessage),
                    // m_pScrDATA[ iMenuIDX ].m_pScrITEM[ nI ].m_Message.BuffLength(),
                    (int)this,
                    CEvent::Click_CLOSE,
                    m_iOwnerObjIDX,
                    m_iEventDlgType);

                Conversation(m_pScrDATA[iMenuIDX].m_pScrITEM[nI].m_lChildDataIDX);
                break;
        }

        nValiedCNT++;
    }

    return nValiedCNT;
}

//-------------------------------------------------------------------------------------------------
// Quest-editor check-function naming: replace-mode giver .CONs use bare
// CHK_accept / CHK_complete, append-mode (QEX1) options are namespaced
// QE<questSN>_CHK_accept / _CHK_complete. Retail conversations use neither
// (verified over all retail .CONs), so a name match means "quest option".
static bool
IsQuestEditorCheckFunc(const char* szName, const char* szSuffix) {
    if (szName == NULL || szName[0] == '\0')
        return false;

    const char* pBase = szName;
    if (pBase[0] == 'Q' && pBase[1] == 'E' && isdigit((unsigned char)pBase[2])) {
        const char* p = pBase + 2;
        while (isdigit((unsigned char)*p))
            p++;
        if (*p != '_')
            return false;
        pBase = p + 1;
    }

    return strcmp(pBase, szSuffix) == 0;
}

static void
AddUniqueFuncName(std::vector<std::string>& List, const char* szName) {
    for (size_t i = 0; i < List.size(); i++)
        if (List[i] == szName)
            return;
    List.push_back(szName);
}

void
CEvent::ScanQuestCheckFuncs() {
    m_bQuestFuncsScanned = true;

    for (int iMenu = 0; iMenu < m_iScrDataCNT; iMenu++) {
        for (short nI = 0; nI < m_pScrDATA[iMenu].m_iScrItemCNT; nI++) {
            const char* szCheck = m_pScrDATA[iMenu].m_pScrITEM[nI].m_CheckFunc.Get();
            if (szCheck == NULL)
                continue;

            if (IsQuestEditorCheckFunc(szCheck, "CHK_accept"))
                AddUniqueFuncName(m_QuestAcceptFuncs, szCheck);
            else if (IsQuestEditorCheckFunc(szCheck, "CHK_complete"))
                AddUniqueFuncName(m_QuestCompleteFuncs, szCheck);
        }
    }

    this->ScanQuestTriggerRefs();
}

// Printable-ASCII runs in a Lua blob. Lua 4 bytecode stores string constants
// with binary length/type bytes between them, so constant boundaries always
// break a run — trigger names come out whole (validated over all retail .CONs).
static void
HarvestLuaStrings(const char* pData, int iLen, std::vector<std::string>& Out) {
    int iStart = -1;
    for (int i = 0; i <= iLen; i++) {
        char c = (i < iLen) ? pData[i] : '\0';
        if (c >= 0x21 && c <= 0x7e) {
            if (iStart < 0)
                iStart = i;
            continue;
        }
        if (iStart >= 0) {
            int iRun = i - iStart;
            if (iRun >= 2 && iRun <= 32)
                Out.push_back(std::string(pData + iStart, iRun));
            iStart = -1;
        }
    }
}

// Classify a QSD trigger (with its check_next fail-over chain, same walk as
// CQuestDATA::CheckQUEST) by its reward composition:
//  - accept: adds a quest (REWD_000 op 1)
//  - turn-in: requires a registered quest (COND_000) and either advances it
//    (REWD_000 op 2 swap / op 3 reset-start) or deletes it (op 0) while
//    granting something (item give / ability / calc reward / hp-mp / skill).
//    Delete-only chains are quest-abandon options and stay unclassified.
static void
ClassifyQuestTrigger(CQuestTRIGGER* pEntry, bool& bAccept, bool& bTurnIn, int& iAddQuestSN) {
    bAccept = bTurnIn = false;
    iAddQuestSN = -1;

    bool bHasQuestCond = false, bGrant = false, bDelete = false, bAdvance = false;

    int iGuard = 0;
    for (CQuestTRIGGER* pT = pEntry; pT && iGuard < 64; iGuard++) {
        for (unsigned int uiC = 0; uiC < pT->GetCondCNT(); uiC++) {
            uniQstENTITY* pCOND = pT->GetCOND(uiC);
            if (pCOND && (pCOND->iType & 0xffff) == 0)
                bHasQuestCond = true;
        }

        for (unsigned int uiR = 0; uiR < pT->GetRewdCNT(); uiR++) {
            uniQstENTITY* pREWD = pT->GetREWD(uiR);
            if (pREWD == NULL)
                continue;

            switch (pREWD->iType & 0xffff) {
                case 0:
                    switch (pREWD->m_Rewd000.btOp) {
                        case 0:
                            bDelete = true;
                            break;
                        case 1:
                            bAccept = true;
                            iAddQuestSN = pREWD->m_Rewd000.iQuestSN;
                            break;
                        case 2:
                        case 3:
                            bAdvance = true;
                            break;
                            // op 4 = select-context only; no state change
                    }
                    break;
                case 1: // item give/remove
                    if (pREWD->m_Rewd001.btOp == 1)
                        bGrant = true;
                    break;
                case 3: // ability
                case 5: // calculated exp/item/zuly reward
                case 6: // hp/mp
                case 14: // skill
                    bGrant = true;
                    break;
            }
        }

        pT = pT->GetCheckNext() ? pT->m_pNextTrigger : NULL;
    }

    bTurnIn = bHasQuestCond && (bAdvance || (bDelete && bGrant));
}

void
CEvent::ScanQuestTriggerRefs() {
    std::vector<std::string> Strings;
    if (m_pLuaDATA && m_iLuaDataLEN > 0)
        HarvestLuaStrings(m_pLuaDATA, m_iLuaDataLEN, Strings);
    if (m_pLuaAppendixDATA && m_iLuaAppendixLEN > 0)
        HarvestLuaStrings(m_pLuaAppendixDATA, m_iLuaAppendixLEN, Strings);

    for (size_t i = 0; i < Strings.size(); i++) {
        CQuestTRIGGER* pTrigger = g_QuestList.GetQuest(::StrToHashKey(Strings[i].c_str()));
        if (pTrigger == NULL)
            continue;

        bool bDup = false;
        for (size_t j = 0; j < m_QuestTriggerRefs.size() && !bDup; j++)
            bDup = (m_QuestTriggerRefs[j].m_Name == Strings[i]);
        if (bDup)
            continue;

        tagQuestTriggerRef Ref;
        Ref.m_Name = Strings[i];
        ClassifyQuestTrigger(pTrigger, Ref.m_bAccept, Ref.m_bTurnIn, Ref.m_iAddQuestSN);
        if (Ref.m_bAccept || Ref.m_bTurnIn)
            m_QuestTriggerRefs.push_back(Ref);
    }
}

// Calls one check function in the throwaway state; 1 if it exists and returned
// >= 1. Existence is verified first so a malformed file cannot pop the
// "function not found" ErrorBOX every refresh tick.
static bool
CallQuestCheckFunc(classLUA& LUA, CEvent* pEvent, const char* szFunc) {
    int iTop = LUA.Stack_GetElementCount();
    LUA.GetGlobal(szFunc);
    bool bIsFunction = LUA.Is_Function(-1) || LUA.Is_CFunction(-1);
    LUA.Stack_SetTop(iTop);

    if (!bIsFunction)
        return false;

    return lua_CallIntFUNC(LUA.m_pState, szFunc, ZZ_PARAM_INT, pEvent, ZZ_PARAM_END) >= 1;
}

// True when the trigger's conditions currently pass for the local avatar.
// bDoReward=false walks conditions only; rewards are skipped, so probing has
// no side effects. COND_013 (NPC binder) is a server-only check and always
// passes on the client, which is exactly right for a per-NPC probe.
static bool
QuestTriggerConditionsPass(const char* szTriggerName) {
    return g_QuestList.CheckQUEST(g_pAVATAR, ::StrToHashKey(szTriggerName), false)
        == QST_RESULT_SUCCESS;
}

short
CEvent::GetQuestSignal() {
    if (g_pAVATAR == NULL)
        return 0;

    if (!m_bQuestFuncsScanned)
        this->ScanQuestCheckFuncs();

    if (m_QuestTriggerRefs.empty() && m_QuestAcceptFuncs.empty()
        && m_QuestCompleteFuncs.empty())
        return 0;

    // Retail/QSD path first — pure condition walks, no Lua involved. Turn-in
    // beats available when an NPC has both.
    for (size_t i = 0; i < m_QuestTriggerRefs.size(); i++)
        if (m_QuestTriggerRefs[i].m_bTurnIn
            && QuestTriggerConditionsPass(m_QuestTriggerRefs[i].m_Name.c_str()))
            return 3;

    bool bAvailable = false;
    for (size_t i = 0; i < m_QuestTriggerRefs.size() && !bAvailable; i++) {
        const tagQuestTriggerRef& Ref = m_QuestTriggerRefs[i];
        if (!Ref.m_bAccept)
            continue;
        // Never re-offer a quest that is already in the log (retail register
        // triggers do not always carry that guard themselves).
        if (Ref.m_iAddQuestSN >= 0
            && g_pAVATAR->Quest_GetRegistered(Ref.m_iAddQuestSN) < QUEST_PER_PLAYER)
            continue;
        bAvailable = QuestTriggerConditionsPass(Ref.m_Name.c_str());
    }

    // Quest-editor Lua check functions (dialog-exact gating; also covers any
    // future custom check logic). Skipped when the QSD path already decided.
    if (!m_QuestAcceptFuncs.empty() || !m_QuestCompleteFuncs.empty()) {
        if (m_pLuaDATA && m_iLuaDataLEN > 0) {
            // Same load order as Start(), but into a local state so an open
            // dialog's m_pLUA (and its pending click items) is never disturbed.
            classLUA LUA;
            bool bLoaded = (LUA.Do_Buffer(m_pLuaDATA, m_iLuaDataLEN) == 0);
            if (bLoaded && m_pLuaAppendixDATA && m_iLuaAppendixLEN > 0)
                bLoaded = (LUA.Do_Buffer(m_pLuaAppendixDATA, m_iLuaAppendixLEN) == 0);

            if (bLoaded) {
                QF_Init(LUA.m_pState);

                for (size_t i = 0; i < m_QuestCompleteFuncs.size(); i++)
                    if (CallQuestCheckFunc(LUA, this, m_QuestCompleteFuncs[i].c_str()))
                        return 3;

                if (!bAvailable)
                    for (size_t i = 0; i < m_QuestAcceptFuncs.size() && !bAvailable; i++)
                        bAvailable = CallQuestCheckFunc(LUA, this, m_QuestAcceptFuncs[i].c_str());
            }
        }
    }

    return bAvailable ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------
bool
CEvent::Start(short nEventIDX) {
    int iResult;

    SAFE_DELETE(m_pLUA);

    m_pLUA = new classLUA;

    iResult = m_pLUA->Do_Buffer(m_pLuaDATA, m_iLuaDataLEN);
    //	iResult = m_pLUA->Do_String( m_pLuaDATA );
    // iResult = m_pLUA->Do_File( "C:\\solutions\\bctools\\scrdata\\testbin.lub" );
    switch (iResult) {
        // error codes for lua_do*
        case LUA_ERRRUN:
        case LUA_ERRFILE: // 2
        case LUA_ERRSYNTAX: // 3
        case LUA_ERRMEM:
        case LUA_ERRERR:
            LogString(LOG_DEBUG_, "Script File ERROR[ %d ] %s \n", iResult, m_pLuaDATA);
            SAFE_DELETE(m_pLUA);
            return false;
    }

    // Quest-editor appendix chunk: extra Lua source executed into the same state so
    // its globals coexist with the retail blob's (which may be bytecode).
    if (m_pLuaAppendixDATA && m_iLuaAppendixLEN > 0) {
        iResult = m_pLUA->Do_Buffer(m_pLuaAppendixDATA, m_iLuaAppendixLEN);
        switch (iResult) {
            case LUA_ERRRUN:
            case LUA_ERRFILE:
            case LUA_ERRSYNTAX:
            case LUA_ERRMEM:
            case LUA_ERRERR:
                LogString(LOG_DEBUG_,
                    "Appendix Script ERROR[ %d ] %s \n",
                    iResult,
                    m_pLuaAppendixDATA);
                SAFE_DELETE(m_pLUA);
                return false;
        }
    }

    QF_Init(m_pLUA->m_pState);

    // 각 상황에 맞는 이벤트 처리...
    if (nEventIDX >= 0 && nEventIDX < NUM_EVENT) {
        iResult = lua_CallIntFUNC(m_pLUA->m_pState,
            m_EventFUNC[nEventIDX].Get(),
            ZZ_PARAM_INT,
            this,
            ZZ_PARAM_END);
        if (iResult < 1) {
            LogString(LOG_DEBUG_,
                "SKIP EventIdxFUNC [%d: %s return %d], [%s] \n",
                nEventIDX,
                m_EventFUNC[nEventIDX].Get(),
                iResult);
            return false;
        }
        return true;
    }

    // 대화 이벤트 처리...
    char* szCheckFunc = m_pScrMSG[0].m_CheckFunc.Get();
    if (szCheckFunc) {
        iResult = lua_CallIntFUNC(m_pLUA->m_pState, szCheckFunc, ZZ_PARAM_INT, this, ZZ_PARAM_END);
        if (iResult < 1) {
            LogString(LOG_DEBUG_, "SKIP Event [%s return %d], [%s] \n", szCheckFunc, iResult);
            return false;
        }
    }

    if (!Conversation(0)) {
        // 조건에 맞는 출력할것들이 없다...
        return false;
    }

    return true;
}
bool
CEvent::End() {
    char* fpCheck = m_pScrMSG[0].m_ClickFunc.Get();
    if (fpCheck) {
        int iResult = lua_CallIntFUNC(m_pLUA->m_pState, fpCheck, ZZ_PARAM_INT, this, ZZ_PARAM_END);
        if (iResult >= 1) {
            return true;
        }

        LogString(LOG_DEBUG_, "End Event [%s return %d], [%s] \n", fpCheck, iResult);
    }

    /*
        // Dance TEST !!!
        CObjCHAR *pObjCHAR = g_pObjMGR->Get_CharOBJ( m_iOwnerObjIDX, true );
        if ( pObjCHAR ) {
            int iMotion = GF_GetMotionUseFile ( "3DDATA\\MOTION\\NPC\\magician\\magician_work.ZMO"
       ); if ( iMotion ) { GF_SetMotion ( m_iOwnerObjIDX, iMotion ); pObjCHAR->Set_STATE(
       CS_NEXT_STOP );
            }
        }
    */
    return false;
}

//-------------------------------------------------------------------------------------------------
void
CEvent::Click_ITEM(int iHandle) {
    if (NULL == iHandle)
        return;

    classDLLNODE<tagEventITEM>* pClickItemNODE = (classDLLNODE<tagEventITEM>*)iHandle;
    char* fpClick = pClickItemNODE->DATA.m_pScrITEM->m_ClickFunc.Get();

    LogString(LOG_DEBUG_,
        " ClickMENU : %d  call: %s \n",
        pClickItemNODE->DATA.m_pScrITEM->m_lChildDataIDX,
        fpClick);

    if (fpClick) {
        int iResult = lua_CallIntFUNC(pClickItemNODE->DATA.m_pEVENT->m_pLUA->m_pState,
            fpClick,
            ZZ_PARAM_INT,
            pClickItemNODE->DATA.m_pEVENT,
            ZZ_PARAM_END);
        if (iResult < 1)
            LogString(LOG_DEBUG_, "click %s call failed \n", fpClick);
    }

    if (!pClickItemNODE->DATA.m_pEVENT->Conversation(
            pClickItemNODE->DATA.m_pScrITEM->m_lChildDataIDX)) {
        if (pClickItemNODE->DATA.m_pEVENT->End()) {
            // restart from !!!
            if (pClickItemNODE->DATA.m_pEVENT->Conversation(0))
                return;
        }

        // g_pEventLIST->Del_EVENT( pClickItemNODE->DATA.m_pEVENT->m_HashKEY );

        g_itMGR.CloseQueryDlg();
    }
}

//-------------------------------------------------------------------------------------------------
void
CEvent::Click_CLOSE(int iHandle) {
    if (NULL == iHandle)
        return;

    //	g_pEventLIST->Del_EVENT( ((CEvent*)iHandle)->m_HashKEY );

    g_itMGR.CloseQueryDlg();
}

//-------------------------------------------------------------------------------------------------
void
CEvent::Decode(char* pData, int iSize, int iValue1, int iValue2) {
    union tag4BYTE {
        BYTE btXOR[4];
        int iValue;
    };
    union {
        int iXOR[2];
        BYTE btXOR[8];
    } KEY_XOR;

    tag4BYTE sTMP;

    KEY_XOR.iXOR[0] = iValue1;
    KEY_XOR.iXOR[1] = iValue2;

    sTMP.iValue = iValue1;
    int iKey = 0;
    for (short nI = 0; nI < 8; nI++) {
        iKey += KEY_XOR.btXOR[nI];
        KEY_XOR.btXOR[nI] = (KEY_XOR.btXOR[nI] + sTMP.btXOR[nI % 4]) & 0x0ff;
    }

    for (int iS = 0; iS < iSize; iS++) {
        iKey &= 0x07;
        // pData[ iS ] ^= KEY_XOR.btXOR[ iKey++ ];
        pData[iS] ^= iValue1 & 1 ? iValue1 : iValue2;
    }
}

//-------------------------------------------------------------------------------------------------
int
CEvent::GetQueryType() {
    int querytype = IT_MGR::QUERYTYPE_NPC;
    switch (m_iEventDlgType) {
        /// NPC 대화창
        case EVENT_DLG_NPC: {
            querytype = IT_MGR::QUERYTYPE_NPC;
        }

        break;
        /// Warp DLG
        case EVENT_DLG_WARP: {
            querytype = IT_MGR::QUERYTYPE_SELECT;
        } break;
        /// Event Object Dlg
        case EVENT_DLG_EVENT_OBJECT: {
            querytype = IT_MGR::QUERYTYPE_ITEM;
        } break;
    }
    return querytype;
}
//-------------------------------------------------------------------------------------------------
