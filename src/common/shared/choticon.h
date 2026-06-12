#pragma once

#include "nlohmann/json_fwd.hpp"

#define MAX_ICONS_PAGES 8
#define HOT_ICONS_PER_PAGE 8

#define MAX_HOT_ICONS 64

#define QUICKBAR_MAX_HOT_ICONS_PAGES 4
#define QUICKBAR_MAX_HOT_ICONS 32

enum t_HotIconTYPE {
    INV_ICON = 1,
    COMMAND_ICON,
    SKILL_ICON,
    EMOTION_ICON,
    DIALOG_ICON,
    CLANSKILL_ICON,
};

#pragma pack(push, 1)
union tagHotICON {
    struct {
        unsigned short m_cType : 5; // 0~31
        unsigned short m_nSlotNo : 11; // 0~2047
    };
    WORD m_wHotICON;
};

class CHotICONS {
public:
    union {
        tagHotICON m_IconLIST[MAX_HOT_ICONS];
        tagHotICON m_IconPAGE[MAX_ICONS_PAGES][HOT_ICONS_PER_PAGE];
    };
    void Init();
    bool RegHotICON(BYTE btListIDX, tagHotICON sHotICON);
    void DelHotICON(BYTE btListIDX);

#ifndef __SERVER
    void UpdateHotICON();
#endif
};
#pragma pack(pop)

void to_json(nlohmann::json& j, const CHotICONS& s);
void from_json(const nlohmann::json& j, CHotICONS& s);