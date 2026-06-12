#include "stdafx.h"

#include ".\levelupevent.h"
#include "TutorialEventUtility.h"

CLevelUpEvent::CLevelUpEvent(void) {}

CLevelUpEvent::~CLevelUpEvent(void) {}

bool
CLevelUpEvent::Init() {
    bool loaded = CVFSManager::GetSingleton().load_stb(m_TblLevelEvent, LEVELUP_EVENT_STB);
    if (!loaded)
        return false;

    /// Make event table from stb that loaded above.
    for (int i = 1; i < m_TblLevelEvent.row_count; i++) {
        m_LevelUpEventTbl.insert(
            std::make_pair(m_TblLevelEvent.get_int32(i, 0), m_TblLevelEvent.get_int32(i, 1)));
    }

    return true;
}

void
CLevelUpEvent::Release() {
}

void
CLevelUpEvent::CheckLevelEvent(int iLevel) {
    std::map<int, int>::iterator itor = m_LevelUpEventTbl.find(iLevel);

    /// if find level up event
    if (itor != m_LevelUpEventTbl.end()) {
        CTutorialEventUtility::GetSingleton().CreateNotifyEventButton(itor->second);
    }
}

void
CLevelUpEvent::Proc() {}
