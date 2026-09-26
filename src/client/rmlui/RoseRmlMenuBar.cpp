#include "stdafx.h"

#include "RoseRmlMenuBar.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CObjUSER.h"
#include "../Game.h"
#include "../System/CGame.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Dlgs/CMenuDlg.h"

#include "rose/common/log.h"

#include <math.h>

namespace {

/// The bar's buttons, in order: the window each opens, the classic menu's
/// button id ( the tutorial blinks by it ), the label, and the caption.
struct MenuEntry {
    int iDlgType;
    int iMenuButtonId;
    const char* pszLabel;
    const char* pszName;
    const char* pszKey; ///< CITStateNormal's Alt shortcut
};

const MenuEntry kEntries[] = {
    {DLG_TYPE_CHAR, MENU_BTN_CHAR, "Char", "Character", "Alt+A"},
    {DLG_TYPE_ITEM, MENU_BTN_ITEM, "Bag", "Inventory", "Alt+I"},
    {DLG_TYPE_SKILL, MENU_BTN_SKILL, "Skills", "Skills", "Alt+S"},
    {DLG_TYPE_QUEST, MENU_BTN_QUEST, "Quests", "Quest journal", "Alt+Q"},
    {DLG_TYPE_COMMUNITY, MENU_BTN_COMMUNITY, "Community", "Friends and chat rooms", "Alt+C"},
    {DLG_TYPE_CLAN, MENU_BTN_CLAN, "Clan", "Clan", "Alt+N"},
    {DLG_TYPE_OPTION, MENU_BTN_OPTION, "Options", "Options", "Alt+O"},
    {DLG_TYPE_SYSTEM, MENU_BTN_EXIT, "System", "Log out or exit", "Alt+X"},
};
const int kEntryCount = (int)(sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace

RoseRmlMenuBar::RoseRmlMenuBar():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_iHover(-1),
    m_bCaption(false) {}

bool
RoseRmlMenuBar::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    for (int i = 0; i < kEntryCount; ++i) {
        EntryVM vm;
        vm.index = i;
        vm.label = kEntries[i].pszLabel;
        vm.on = false;
        vm.blink = false;
        m_Entries.push_back(vm);
    }

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("menubar");
    if (!constructor)
        return false;

    if (auto entry = constructor.RegisterStruct<EntryVM>()) {
        entry.RegisterMember("index", &EntryVM::index);
        entry.RegisterMember("label", &EntryVM::label);
        entry.RegisterMember("on", &EntryVM::on);
        entry.RegisterMember("blink", &EntryVM::blink);
    }
    constructor.RegisterArray<std::vector<EntryVM>>();

    constructor.Bind("entries", &m_Entries);
    constructor.Bind("caption", &m_strCaption);
    constructor.Bind("has_caption", &m_bCaption);

    constructor.BindEventCallback("open",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                OnOpen(args[0].Get<int>());
        });
    constructor.BindEventCallback("hover",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                OnHover(args[0].Get<int>());
        });
    constructor.BindEventCallback("unhover",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnHover(-1); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "menubar.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load menu bar document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("menubar");
    RoseRmlLayout::Track(m_pPanel, "menubar");

    LOG_INFO("[rmlui] menu bar document loaded");
    return true;
}

void
RoseRmlMenuBar::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

bool
RoseRmlMenuBar::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlMenuBar::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

bool
RoseRmlMenuBar::Blink(int iMenuButtonId) {
    for (int i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].iMenuButtonId != iMenuButtonId)
            continue;
        if (!m_Entries[i].blink) {
            m_Entries[i].blink = true;
            m_Model.DirtyVariable("entries");
        }
        return true;
    }
    return false;
}

void
RoseRmlMenuBar::OnOpen(int iIndex) {
    if (iIndex < 0 || iIndex >= kEntryCount)
        return;
    if (m_Entries[iIndex].blink) {
        m_Entries[iIndex].blink = false;
        m_Model.DirtyVariable("entries");
    }
    /// As the classic menu and the Alt shortcuts: a toggle.
    g_itMGR.OpenDialog(kEntries[iIndex].iDlgType);
}

void
RoseRmlMenuBar::OnHover(int iIndex) {
    if (iIndex == m_iHover)
        return;
    m_iHover = iIndex;
    if (iIndex >= 0 && iIndex < kEntryCount) {
        /// The tutorial's blink stops once the button is found ( CTButton
        /// stopped it on mouse-over ).
        if (m_Entries[iIndex].blink) {
            m_Entries[iIndex].blink = false;
            m_Model.DirtyVariable("entries");
        }
        m_strCaption = Rml::String(kEntries[iIndex].pszName) + "   " + kEntries[iIndex].pszKey;
        m_bCaption = true;
    } else {
        m_bCaption = false;
    }
    m_Model.DirtyVariable("caption");
    m_Model.DirtyVariable("has_caption");
}

void
RoseRmlMenuBar::PlaceDefault() {
    /// Bottom right, where the classic menu opened -- only while no position
    /// is set ( dragged, saved or reset ).
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fGap = floorf(6.0f * RoseRmlLayout::GetScaleRatio());
    const float fLeft = floorf((float)view.x - size.x - fGap);
    const float fTop = floorf((float)view.y - size.y - fGap);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlMenuBar::Update() {
    if (m_pDocument == NULL)
        return;

    SetVisible(IsInWorld());
    if (!m_bVisible)
        return;

    /// A button lights while its window is open.
    bool bChanged = false;
    for (int i = 0; i < kEntryCount; ++i) {
        const bool bOn = g_itMGR.IsDlgOpened(kEntries[i].iDlgType);
        if (m_Entries[i].on != bOn) {
            m_Entries[i].on = bOn;
            bChanged = true;
        }
    }
    if (bChanged)
        m_Model.DirtyVariable("entries");

    PlaceDefault();
    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
