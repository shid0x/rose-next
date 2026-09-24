#include "stdafx.h"

#include "RoseRmlBuffBar.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include "..\\Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../GameCommon/Item.h"
#include "../gamecommon/StringManager.h"
#include "../interface/CEnduranceProperty.h"
#include "../interface/io_imageres.h"
#include "../interface/TypeResource.h"

#include "rose/common/log.h"

#include <stdio.h>
#include <stdarg.h>

namespace {

/// The legacy strip flashes an icon through its last 10 s.
const int kExpiringMs = 10000;

/// Equipment below this life ( per mille ) is shown as worn -- the legacy 5%.
const int kWornLife = 50;

/// Space between the status panel and the strip hanging off it, in dp.
const float kAnchorGap = 4.0f;

Rml::String
Printf(const char* pszFormat, ...) {
    char szBuf[128];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf(szBuf, sizeof(szBuf), pszFormat, args);
    va_end(args);
    szBuf[sizeof(szBuf) - 1] = '\0';
    return szBuf;
}

/// Short label for under an icon.
Rml::String
ShortTime(int iMs) {
    const int iSec = (iMs + 999) / 1000;
    if (iSec < 60)
        return Printf("%ds", iSec);
    if (iSec < 3600)
        return Printf("%dm", (iSec + 59) / 60);
    return Printf("%dh", (iSec + 3599) / 3600);
}

/// The legacy tooltip's wording.
Rml::String
LongTime(int iMs) {
    const int iSeconds = (iMs / 1000) % 60;
    const int iMinutes = (iMs / 1000) / 60;
    Rml::String str;
    if (iMinutes == 1)
        str = "1 minute and ";
    else if (iMinutes > 1)
        str = Printf("%d minutes and ", iMinutes);
    str += Printf(iSeconds == 1 ? "%d second remaining." : "%d seconds remaining.", iSeconds);
    return str;
}

/// The goddess blessing lists what it adds on top of any stacked buff of the
/// same kind, as the legacy tooltip did.
Rml::String
GoddessDetail(CEndurancePack& pack, CEnduranceProperty* pEntity) {
    Rose::Common::GoddessEffect& fx = static_cast<CEnduranceGoddess*>(pEntity)->goddess_effect;
    struct Line {
        const char* pszLabel;
        int iValue;
    } lines[] = {
        {"Move Speed", fx.move_speed - pack.GetStateValue(ING_INC_MOV_SPD)},
        {"Attack Damage", fx.attack_power - pack.GetStateValue(ING_INC_APOWER)},
        {"Hit Rate", fx.hit - pack.GetStateValue(ING_INC_HIT)},
        {"Attack Speed", fx.attack_speed - pack.GetStateValue(ING_INC_ATK_SPD)},
        {"Crit", fx.crit - pack.GetStateValue(ING_INC_CRITICAL)},
    };

    Rml::String str;
    for (const Line& line : lines) {
        if (line.iValue <= 0)
            continue;
        if (!str.empty())
            str += "\n";
        str += Printf("%s: +%d", line.pszLabel, line.iValue);
    }
    return str;
}

} // namespace

RoseRmlBuffBar::RoseRmlBuffBar():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_pAnchor(NULL),
    m_fPlacedX(-1.0f),
    m_fPlacedY(-1.0f),
    m_bVisible(false),
    m_bSummon(false),
    m_fSummonWidth(0.0f),
    m_bFuel(false),
    m_fFuelWidth(0.0f) {}

void
RoseRmlBuffBar::RegisterBuffStruct(Rml::DataModelConstructor& constructor) {
    /// Data models in one context share a type register, and registering a
    /// struct twice logs "Struct type already declared". The buff strip and the
    /// target frame both need BuffVM, so register once per register.
    static Rml::DataTypeRegister* s_pRegistered = NULL;
    if (constructor.GetDataTypeRegister() == s_pRegistered)
        return;
    s_pRegistered = constructor.GetDataTypeRegister();

    if (auto buff = constructor.RegisterStruct<BuffVM>()) {
        buff.RegisterMember("src", &BuffVM::src);
        buff.RegisterMember("rect", &BuffVM::rect);
        buff.RegisterMember("time", &BuffVM::time);
        buff.RegisterMember("name", &BuffVM::name);
        buff.RegisterMember("detail", &BuffVM::detail);
        buff.RegisterMember("expiring", &BuffVM::expiring);
    }
    constructor.RegisterArray<std::vector<BuffVM>>();
}

bool
RoseRmlBuffBar::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("buffs");
    if (!constructor)
        return false;

    RegisterBuffStruct(constructor);

    if (auto worn = constructor.RegisterStruct<WornVM>()) {
        worn.RegisterMember("src", &WornVM::src);
        worn.RegisterMember("rect", &WornVM::rect);
        worn.RegisterMember("name", &WornVM::name);
    }
    constructor.RegisterArray<std::vector<WornVM>>();

    constructor.Bind("buffs", &m_Buffs);
    constructor.Bind("worn", &m_Worn);
    constructor.Bind("summon_on", &m_bSummon);
    constructor.Bind("summon_width", &m_fSummonWidth);
    constructor.Bind("summon", &m_strSummon);
    constructor.Bind("summon_label", &m_strSummonLabel);
    constructor.Bind("fuel_on", &m_bFuel);
    constructor.Bind("fuel_width", &m_fFuelWidth);
    constructor.Bind("fuel", &m_strFuel);
    constructor.Bind("fuel_label", &m_strFuelLabel);

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "buffs.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load buff bar document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("buffs");

    LOG_INFO("[rmlui] buff bar document loaded");
    return true;
}

void
RoseRmlBuffBar::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_pAnchor = NULL;
    m_bVisible = false;
}

void
RoseRmlBuffBar::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlBuffBar::CollectBuffs(CObjCHAR* pChar, std::vector<BuffVM>& out) {
    out.clear();
    if (pChar == NULL)
        return;

    CEndurancePack& pack = pChar->m_EndurancePack;
    const int iNow = (int)g_GameDATA.GetGameTime();

    /// In the legacy strip's order.
    for (int i = 0; i < pack.GetEntityCount(); ++i) {
        CEnduranceProperty* pEntity = pack.GetEntity(i);
        if (pEntity == NULL)
            continue;

        BuffVM vm;
        if (!RoseRmlIcons::Resolve(IMAGE_RES_STATE_ICON, pEntity->GetIconNO(), vm.src, vm.rect))
            continue;

        const char* pszName = STATE_NAME(pEntity->GetStateSTBNO());
        vm.name = pszName ? pszName : "";

        const int iElapsed = iNow - pEntity->GetStartTime();
        const int iDuration = pEntity->GetEnduranceTime() * 1000;
        const int iRemain = abs(iDuration - iElapsed);

        if (i == ING_GODDESS) {
            vm.expiring = false;
            vm.detail = GoddessDetail(pack, pEntity);
        } else {
            /// Past its duration but not yet removed flashes too, as before.
            vm.expiring = iRemain < kExpiringMs || iElapsed > iDuration;
            vm.time = ShortTime(iRemain);
            vm.detail = LongTime(iRemain);
        }
        out.push_back(vm);
    }
}

void
RoseRmlBuffBar::Sample() {
    CObjUSER* pAvatar = g_pAVATAR;

    /// --- status effects -----------------------------------------------------
    std::vector<BuffVM> buffs;
    CollectBuffs(pAvatar, buffs);
    if (buffs != m_Buffs) {
        m_Buffs.swap(buffs);
        m_Model.DirtyVariable("buffs");
    }

    /// --- summon capacity ---------------------------------------------------
    const int iSummonMax = pAvatar->GetCur_SummonMaxCapacity();
    const int iSummonUsed = pAvatar->GetCur_SummonUsedCapacity();
    const bool bSummon = iSummonMax > 0 && iSummonUsed > 0;
    Assign(m_bSummon, bSummon, "summon_on");
    if (bSummon) {
        Assign(m_fSummonWidth, 100.0f * (float)iSummonUsed / (float)iSummonMax, "summon_width");
        Assign(m_strSummon, Printf("%d / %d", iSummonUsed, iSummonMax), "summon");
        Assign(m_strSummonLabel, Rml::String(STR_SUMMON_CAPACITY), "summon_label");
    }

    /// --- cart fuel: riding your own cart, not as a passenger ---------------
    bool bFuel = false;
    if (pAvatar->GetPetMode() >= 0 && !pAvatar->IsRideUser()) {
        CItem* pEngine =
            pAvatar->GetItemSlot()->GetItem(INVENTORY_RIDE_ITEM0 + RIDE_PART_ENGINE);
        if (pEngine != NULL) {
            const int iLife = pEngine->GetItem().GetLife();
            const int iPct = iLife / 10 + ((iLife % 10) ? 1 : 0);
            bFuel = true;
            Assign(m_fFuelWidth, (float)min(100, max(0, iPct)), "fuel_width");
            Assign(m_strFuel, Printf("%d%%", iPct), "fuel");
            Assign(m_strFuelLabel, Rml::String(STR_FUEL), "fuel_label");
        }
    }
    Assign(m_bFuel, bFuel, "fuel_on");

    /// --- equipment about to break ------------------------------------------
    std::vector<WornVM> worn;
    CItemSlot* pItemSlot = pAvatar->GetItemSlot();
    for (int i = 1; i < MAX_EQUIP_IDX; ++i) {
        CItem* pItem = pItemSlot->GetItem(i);
        if (pItem == NULL || pItem->GetItem().GetLife() >= kWornLife)
            continue;

        WornVM vm;
        if (!RoseRmlIcons::Resolve(IMAGE_RES_ITEM,
                ITEM_ICON_NO(pItem->GetType(), pItem->GetItemNo()), vm.src, vm.rect))
            continue;
        const char* pszName = pItem->GetName();
        vm.name = pszName ? pszName : "";
        worn.push_back(vm);
    }
    if (worn != m_Worn) {
        m_Worn.swap(worn);
        m_Model.DirtyVariable("worn");
    }
}

void
RoseRmlBuffBar::FollowAnchor() {
    if (m_pAnchor == NULL || m_pPanel == NULL)
        return;

    /// Both documents' bodies sit at the screen origin, so absolute offsets
    /// from one are positions in the other. This reads last frame's layout, so
    /// the strip trails a dragged panel by one frame.
    const Rml::Vector2f anchorPos = m_pAnchor->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f anchorSize = m_pAnchor->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (anchorSize.y <= 0.0f)
        return;

    const Rml::Vector2i view = m_pContext->GetDimensions();
    float x = anchorPos.x;
    const float fGap = kAnchorGap * RoseRmlLayout::GetScaleRatio();
    float y = anchorPos.y + anchorSize.y + fGap;
    /// Near the bottom edge, hang above the panel instead of off screen.
    if (y + size.y > (float)view.y)
        y = anchorPos.y - size.y - fGap;

    if (x == m_fPlacedX && y == m_fPlacedY)
        return;
    m_fPlacedX = x;
    m_fPlacedY = y;
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
}

void
RoseRmlBuffBar::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;

    if (bInWorld)
        Sample();

    /// Hidden when there is nothing to show, so an empty strip is never an
    /// invisible click-eating rectangle.
    SetVisible(bInWorld && (!m_Buffs.empty() || !m_Worn.empty() || m_bSummon || m_bFuel));

    if (m_bVisible) {
        FollowAnchor();
        const Rml::Vector2i view = m_pContext->GetDimensions();
        RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
    }
}
