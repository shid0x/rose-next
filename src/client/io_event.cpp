/*
    $Header: /Client/IO_Event.cpp 16    05-01-07 11:32a Jeddli $
*/
#include "stdAFX.h"

#include "rose/io/stb.h"
#include "IO_Event.h"
#include "LUA_Func.h"

#include <set>

//-------------------------------------------------------------------------------------------------
CEventLIST::CEventLIST(): CFileLIST<CEvent*>("EVT") {
    ;
}

bool
CEventLIST::Load_FILE(tagFileDATA<CEvent*>* pDATA) {
    if (pDATA->m_DATA) {
        int kkkk = 0;
        return false;
    }

    pDATA->m_DATA = new CEvent;
    if (pDATA->m_DATA->Load(pDATA->m_FileName.Get())) {
        return true;
    }
    SAFE_DELETE(pDATA->m_DATA);

    return false;
}
void
CEventLIST::Free_FILE(tagFileDATA<CEvent*>* pDATA) {
    SAFE_DELETE(pDATA->m_DATA);
}

//-------------------------------------------------------------------------------------------------
t_HASHKEY
CEventLIST::Add_EVENT(char* szFileName) {
    if ((CVFSManager::GetSingleton()).IsExistFile(szFileName) == false) {
        return 0;
    }

    LogString(LOG_DEBUG_, "Add_EVENT:: %s \n", szFileName);

    return this->Add_FILE(szFileName);
}
void
CEventLIST::Del_EVENT(t_HASHKEY HashKEY) {
    this->Del_FILE(HashKEY);
}

//-------------------------------------------------------------------------------------------------
void
CEventLIST::Clear() {
    this->Free();
}

//-------------------------------------------------------------------------------------------------
bool
CEventLIST::Run_EVENT(int iOwnerObjIDX, char* szQuestFILE, short nEventIDX, int iEventDlgType) {
    t_HASHKEY HashKEY = this->Add_EVENT(szQuestFILE);

    if (HashKEY) {
        CEvent* pEvent;

        pEvent = this->Get_DATAUseKEY(HashKEY);
        if (pEvent == NULL) {
            return false;
        }

        pEvent->SetEventDlgType(iEventDlgType);

        pEvent->m_iOwnerObjIDX = iOwnerObjIDX;
        bool bReturn = pEvent->Start(nEventIDX);

        // if ( !bReturn )	this->Del_EVENT( HashKEY );

        return bReturn;
    }

    return false;
}

//-------------------------------------------------------------------------------------------------
/// @param bool bItemEvent : 아이템에 의해 발생한 이벤트 인가?
//-------------------------------------------------------------------------------------------------
bool
CEventLIST::Run_EVENT(int iOwnerObjIDX, short nQuestIDX, short nEventIDX, int iEventDlgType) {
    return this->Run_EVENT(iOwnerObjIDX, EVENT_FILENAME(nQuestIDX), nEventIDX, iEventDlgType);
}

//-------------------------------------------------------------------------------------------------
short
CEventLIST::GetNpcQuestSignal(int iOwnerObjIDX, short nQuestIDX) {
    if (nQuestIDX <= 0)
        return 0;

    char* szQuestFILE = EVENT_FILENAME(nQuestIDX);
    if (szQuestFILE == NULL || szQuestFILE[0] == '\0')
        return 0;

    t_HASHKEY HashKEY = this->Add_EVENT(szQuestFILE);
    if (!HashKEY)
        return 0;

    CEvent* pEvent = this->Get_DATAUseKEY(HashKEY);
    if (pEvent == NULL)
        return 0;

    // The icon walk runs this NPC's dialog Lua headlessly on every refresh, so a
    // script error in it must not open the "Script running error.." box: it would
    // come straight back 2 s later, for as long as the NPC is in view (reported on
    // the Ferrell Guild manager, 2026-11-05). Quiet, and logged once per file.
    bool bWasQuiet = lua_SetQuietErrors(true);
    short nSignal = pEvent->GetQuestSignal(iOwnerObjIDX);
    lua_SetQuietErrors(bWasQuiet);

    if (const char* szError = lua_TakeQuietError()) {
        static std::set<t_HASHKEY> s_Logged;
        if (s_Logged.insert(HashKEY).second)
            LOG_WARN("Quest icon: script error in {} skipped (logged once per file): {}",
                szQuestFILE,
                szError);
    }

    return nSignal;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
