#include "stdafx.h"

#include "RoseRmlInventory.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../misc/gameutil.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CToolTipMgr.h"
#include "../interface/CUIMediator.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/Dlgs/CItemDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"
#include "../Network/CNetwork.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>
#include <stdarg.h>

namespace {

/// The three bag tabs of the gear section ( INV_WEAPON, INV_USE, INV_ETC ).
const int kBagTabs = 3;

/// The PAT parts, by RIDE_PART_*.
const char* const kPatLabel[MAX_RIDING_PART] = {"Body", "Engine", "Legs", "Special", "Arms"};

/// CIconItem::Update's red tint: an item under this much life.
const int kWornLife = 50;

/// The paper doll, four across ( EQUIP_IDX_*; 0 is a spacer ): head and
/// jewellery, weapons around the body, hands / feet / back.
const int kDoll[] = {EQUIP_IDX_HELMET,
    EQUIP_IDX_FACE_ITEM,
    EQUIP_IDX_NECKLACE,
    EQUIP_IDX_EARRING,
    EQUIP_IDX_WEAPON_R,
    EQUIP_IDX_ARMOR,
    EQUIP_IDX_WEAPON_L,
    EQUIP_IDX_RING,
    EQUIP_IDX_GAUNTLET,
    EQUIP_IDX_BOOTS,
    EQUIP_IDX_KNAPSACK,
    0};

/// The costume doll: the gear doll's positions, each costume slot where the
/// piece it covers sits ( COSTUME_IDX_*; 0 is a spacer ). Only these six can
/// be worn -- equip_costume refuses the costume weapon slots.
const int kCostumeDoll[] = {COSTUME_IDX_HELMET,
    COSTUME_IDX_FACE_ITEM,
    0,
    0,
    0,
    COSTUME_IDX_ARMOR,
    0,
    0,
    COSTUME_IDX_GAUNTLET,
    COSTUME_IDX_BOOTS,
    COSTUME_IDX_KNAPSACK,
    0};

/// ... and what goes in each, by COSTUME_IDX_*.
const char* const kCostumeLabel[MAX_COSTUME_IDX] = {
    "", "Face", "Head", "Body", "Back", "Hands", "Feet", "Weapon", "Off-hand"};

/// What an empty slot is for, by EQUIP_IDX_*.
const char* const kEquipLabel[MAX_EQUIP_IDX] = {
    "", "Face", "Head", "Body", "Back", "Hands", "Feet", "Weapon", "Off-hand", "Neck", "Ring", "Earring"};

/// ... and by SHOT_TYPE_*.
const char* const kAmmoLabel[MAX_SHOT_TYPE] = {"Arrows", "Bullets", "Throw"};

/// A cell's picture and states from the classic slot behind it.
void
FillCell(RoseRmlInventory::CellVM& vm, CSlot* pSlot) {
    vm.filled = false;
    vm.count = 0;
    vm.cd = 0.0f;
    vm.dim = false;
    vm.worn = false;
    vm.socket = 0;

    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    int iModule = 0, iGraphic = 0;
    if (pIcon == NULL || !pIcon->IsItemIcon() || !pIcon->GetSprite(iModule, iGraphic)
        || !RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect))
        return;

    CIconItem* pItemIcon = (CIconItem*)pIcon;
    tagITEM& Item = pItemIcon->GetItem();

    vm.filled = true;
    vm.count = pIcon->GetStackCount();
    vm.cd = floorf(pIcon->GetCooldown(NULL) * 100.0f + 0.5f);
    vm.dim = !pIcon->IsEnable();
    vm.worn = !vm.dim && Item.HasLife() && Item.GetLife() < kWornLife;

    /// The socket mark CIconItem::Draw puts on the icon.
    if (Item.HasSocket()) {
        vm.socket = 1;
        const int iGem = Item.GetGemNO();
        if (iGem > 300 && iGem <= (int)g_TblGEMITEM.row_count
            && RoseRmlIcons::Resolve(
                IMAGE_RES_SOCKETJAM_ICON, GEMITEM_MARK_IMAGE(iGem), vm.gem_src, vm.gem_rect))
            vm.socket = 2;
    }
}

RoseRmlInventory::CellVM
MakeCell(int iKind, int iIndex, const char* pszLabel) {
    RoseRmlInventory::CellVM vm;
    vm.kind = iKind;
    vm.index = iIndex;
    vm.label = pszLabel ? pszLabel : "";
    vm.filled = false;
    vm.count = 0;
    vm.cd = 0.0f;
    vm.dim = false;
    vm.worn = false;
    vm.socket = 0;
    return vm;
}

Rml::String
Printf(const char* pszFormat, ...) {
    char szBuf[96];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf(szBuf, sizeof(szBuf), pszFormat, args);
    va_end(args);
    szBuf[sizeof(szBuf) - 1] = '\0';
    return szBuf;
}

/// Walks up from the element under the point to the window's drop-target
/// root, returning the first element carrying pszAttr on the way ( NULL when
/// there is none, or the point is not over this window ).
Rml::Element*
FindUp(Rml::Context* pContext, int x, int y, const char* pszAttr) {
    if (pContext == NULL)
        return NULL;
    for (Rml::Element* pEl = pContext->GetElementAtPoint(Rml::Vector2f((float)x, (float)y));
         pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute(pszAttr))
            return pEl;
        if (pEl->GetId() == "inventory")
            return NULL;
    }
    return NULL;
}

} // namespace

RoseRmlInventory::RoseRmlInventory():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iPressKind(KIND_BAG),
    m_iPressIndex(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iDropType(DLG_TYPE_ITEM),
    m_iSection(SECTION_GEAR),
    m_iPage(INV_WEAPON),
    m_strMoney("0"),
    m_strWeight("0 / 0"),
    m_fWeightPct(0.0f),
    m_iWeightLevel(0) {
    for (int i = 0; i < MAX_INV_TYPE; ++i)
        m_iCount[i] = 0;
}

bool
RoseRmlInventory::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("inventory");
    if (!constructor)
        return false;

    if (auto cell = constructor.RegisterStruct<CellVM>()) {
        cell.RegisterMember("kind", &CellVM::kind);
        cell.RegisterMember("index", &CellVM::index);
        cell.RegisterMember("label", &CellVM::label);
        cell.RegisterMember("filled", &CellVM::filled);
        cell.RegisterMember("src", &CellVM::src);
        cell.RegisterMember("rect", &CellVM::rect);
        cell.RegisterMember("count", &CellVM::count);
        cell.RegisterMember("cd", &CellVM::cd);
        cell.RegisterMember("dim", &CellVM::dim);
        cell.RegisterMember("worn", &CellVM::worn);
        cell.RegisterMember("socket", &CellVM::socket);
        cell.RegisterMember("gem_src", &CellVM::gem_src);
        cell.RegisterMember("gem_rect", &CellVM::gem_rect);
    }
    constructor.RegisterArray<std::vector<CellVM>>();

    if (auto tune = constructor.RegisterStruct<TuneVM>()) {
        tune.RegisterMember("label", &TuneVM::label);
        tune.RegisterMember("value", &TuneVM::value);
        tune.RegisterMember("fuel", &TuneVM::fuel);
    }
    constructor.RegisterArray<std::vector<TuneVM>>();

    constructor.Bind("drop_type", &m_iDropType);
    constructor.Bind("section", &m_iSection);
    constructor.Bind("page", &m_iPage);
    constructor.Bind("count0", &m_iCount[0]);
    constructor.Bind("count1", &m_iCount[1]);
    constructor.Bind("count2", &m_iCount[2]);
    constructor.Bind("count3", &m_iCount[3]);
    constructor.Bind("pat", &m_Pat);
    constructor.Bind("costume", &m_Costume);
    constructor.Bind("tune", &m_Tune);
    constructor.Bind("cells", &m_Cells);
    constructor.Bind("gear", &m_Gear);
    constructor.Bind("ammo", &m_Ammo);
    constructor.Bind("atk", &m_strAtk);
    constructor.Bind("def", &m_strDef);
    constructor.Bind("res", &m_strRes);
    constructor.Bind("money", &m_strMoney);
    constructor.Bind("weight", &m_strWeight);
    constructor.Bind("weight_pct", &m_fWeightPct);
    constructor.Bind("weight_level", &m_iWeightLevel);

    constructor.BindEventCallback("set_section",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iSection = args[0].Get<int>();
            if (iSection >= SECTION_GEAR && iSection <= SECTION_COSTUME && iSection != m_iSection) {
                m_iSection = iSection;
                m_iPressIndex = -1;
                m_Model.DirtyVariable("section");
                Sample();
            }
        });

    constructor.BindEventCallback("set_page",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iPage = args[0].Get<int>();
            if (iPage >= 0 && iPage < kBagTabs && iPage != m_iPage) {
                m_iPage = iPage;
                m_iPressIndex = -1;
                m_Model.DirtyVariable("page");
                Sample();
            }
        });

    /// press(kind, index)
    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.size() < 2 || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
            OnPress(args[0].Get<int>(), args[1].Get<int>());
        });

    /// use(kind, index) -- double-click: use / equip / unequip, as CSlot's
    /// WM_LBUTTONDBLCLK.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.size() < 2)
                return;
            m_iPressIndex = -1;
            CSlot* pSlot = SlotFor(args[0].Get<int>(), args[1].Get<int>());
            if (pSlot && pSlot->GetIcon())
                pSlot->GetIcon()->ExecuteCommand();
        });

    constructor.BindEventCallback("money",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (CItemDlg* pDlg = ItemDlg())
                pDlg->OnMoneyButton();
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { SetOpen(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "inventory.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load inventory document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("inventory");
    RoseRmlLayout::Track(m_pPanel, "inventory");

    LOG_INFO("[rmlui] inventory document loaded");
    return true;
}

void
RoseRmlInventory::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CItemDlg*
RoseRmlInventory::ItemDlg() const {
    return (CItemDlg*)g_itMGR.FindDlg(DLG_TYPE_ITEM);
}

int
RoseRmlInventory::CurrentPage() const {
    /// The PAT section shows the riding-parts page, as CItemDlg's tuning tab;
    /// the costume section the equipment page, as its costume tab.
    if (m_iSection == SECTION_PAT)
        return INV_RIDING;
    if (m_iSection == SECTION_COSTUME)
        return INV_WEAPON;
    return m_iPage;
}

CSlot*
RoseRmlInventory::SlotFor(int iKind, int iIndex) const {
    CItemDlg* pDlg = ItemDlg();
    if (pDlg == NULL)
        return NULL;
    switch (iKind) {
        case KIND_BAG:
            return pDlg->GetBagSlot(CurrentPage(), iIndex);
        case KIND_EQUIP:
            return pDlg->GetEquipSlotCtrl(iIndex);
        case KIND_AMMO:
            return pDlg->GetAmmoSlot(iIndex);
        case KIND_PAT:
            return pDlg->GetPatSlot(iIndex);
        case KIND_COSTUME:
            return pDlg->GetCostumeSlot(iIndex);
        default:
            return NULL;
    }
}

CDragItem*
RoseRmlInventory::DragItemFor(int iKind) const {
    CItemDlg* pDlg = ItemDlg();
    if (pDlg == NULL)
        return NULL;
    /// As CItemDlg wires its slots: the bag drags one way, everything worn
    /// ( gear, ammo, PAT parts, costumes ) the other.
    return (iKind == KIND_BAG) ? pDlg->GetInvenDragItem() : pDlg->GetEquipDragItem();
}

bool
RoseRmlInventory::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlInventory::SetOpen(bool bOpen) {
    if (bOpen != m_bOpen)
        RoseUi2::PlayWindowSound(DLG_TYPE_ITEM, bOpen);
    m_bOpen = bOpen;
    m_iPressIndex = -1;
    if (bOpen)
        Sample(); /// no stale grid on the first frame
}

void
RoseRmlInventory::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressIndex = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- CItemDlg's questions ---------------------------------------------------

bool
RoseRmlInventory::BagAt(int x, int y) const {
    return m_bVisible && FindUp(m_pContext, x, y, "bag-area") != NULL;
}

bool
RoseRmlInventory::BagSlotAt(int x, int y, int& iPage, int& iSlot) const {
    if (!m_bVisible)
        return false;
    Rml::Element* pEl = FindUp(m_pContext, x, y, "slot-kind");
    if (pEl == NULL || pEl->GetAttribute<int>("slot-kind", -1) != KIND_BAG)
        return false;
    iSlot = pEl->GetAttribute<int>("slot-index", -1);
    iPage = CurrentPage();
    return iSlot >= 0 && iSlot < INVENTORY_PAGE_SIZE;
}

bool
RoseRmlInventory::EquipAt(int x, int y) const {
    return m_bVisible && FindUp(m_pContext, x, y, "equip-area") != NULL;
}

int
RoseRmlInventory::EquipSlotAt(int x, int y) const {
    /// Worn gear only ( a gem is socketed into it ); the ammo row and the
    /// doll's spacer answer -1, as the classic hit test did.
    if (!m_bVisible)
        return -1;
    Rml::Element* pEl = FindUp(m_pContext, x, y, "slot-kind");
    if (pEl == NULL || pEl->GetAttribute<int>("slot-kind", -1) != KIND_EQUIP)
        return -1;
    const int iIdx = pEl->GetAttribute<int>("slot-index", -1);
    return (iIdx >= 1 && iIdx < MAX_EQUIP_IDX) ? iIdx : -1;
}

bool
RoseRmlInventory::CostumeOpen() const {
    /// On screen only. The classic dialog answered from its last tab even while
    /// closed, so armour equipped from the skill bar with the inventory shut
    /// could land in a costume slot.
    return m_bVisible && m_iSection == SECTION_COSTUME;
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlInventory::Sample() {
    CObjUSER* pAvatar = g_pAVATAR;
    CItemDlg* pDlg = ItemDlg();
    if (pAvatar == NULL || pDlg == NULL)
        return;

    /// --- the page on screen -------------------------------------------------
    std::vector<CellVM> cells;
    cells.reserve(INVENTORY_PAGE_SIZE);
    for (int i = 0; i < INVENTORY_PAGE_SIZE; ++i) {
        CellVM vm = MakeCell(KIND_BAG, i, NULL);
        FillCell(vm, pDlg->GetBagSlot(CurrentPage(), i));
        cells.push_back(vm);
    }
    if (cells != m_Cells) {
        m_Cells.swap(cells);
        m_Model.DirtyVariable("cells");
    }

    /// --- worn gear and ammo ---------------------------------------------------
    std::vector<CellVM> gear;
    for (int i = 0; i < (int)(sizeof(kDoll) / sizeof(kDoll[0])); ++i) {
        CellVM vm = MakeCell(KIND_EQUIP, kDoll[i], kEquipLabel[kDoll[i]]);
        if (kDoll[i] != 0)
            FillCell(vm, pDlg->GetEquipSlotCtrl(kDoll[i]));
        gear.push_back(vm);
    }
    if (gear != m_Gear) {
        m_Gear.swap(gear);
        m_Model.DirtyVariable("gear");
    }

    std::vector<CellVM> ammo;
    for (int i = 0; i < MAX_SHOT_TYPE; ++i) {
        CellVM vm = MakeCell(KIND_AMMO, i, kAmmoLabel[i]);
        FillCell(vm, pDlg->GetAmmoSlot(i));
        ammo.push_back(vm);
    }
    if (ammo != m_Ammo) {
        m_Ammo.swap(ammo);
        m_Model.DirtyVariable("ammo");
    }

    std::vector<CellVM> pat;
    for (int i = 0; i < MAX_RIDING_PART; ++i) {
        CellVM vm = MakeCell(KIND_PAT, i, kPatLabel[i]);
        FillCell(vm, pDlg->GetPatSlot(i));
        pat.push_back(vm);
    }
    if (pat != m_Pat) {
        m_Pat.swap(pat);
        m_Model.DirtyVariable("pat");
    }
    if (m_iSection == SECTION_PAT)
        SampleTuning();

    std::vector<CellVM> costume;
    for (int i = 0; i < (int)(sizeof(kCostumeDoll) / sizeof(kCostumeDoll[0])); ++i) {
        CellVM vm = MakeCell(KIND_COSTUME, kCostumeDoll[i], kCostumeLabel[kCostumeDoll[i]]);
        if (kCostumeDoll[i] != 0)
            FillCell(vm, pDlg->GetCostumeSlot(kCostumeDoll[i]));
        costume.push_back(vm);
    }
    if (costume != m_Costume) {
        m_Costume.swap(costume);
        m_Model.DirtyVariable("costume");
    }

    /// --- what the gear adds up to ( the character window's numbers ) --------
    const Rml::String strAtk = Printf("%d", pAvatar->stats.attack_power);
    const Rml::String strDef = Printf("%d", pAvatar->Get_DEF());
    const Rml::String strRes = Printf("%d", pAvatar->Get_RES());
    if (strAtk != m_strAtk) {
        m_strAtk = strAtk;
        m_Model.DirtyVariable("atk");
    }
    if (strDef != m_strDef) {
        m_strDef = strDef;
        m_Model.DirtyVariable("def");
    }
    if (strRes != m_strRes) {
        m_strRes = strRes;
        m_Model.DirtyVariable("res");
    }

    /// --- tab counts -----------------------------------------------------------
    static const char* const kCountNames[MAX_INV_TYPE] = {"count0", "count1", "count2", "count3"};
    for (int t = 0; t < MAX_INV_TYPE; ++t) {
        int iFilled = 0;
        for (int i = 0; i < INVENTORY_PAGE_SIZE; ++i) {
            CSlot* pSlot = pDlg->GetBagSlot(t, i);
            if (pSlot && pSlot->GetIcon())
                ++iFilled;
        }
        if (iFilled != m_iCount[t]) {
            m_iCount[t] = iFilled;
            m_Model.DirtyVariable(kCountNames[t]);
        }
    }

    /// --- money and weight -------------------------------------------------------
    char szMoney[64];
    CGameUtil::ConvertMoney2String(pAvatar->Get_MONEY(), szMoney, sizeof(szMoney));
    if (m_strMoney != szMoney) {
        m_strMoney = szMoney;
        m_Model.DirtyVariable("money");
    }

    const int iWeight = pAvatar->GetCur_WEIGHT();
    const int iMaxWeight = pAvatar->GetCur_MaxWEIGHT();
    const Rml::String strWeight = Printf("%d / %d", iWeight, iMaxWeight);
    if (strWeight != m_strWeight) {
        m_strWeight = strWeight;
        m_Model.DirtyVariable("weight");
    }
    const float fPct = (iMaxWeight > 0) ? 100.0f * (float)iWeight / (float)iMaxWeight : 0.0f;
    const float fBar = floorf(fPct > 100.0f ? 100.0f : fPct);
    if (fBar != m_fWeightPct) {
        m_fWeightPct = fBar;
        m_Model.DirtyVariable("weight_pct");
    }
    const int iLevel = (fPct >= 100.0f) ? 2 : (fPct >= 80.0f ? 1 : 0);
    if (iLevel != m_iWeightLevel) {
        m_iWeightLevel = iLevel;
        m_Model.DirtyVariable("weight_level");
    }
}

/// The mounted-stats table, from the server's preview ( as DrawTuningStats ).
void
RoseRmlInventory::SampleTuning() {
    if (g_pNet == NULL)
        return;
    const Rose::Tuning::MountedStatsResult& value = g_pNet->tuning_preview.result;

    static const char* const kLabels[7] = {
        "Type", "Defense", "Magic Resist", "Fuel use", "Move speed", "Attack", "Attack speed"};
    const int iNumbers[7] = {0, value.defence, value.resistance, value.fuel, value.speed, 0,
        value.attack_speed};

    std::vector<TuneVM> rows;
    for (int row = 0; row < 7; ++row) {
        TuneVM vm;
        vm.label = kLabels[row];
        vm.fuel = (row == 3);
        if (!(value.valid_fields & (1 << row)))
            vm.value = "\xE2\x80\x94"; /// em dash, UTF-8: the parts do not allow it
        else if (row == 0)
            vm.value = (value.vehicle_type == 2) ? "Castle Gear" : "Cart";
        else if (row == 5)
            vm.value = Printf("%u", value.attack);
        else
            vm.value = Printf("%d", iNumbers[row]);
        rows.push_back(vm);
    }
    if (rows != m_Tune) {
        m_Tune.swap(rows);
        m_Model.DirtyVariable("tune");
    }
}

/// --- input ------------------------------------------------------------------------

/// A left press on a cell, with CSlot::Process's precedence: Alt previews,
/// Shift links, Ctrl asks for the wishlist; then repair / appraisal take it;
/// otherwise it may become a drag.
void
RoseRmlInventory::OnPress(int iKind, int iIndex) {
    m_iPressIndex = -1;

    CSlot* pSlot = SlotFor(iKind, iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;

    CIconItem* pItemIcon = (CIconItem*)pIcon;
    tagITEM& Item = pItemIcon->GetItem();

    if (GetAsyncKeyState(VK_MENU) < 0 && !Item.IsEmpty() && g_UIMed.OpenItemPreview(Item))
        return;

    if (GetAsyncKeyState(VK_SHIFT) < 0) {
        if (!Item.IsEmpty()) {
            if (CChatDLG* pChatDlg = g_itMGR.GetChatDLG())
                pChatDlg->AddItemLinkToInput(Item);
        }
        return;
    }

    /// Ctrl: CIconItem::Process's wishlist question.
    if (GetAsyncKeyState(VK_CONTROL) < 0 && pIcon->Process(WM_LBUTTONDOWN, MK_CONTROL, 0))
        return;

    /// Repair / appraisal: CItemDlg listens on the bag, the worn gear, the PAT
    /// parts and the costumes, not on the ammo slots.
    if (iKind != KIND_AMMO) {
        if (CItemDlg* pDlg = ItemDlg()) {
            if (pDlg->HandleStateClick(pSlot))
                return;
        }
    }

    if (pIcon->IsEnable()) {
        m_iPressKind = iKind;
        m_iPressIndex = iIndex;
    }
}

void
RoseRmlInventory::UpdateDragStart() {
    if (m_iPressIndex < 0)
        return;

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_iPressIndex = -1;
        return;
    }

    /// CSlot::Update's threshold: a two-hundredth of the screen.
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iSlopX = max(3, g_pCApp->GetWIDTH() / 200);
    const int iSlopY = max(3, g_pCApp->GetHEIGHT() / 200);
    if (abs(ptMouse.x - m_iPressX) < iSlopX && abs(ptMouse.y - m_iPressY) < iSlopY)
        return;

    const int iKind = m_iPressKind;
    const int iIndex = m_iPressIndex;
    m_iPressIndex = -1;

    CSlot* pSlot = SlotFor(iKind, iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    CDragItem* pDrag = DragItemFor(iKind);
    if (pIcon == NULL || pDrag == NULL)
        return;

    /// CItemDlg's own drag: the clone keeps its slot ( CIconItem::Clone ),
    /// which the rearrange command needs to find where it came from.
    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlInventory::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iKind = -1, iIndex = -1;
    for (Rml::Element* pEl = m_pContext->GetHoverElement(); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("slot-kind")) {
            iKind = pEl->GetAttribute<int>("slot-kind", -1);
            iIndex = pEl->GetAttribute<int>("slot-index", -1);
            break;
        }
    }
    if (iKind < 0 || iIndex < 0) {
        UpdateTuningTooltip();
        return;
    }

    CSlot* pSlot = SlotFor(iKind, iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL)
        return;

    /// DLG_TYPE_ITEM, as CSlot passes its parent: the tooltip adds the repair,
    /// appraisal, sell or storage price the moment calls for.
    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, DLG_TYPE_ITEM, 0);
    if (ToolTip.IsEmpty())
        return;

    PlaceTooltip(ToolTip);
}

void
RoseRmlInventory::PlaceTooltip(CInfo& ToolTip) {
    /// Beside the window, on the side chosen by where the WINDOW sits.
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const Rml::Vector2f pos = m_pPanel->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    const int iScreenW = g_pCApp->GetWIDTH();
    const int iScreenH = g_pCApp->GetHEIGHT();

    POINT pt;
    const bool bRightSide = (pos.x + size.x * 0.5f) < (float)iScreenW * 0.5f;
    pt.x = bRightSide ? (int)(pos.x + size.x) + 4 : (int)pos.x - ToolTip.GetWidth() - 4;
    if (pt.x > iScreenW - ToolTip.GetWidth())
        pt.x = iScreenW - ToolTip.GetWidth();
    if (pt.x < 0)
        pt.x = 0;
    pt.y = ptMouse.y - ToolTip.GetHeight() / 2;
    if (pt.y > iScreenH - ToolTip.GetHeight())
        pt.y = iScreenH - ToolTip.GetHeight();
    if (pt.y < 0)
        pt.y = 0;

    ToolTip.SetPosition(pt);
    CToolTipMgr::GetInstance().RegistInfo(ToolTip);
}

void
RoseRmlInventory::UpdateTuningTooltip() {
    int iTip = 0;
    for (Rml::Element* pEl = m_pContext->GetHoverElement(); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("tune-tip")) {
            iTip = pEl->GetAttribute<int>("tune-tip", 0);
            break;
        }
    }
    if (iTip == 0)
        return;

    CInfo ToolTip;
    CItemDlg::BuildTuningTooltip(ToolTip, iTip == 2);
    PlaceTooltip(ToolTip);
}

void
RoseRmlInventory::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = IsInWorld();
    if (!bInWorld)
        m_bOpen = false;

    SetVisible(m_bOpen && bInWorld);

    /// The server's mounted-stats preview runs while the PAT section is on
    /// screen. Only while UI2 is on: otherwise CItemDlg drives it itself.
    if (RoseUi2::IsActive()) {
        if (CItemDlg* pDlg = ItemDlg())
            pDlg->DriveTuningPreview(m_bVisible && m_iSection == SECTION_PAT);
    }

    if (!m_bVisible)
        return;

    Sample();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}
