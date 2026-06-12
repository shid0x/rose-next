#include "stdAFX.h"

#include "CHotICON.h"
#ifndef __SERVER
    #include "..\\Object.h"
#endif

#include "nlohmann/json.hpp"

using json = nlohmann::json;

void
CHotICONS::Init() {
    ::ZeroMemory(m_IconLIST, MAX_HOT_ICONS * sizeof(tagHotICON));
}

bool
CHotICONS::RegHotICON(BYTE btListIDX, tagHotICON sHotICON) {
    if (btListIDX >= MAX_HOT_ICONS)
        return false;

    m_IconLIST[btListIDX] = sHotICON;
    return true;
}

void
CHotICONS::DelHotICON(BYTE btListIDX) {
    if (btListIDX < MAX_HOT_ICONS)
        m_IconLIST[btListIDX].m_wHotICON = 0;
}

#ifndef __SERVER
/// 만약 핫아이콘이 참조하는 아이템이 사라졌을경우..
void
CHotICONS::UpdateHotICON() {
    for (int i = 0; i < MAX_HOT_ICONS; i++) {
        switch (m_IconLIST[i].m_cType) {
            case INV_ICON: {
                if (g_pAVATAR) {
                    if (m_IconLIST[i].m_nSlotNo < 0
                        || m_IconLIST[i].m_nSlotNo >= INVENTORY_TOTAL_SIZE) {
                        assert(0 && " UpdateHotICON has invalid slot number");
                        break;
                    }

                    /// 참조하는 아이템이 무효하면 지워라..
                    if (g_pAVATAR->m_Inventory.m_ItemLIST[m_IconLIST[i].m_nSlotNo].m_cType == 0) {
                        DelHotICON(i);
                    }
                } else {
                    assert(0 && " CHotICONS::UpdateHotICON has invalid pAvatar");
                }
            } break;
        }
    }
}
#endif

void
to_json(nlohmann::json& j, const CHotICONS& s) {
    j = json::array();
    for (size_t i = 0; i < MAX_HOT_ICONS; ++i) {
        json icon;
        icon["skill_slot"] = s.m_IconLIST[i].m_nSlotNo;
        icon["type"] = s.m_IconLIST[i].m_cType;
        j.push_back(icon);
    }
}

void
from_json(const nlohmann::json& j, CHotICONS& s) {
    if (!j.is_array()) {
        return;
    }

    for (size_t i = 0; i < min(j.size(), MAX_HOT_ICONS); ++i) {
        json icon = j[i];
        if (icon.contains("skill_slot") && icon.contains("type")) {
            s.m_IconLIST[i].m_nSlotNo = icon["skill_slot"];
            s.m_IconLIST[i].m_cType = icon["type"];
        }
    }
}
