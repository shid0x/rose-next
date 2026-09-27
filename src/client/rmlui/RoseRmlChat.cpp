#include "stdafx.h"

#include "RoseRmlChat.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../CObjUSER.h"
#include "../Game.h"
#include "../System/CGame.h"
#include "../GameCommon/Item.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CUIMediator.h"
#include "../interface/chatitemlink.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Dlgs/ChattingDLG.h"
#include "tgamectrl/tcontrolmgr.h"
#include "tgamectrl/teditbox.h"
#include "tgamectrl/tgamectrl.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>

namespace {

const char* kIniPath = ".\\rose-next.ini";

/// How much is kept: the session's lines, and the elements each list shows.
const size_t kChatKeep = 400;
const size_t kSystemKeep = 80;
const int kLogView = 200;
const int kSystemView = 40;

/// The input's limit ( the wire allows 129 bytes after item-link tokens,
/// which SendLine checks ).
const int kInputMax = 120;

const char* kTabLabels[RoseRmlChat::kTabCount] = {"All", "Whisper", "Trade", "Party", "Clan"};
/// What a tab click puts in the input ( the classic tabs' prefixes ).
const char* kTabPrefix[RoseRmlChat::kTabCount] = {"", "@", "$", "#", "&"};
/// The channel a tab exists for: always shown there ( -1: none ).
const int kTabOwn[RoseRmlChat::kTabCount] = {
    -1, -1, CChatDLG::FILTER_TRADE, CChatDLG::FILTER_PARTY, CChatDLG::FILTER_CLAN};
/// The kinds of line a tab can show ( CChatDLG::FILTER_NORMAL .. CLAN;
/// whispers always show, the alliance channel is gone ).
const char* kFilterLabels[RoseRmlChat::kFilterCount] = {
    "Nearby chat", "Notices", "Trade", "Party", "Clan"};

/// The window's size limits ( dp ) and default.
const float kMinW = 300.0f, kMinH = 170.0f;
const float kMaxW = 900.0f, kMaxH = 720.0f;
const float kDefaultW = 400.0f, kDefaultH = 250.0f;

Rml::String
Colour(DWORD dwColor) {
    char szBuf[16];
    _snprintf(szBuf, sizeof(szBuf), "#%02x%02x%02x", (dwColor >> 16) & 0xff, (dwColor >> 8) & 0xff,
        dwColor & 0xff);
    szBuf[sizeof(szBuf) - 1] = '\0';
    return szBuf;
}

Rml::String
Text(const std::string& strGame) {
    return RoseRmlText::Escape(RoseRmlText::FromGame(strGame.c_str()));
}

/// UTF-8 ( the input ) -> the game's code page.
std::string
ToGame(const Rml::String& strText) {
    if (strText.empty())
        return std::string();
    const int iWide = MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), -1, NULL, 0);
    if (iWide <= 0)
        return strText;
    std::wstring wide((size_t)iWide, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), -1, &wide[0], iWide);
    const int iAnsi = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, NULL, 0, NULL, NULL);
    if (iAnsi <= 0)
        return strText;
    std::string out((size_t)iAnsi, '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, &out[0], iAnsi, NULL, NULL);
    out.resize((size_t)iAnsi - 1);
    return out;
}

/// A chat line as RML: its colour, and each item link a span carrying the
/// item's 6 wire bytes ( hex ) for the tooltip and the preview.
Rml::String
LineRml(const char* pszMsg, DWORD dwColor) {
    char szDisplay[512];
    ChatItemLinkRange Links[CHAT_ITEM_LINK_MAX_PER_MSG];
    const int iLinks =
        ChatItemLink_Decode(pszMsg, szDisplay, sizeof(szDisplay), Links, CHAT_ITEM_LINK_MAX_PER_MSG);
    const std::string strText = (iLinks > 0) ? std::string(szDisplay) : std::string(pszMsg);

    Rml::String out = "<span style=\"color: " + Colour(dwColor) + ";\">";
    int iPos = 0;
    for (int i = 0; i < iLinks; ++i) {
        const int iBegin = Links[i].iBegin, iEnd = Links[i].iEnd;
        if (iBegin < iPos || iEnd > (int)strText.size() || iEnd <= iBegin)
            continue;
        out += Text(strText.substr(iPos, iBegin - iPos));
        char szHex[16];
        for (int b = 0; b < 6; ++b)
            _snprintf(szHex + b * 2, 3, "%02X", Links[i].Item[b]);
        szHex[12] = '\0';
        out += Rml::String("<span class=\"ilink\" item=\"") + szHex + "\" style=\"color: "
            + Colour(Links[i].dwColor) + ";\">" + Text(strText.substr(iBegin, iEnd - iBegin))
            + "</span>";
        iPos = iEnd;
    }
    out += Text(strText.substr(iPos)) + "</span>";
    return out;
}

/// The item a link span carries.
bool
ItemFromHex(const Rml::String& strHex, tagITEM& sItem) {
    if (strHex.size() != 12)
        return false;
    unsigned char Bytes[6];
    for (int b = 0; b < 6; ++b) {
        unsigned int v = 0;
        if (sscanf(strHex.c_str() + b * 2, "%2X", &v) != 1)
            return false;
        Bytes[b] = (unsigned char)v;
    }
    return ChatItemLink_ItemFromBytes(Bytes, sItem);
}

/// The link under an element, walking up.
Rml::Element*
LinkAt(Rml::Element* pEl) {
    for (; pEl != NULL; pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("item"))
            return pEl;
    }
    return NULL;
}

} // namespace

RoseRmlChat::RoseRmlChat():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_pLog(NULL),
    m_pSystem(NULL),
    m_pInput(NULL),
    m_bVisible(false),
    m_bAutoEnter(false),
    m_iTab(0),
    m_iScrollLog(0),
    m_iScrollSystem(0),
    m_bResizing(false),
    m_iPressX(0),
    m_iPressY(0),
    m_fPressW(0.0f),
    m_fPressH(0.0f),
    m_fPressTop(0.0f),
    m_fW(kDefaultW),
    m_fH(kDefaultH),
    m_bMenu(false),
    m_iMenuTab(0),
    m_strMenuTab(kTabLabels[0]) {
    for (int t = 0; t < kTabCount; ++t)
        for (int f = 0; f < kFilterCount; ++f)
            m_bShow[t][f] = false;
}

bool
RoseRmlChat::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;
    LoadFilters();

    for (int t = 0; t < kTabCount; ++t) {
        TabVM vm = {t, kTabLabels[t], t == m_iTab};
        m_Tabs.push_back(vm);
    }
    for (int f = 0; f < kFilterCount; ++f) {
        FilterVM vm = {f, kFilterLabels[f], false, false};
        m_Filters.push_back(vm);
    }

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("chat");
    if (!constructor)
        return false;

    if (auto tab = constructor.RegisterStruct<TabVM>()) {
        tab.RegisterMember("index", &TabVM::index);
        tab.RegisterMember("label", &TabVM::label);
        tab.RegisterMember("on", &TabVM::on);
    }
    constructor.RegisterArray<std::vector<TabVM>>();
    if (auto filter = constructor.RegisterStruct<FilterVM>()) {
        filter.RegisterMember("index", &FilterVM::index);
        filter.RegisterMember("label", &FilterVM::label);
        filter.RegisterMember("on", &FilterVM::on);
        filter.RegisterMember("locked", &FilterVM::locked);
    }
    constructor.RegisterArray<std::vector<FilterVM>>();

    constructor.Bind("tabs", &m_Tabs);
    constructor.Bind("filters", &m_Filters);
    constructor.Bind("menu", &m_bMenu);
    constructor.Bind("menu_tab", &m_strMenuTab);

    /// A tab: selects it, and puts its prefix in the input, as the classic
    /// tabs did.
    constructor.BindEventCallback("tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iTab = args[0].Get<int>();
            if (iTab < 0 || iTab >= kTabCount)
                return;
            SelectTab(iTab);
            if (m_bMenu)
                OpenMenu(iTab); /// the open filter follows to the new tab
            SetInput(kTabPrefix[iTab]);
            FocusInput();
        });
    /// The Filter button: the menu for the tab on screen ( again: closes it ).
    constructor.BindEventCallback("filter_menu",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (m_bMenu) {
                m_bMenu = false;
                m_Model.DirtyVariable("menu");
            } else {
                OpenMenu(m_iTab);
            }
        });
    constructor.BindEventCallback("toggle_filter",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                ToggleFilter(args[0].Get<int>());
        });
    /// Enter in the input ( a "change" with linebreak ).
    constructor.BindEventCallback("input_change",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<bool>("linebreak", false))
                Send();
        });
    constructor.BindEventCallback("log_press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) { OnLogPress(ev); });
    constructor.BindEventCallback("resize_press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<int>("button", 0) != 0 || m_pPanel == NULL || RoseRmlLayout::IsLocked())
                return;
            m_bResizing = true;
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
            m_fPressW = m_fW;
            m_fPressH = m_fH;
            m_fPressTop = m_pPanel->GetAbsoluteTop();
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "chat.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load chat document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("chat");
    m_pLog = m_pDocument->GetElementById("log");
    m_pSystem = m_pDocument->GetElementById("system");
    m_pInput = m_pDocument->GetElementById("chatin");
    RoseRmlLayout::Track(m_pPanel, "chat");
    LoadSize();
    RefreshTabs();

    LOG_INFO("[rmlui] chat document loaded");
    return true;
}

void
RoseRmlChat::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_pLog = NULL;
    m_pSystem = NULL;
    m_pInput = NULL;
    m_bVisible = false;
}

bool
RoseRmlChat::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlChat::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_bResizing = false;
    if (bVisible) {
        m_pDocument->Show();
        m_iScrollLog = m_iScrollSystem = 2;
    } else {
        m_pDocument->Hide();
    }
}

/// --- the log ---------------------------------------------------------------------

bool
RoseRmlChat::Passes(int iTab, int iFilter) const {
    if (iFilter == CChatDLG::FILTER_WHISPER)
        return true; /// in every tab, as the classic window
    if (iFilter < 0 || iFilter >= kFilterCount)
        return false; /// the alliance channel, gone
    return iFilter == kTabOwn[iTab] || m_bShow[iTab][iFilter];
}

bool
RoseRmlChat::IsAtEnd(Rml::Element* pList) const {
    return pList->GetScrollTop() + pList->GetClientHeight() >= pList->GetScrollHeight() - 4.0f;
}

void
RoseRmlChat::ScrollToEnd(Rml::Element* pList) {
    pList->SetScrollTop(pList->GetScrollHeight());
}

void
RoseRmlChat::AddLineElement(Rml::Element* pList, const Rml::String& strRml, int iCap) {
    if (m_pDocument == NULL || pList == NULL)
        return;
    Rml::ElementPtr pLine = m_pDocument->CreateElement("div");
    pLine->SetClass("line", true);
    pLine->SetInnerRML(strRml);
    pList->AppendChild(std::move(pLine));
    while (pList->GetNumChildren() > iCap)
        pList->RemoveChild(pList->GetFirstChild());
}

void
RoseRmlChat::Append(const char* pszMsg, DWORD dwColor, int iFilter) {
    if (pszMsg == NULL || m_pDocument == NULL)
        return;

    const Rml::String strRml = LineRml(pszMsg, dwColor);
    if (iFilter < 0) {
        m_System.push_back(strRml);
        if (m_System.size() > kSystemKeep)
            m_System.pop_front();
        /// Keep following the end unless the player scrolled up to read.
        if (m_pSystem != NULL) {
            if (IsAtEnd(m_pSystem))
                m_iScrollSystem = 2;
            AddLineElement(m_pSystem, strRml, kSystemView);
        }
        return;
    }

    Line line = {strRml, iFilter};
    m_Chat.push_back(line);
    if (m_Chat.size() > kChatKeep)
        m_Chat.pop_front();
    if (m_pLog != NULL && Passes(m_iTab, iFilter)) {
        if (IsAtEnd(m_pLog))
            m_iScrollLog = 2;
        AddLineElement(m_pLog, strRml, kLogView);
    }
}

/// The tab's lines, from the session's store.
void
RoseRmlChat::RebuildLog() {
    if (m_pLog == NULL)
        return;
    while (m_pLog->GetNumChildren() > 0)
        m_pLog->RemoveChild(m_pLog->GetFirstChild());

    std::vector<const Line*> Shown;
    for (std::deque<Line>::const_reverse_iterator it = m_Chat.rbegin();
         it != m_Chat.rend() && (int)Shown.size() < kLogView; ++it) {
        if (Passes(m_iTab, it->iFilter))
            Shown.push_back(&*it);
    }
    for (std::vector<const Line*>::reverse_iterator it = Shown.rbegin(); it != Shown.rend(); ++it)
        AddLineElement(m_pLog, (*it)->rml, kLogView);
    m_iScrollLog = 2;
}

void
RoseRmlChat::SelectTab(int iTab) {
    if (iTab == m_iTab)
        return;
    m_iTab = iTab;
    RefreshTabs();
    RebuildLog();
}

void
RoseRmlChat::RefreshTabs() {
    for (size_t i = 0; i < m_Tabs.size(); ++i)
        m_Tabs[i].on = m_Tabs[i].index == m_iTab;
    m_Model.DirtyVariable("tabs");
}

/// --- the filter menu --------------------------------------------------------------

void
RoseRmlChat::OpenMenu(int iTab) {
    if (iTab < 0 || iTab >= kTabCount)
        return;
    m_iMenuTab = iTab;
    m_strMenuTab = kTabLabels[iTab];
    m_bMenu = true;
    RefreshMenu();
    m_Model.DirtyVariable("menu");
    m_Model.DirtyVariable("menu_tab");
}

void
RoseRmlChat::RefreshMenu() {
    for (size_t f = 0; f < m_Filters.size(); ++f) {
        m_Filters[f].locked = (int)f == kTabOwn[m_iMenuTab];
        m_Filters[f].on = m_Filters[f].locked || m_bShow[m_iMenuTab][f];
    }
    m_Model.DirtyVariable("filters");
}

void
RoseRmlChat::ToggleFilter(int iFilter) {
    if (iFilter < 0 || iFilter >= kFilterCount || iFilter == kTabOwn[m_iMenuTab])
        return;
    m_bShow[m_iMenuTab][iFilter] = !m_bShow[m_iMenuTab][iFilter];
    SaveFilters();
    RefreshMenu();
    if (m_iMenuTab == m_iTab)
        RebuildLog();
}

/// [UI_CHAT] TAB<n> = one digit per kind of line ( the classic defaults
/// otherwise: All shows everything, the others notices and their channel ).
void
RoseRmlChat::LoadFilters() {
    for (int t = 0; t < kTabCount; ++t) {
        for (int f = 0; f < kFilterCount; ++f)
            m_bShow[t][f] = (t == 0) || f == CChatDLG::FILTER_SYSTEM;
        char szKey[16], szBuf[32] = {0};
        _snprintf(szKey, sizeof(szKey), "TAB%d", t);
        szKey[sizeof(szKey) - 1] = '\0';
        GetPrivateProfileStringA("UI_CHAT", szKey, "", szBuf, sizeof(szBuf), kIniPath);
        if ((int)strlen(szBuf) < kFilterCount)
            continue;
        for (int f = 0; f < kFilterCount; ++f)
            m_bShow[t][f] = szBuf[f] == '1';
    }
}

void
RoseRmlChat::SaveFilters() {
    for (int t = 0; t < kTabCount; ++t) {
        char szKey[16], szBuf[16];
        _snprintf(szKey, sizeof(szKey), "TAB%d", t);
        szKey[sizeof(szKey) - 1] = '\0';
        for (int f = 0; f < kFilterCount; ++f)
            szBuf[f] = m_bShow[t][f] ? '1' : '0';
        szBuf[kFilterCount] = '\0';
        WritePrivateProfileStringA("UI_CHAT", szKey, szBuf, kIniPath);
    }
}

/// --- the input ----------------------------------------------------------------------

std::string
RoseRmlChat::GetInput() const {
    if (m_pInput == NULL)
        return std::string();
    return ToGame(m_pInput->GetAttribute<Rml::String>("value", ""));
}

void
RoseRmlChat::SetInput(const Rml::String& strUtf8) {
    if (m_pInput == NULL)
        return;
    m_pInput->SetAttribute("value", strUtf8);
    /// The caret after what is there ( "@name " is typed on from its end ).
    if (Rml::ElementFormControlInput* pInput = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(m_pInput))
        pInput->SetSelectionRange((int)strUtf8.size(), (int)strUtf8.size());
}

bool
RoseRmlChat::AppendInput(const char* pszText) {
    if (m_pInput == NULL || pszText == NULL || !m_bVisible)
        return false;
    const Rml::String strValue =
        m_pInput->GetAttribute<Rml::String>("value", "") + RoseRmlText::FromGame(pszText);
    if ((int)strValue.size() > kInputMax)
        return false;
    SetInput(strValue);
    FocusInput();
    return true;
}

bool
RoseRmlChat::FocusInput() {
    if (m_pInput == NULL || !m_bVisible)
        return false;
    m_pInput->Focus();
    /// One caret at a time: the classic box lets go.
    if (CTEditBox::s_pFocusEdit != NULL)
        CTEditBox::s_pFocusEdit->SetFocus(false);
    const Rml::String strValue = m_pInput->GetAttribute<Rml::String>("value", "");
    if (Rml::ElementFormControlInput* pInput = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(m_pInput))
        pInput->SetSelectionRange((int)strValue.size(), (int)strValue.size());
    return true;
}

/// Enter: the classic send path, then what the input holds next.
void
RoseRmlChat::Send() {
    CChatDLG* pDlg = g_itMGR.GetChatDLG();
    if (pDlg == NULL || m_pInput == NULL)
        return;
    const std::string strLine = ToGame(m_pInput->GetAttribute<Rml::String>("value", ""));
    std::string strNext;
    if (!pDlg->SendLine(strLine.c_str(), strNext))
        return; /// blocked or too long: the line stays to be edited
    if (strNext.empty())
        strNext = kTabPrefix[m_iTab];
    SetInput(RoseRmlText::FromGame(strNext.c_str()));
    m_iScrollLog = 2; /// sending always shows the end, as the classic window
}

/// --- links ----------------------------------------------------------------------------

/// Alt+click on an item link: the equipment preview ( CChatDLG's ).
void
RoseRmlChat::OnLogPress(Rml::Event& ev) {
    if (GetAsyncKeyState(VK_MENU) >= 0 || ev.GetParameter<int>("button", 0) != 0)
        return;
    Rml::Element* pLink = LinkAt(ev.GetTargetElement());
    tagITEM sItem;
    if (pLink != NULL && ItemFromHex(pLink->GetAttribute<Rml::String>("item", ""), sItem))
        g_UIMed.OpenItemPreview(sItem);
}

void
RoseRmlChat::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging() || m_bResizing)
        return;
    Rml::Element* pLink = LinkAt(RoseRmlLayout::HoverIn(m_pContext, m_pDocument));
    tagITEM sItem;
    if (pLink == NULL || !ItemFromHex(pLink->GetAttribute<Rml::String>("item", ""), sItem))
        return;

    CItem TipItem(&sItem);
    CInfo Info;
    DWORD dwInfoType = 0;
    if (GetAsyncKeyState(VK_RBUTTON) < 0)
        dwInfoType |= INFO_STATUS_DETAIL;
    TipItem.GetToolTip(Info, 0, dwInfoType);
    if (!Info.IsEmpty())
        RoseRmlUi::PlaceTooltipAtCursor(Info);
}

/// --- size -----------------------------------------------------------------------------

void
RoseRmlChat::ApplySize(float fW, float fH) {
    m_fW = floorf(max(kMinW, min(kMaxW, fW)));
    m_fH = floorf(max(kMinH, min(kMaxH, fH)));
    if (m_pPanel == NULL)
        return;
    m_pPanel->SetProperty(Rml::PropertyId::Width, Rml::Property(m_fW, Rml::Unit::DP));
    m_pPanel->SetProperty(Rml::PropertyId::Height, Rml::Property(m_fH, Rml::Unit::DP));
}

void
RoseRmlChat::LoadSize() {
    char szBuf[32] = {0};
    GetPrivateProfileStringA("UI_LAYOUT", "chat_size", "", szBuf, sizeof(szBuf), kIniPath);
    int w = 0, h = 0;
    if (szBuf[0] != '\0' && sscanf(szBuf, "%d,%d", &w, &h) == 2)
        ApplySize((float)w, (float)h);
    else
        ApplySize(kDefaultW, kDefaultH);
}

/// The grip at the top right: wider to the right, taller upwards ( the
/// bottom edge stays where it is ).
void
RoseRmlChat::UpdateResize() {
    if (!m_bResizing || m_pPanel == NULL)
        return;

    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const float fRatio = RoseRmlLayout::GetScaleRatio();
    ApplySize(m_fPressW + (ptMouse.x - m_iPressX) / fRatio, m_fPressH - (ptMouse.y - m_iPressY) / fRatio);
    const float fTop = floorf(m_fPressTop + (m_fPressH - m_fH) * fRatio);
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(max(0.0f, fTop), Rml::Unit::PX));

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_bResizing = false;
        char szBuf[32];
        _snprintf(szBuf, sizeof(szBuf), "%d,%d", (int)m_fW, (int)m_fH);
        szBuf[sizeof(szBuf) - 1] = '\0';
        WritePrivateProfileStringA("UI_LAYOUT", "chat_size", szBuf, kIniPath);
        RoseRmlLayout::SavePosition(m_pPanel, "chat");
    }
}

void
RoseRmlChat::PlaceDefault() {
    /// Bottom left, where the classic chat sat -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.y <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fGap = floorf(4.0f * RoseRmlLayout::GetScaleRatio());
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fGap, Rml::Unit::PX));
    m_pPanel->SetProperty(
        Rml::PropertyId::Top, Rml::Property(max(0.0f, floorf((float)view.y - size.y - fGap)), Rml::Unit::PX));
}

/// --- per frame ------------------------------------------------------------------------

void
RoseRmlChat::Update() {
    if (m_pDocument == NULL)
        return;

    SetVisible(IsInWorld());
    if (!m_bVisible)
        return;

    PlaceDefault();

    /// The Options chat mode, on the input ( RoseRmlUi's key routing reads it ).
    const bool bAuto = it_GetKeyboardInputType() == CTControlMgr::INPUTTYPE_AUTOENTER;
    if (bAuto != m_bAutoEnter && m_pInput != NULL) {
        m_bAutoEnter = bAuto;
        if (bAuto) {
            m_pInput->SetAttribute("keep-focus", "1");
            m_pInput->SetAttribute("esc-to-game", "1");
        } else {
            m_pInput->RemoveAttribute("keep-focus");
            m_pInput->RemoveAttribute("esc-to-game");
        }
    }
    /// "Always typing": the input has the keyboard whenever nothing else does.
    if (bAuto && !RoseRmlUi::HasFocusedTextField() && CTEditBox::s_pFocusEdit == NULL
        && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0 && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0)
        FocusInput();

    /// The filter menu closes on a click anywhere else ( a tab keeps it: the
    /// menu follows to that tab ).
    if (m_bMenu && ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000))) {
        bool bInMenu = false;
        for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
             pEl = pEl->GetParentNode()) {
            if (pEl->GetId() == "filtermenu" || pEl->HasAttribute("filterbtn")
                || pEl->HasAttribute("chattab")) {
                bInMenu = true;
                break;
            }
        }
        if (!bInMenu) {
            m_bMenu = false;
            m_Model.DirtyVariable("menu");
        }
    }

    UpdateResize();

    /// Follow the end after new lines ( laid out by the last frame's update ).
    if (m_iScrollLog > 0 && m_pLog != NULL) {
        ScrollToEnd(m_pLog);
        --m_iScrollLog;
    }
    if (m_iScrollSystem > 0 && m_pSystem != NULL) {
        ScrollToEnd(m_pSystem);
        --m_iScrollSystem;
    }

    const Rml::Vector2i view = m_pContext->GetDimensions();
    if (!m_bResizing)
        RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateTooltip();
}
