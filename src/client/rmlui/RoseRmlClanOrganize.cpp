#include "stdafx.h"

#include "RoseRmlClanOrganize.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CObjUSER.h"
#include "../Game.h"
#include "../System/CGame.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Dlgs/ClanOrganizeDlg.h"
#include "tgamectrl/teditbox.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdlib.h>

namespace {

const int kNeedLevel = 30;
const __int64 kNeedMoney = 1000000;

/// "12,345,678".
Rml::String
Thousands(__int64 iValue) {
    char szRaw[32];
    _snprintf(szRaw, sizeof(szRaw), "%I64d", iValue < 0 ? -iValue : iValue);
    szRaw[sizeof(szRaw) - 1] = '\0';

    const int iLen = (int)strlen(szRaw);
    Rml::String out = iValue < 0 ? "-" : "";
    for (int i = 0; i < iLen; ++i) {
        if (i > 0 && ((iLen - i) % 3) == 0)
            out += ',';
        out += szRaw[i];
    }
    return out;
}

/// UTF-8 ( a field's value ) -> the game's code page, for the server.
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

std::string
Trimmed(const std::string& str) {
    size_t b = 0, e = str.size();
    while (b < e && str[b] == ' ')
        ++b;
    while (e > b && str[e - 1] == ' ')
        --e;
    return str.substr(b, e - b);
}

/// Sprite 0 of each sheet is not a choice ( CClanOrganizeDlg::Create starts
/// at 1 ).
int
SpriteCount(int iModule) {
    CImageRes* pRes = CImageResManager::GetSingleton().GetImageRes(iModule);
    return pRes ? pRes->GetSpriteCount() : 0;
}

} // namespace

RoseRmlClanOrganize::RoseRmlClanOrganize():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bFocusName(false),
    m_iBack(0),
    m_iCenter(0),
    m_iMyLevel(0),
    m_bLevelOk(false),
    m_bMoneyOk(false) {}

bool
RoseRmlClanOrganize::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("clanorganize");
    if (!constructor)
        return false;

    if (auto mark = constructor.RegisterStruct<MarkVM>()) {
        mark.RegisterMember("index", &MarkVM::index);
        mark.RegisterMember("src", &MarkVM::src);
        mark.RegisterMember("rect", &MarkVM::rect);
        mark.RegisterMember("on", &MarkVM::on);
    }
    constructor.RegisterArray<std::vector<MarkVM>>();

    constructor.Bind("backs", &m_Backs);
    constructor.Bind("centers", &m_Centers);
    constructor.Bind("back_src", &m_strBackSrc);
    constructor.Bind("back_rect", &m_strBackRect);
    constructor.Bind("center_src", &m_strCenterSrc);
    constructor.Bind("center_rect", &m_strCenterRect);
    constructor.Bind("my_level", &m_iMyLevel);
    constructor.Bind("my_money", &m_strMyMoney);
    constructor.Bind("level_ok", &m_bLevelOk);
    constructor.Bind("money_ok", &m_bMoneyOk);

    constructor.BindEventCallback("pick_back",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                Pick(true, args[0].Get<int>());
        });
    constructor.BindEventCallback("pick_center",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                Pick(false, args[0].Get<int>());
        });
    constructor.BindEventCallback("found",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnFound(); });
    /// Enter in either field founds, as the Found button.
    constructor.BindEventCallback("edited",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<bool>("linebreak", false))
                OnFound();
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_CLAN_ORGANIZE);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "clanorganize.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load clan organize document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("clanorganize");
    RoseRmlLayout::Track(m_pPanel, "clanorganize");

    LOG_INFO("[rmlui] clan organize document loaded");
    return true;
}

void
RoseRmlClanOrganize::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

bool
RoseRmlClanOrganize::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

/// The two sheets' choices ( once: the sheets load with the UI resources ).
void
RoseRmlClanOrganize::BuildSheets() {
    if (!m_Backs.empty() && !m_Centers.empty())
        return;
    struct { std::vector<MarkVM>* pList; int iModule; const char* pszVar; } sheets[] = {
        {&m_Backs, IMAGE_RES_CLANBACK, "backs"},
        {&m_Centers, IMAGE_RES_CLANCENTER, "centers"},
    };
    for (int s = 0; s < 2; ++s) {
        std::vector<MarkVM>& list = *sheets[s].pList;
        list.clear();
        const int iCount = SpriteCount(sheets[s].iModule);
        for (int i = 1; i < iCount; ++i) {
            MarkVM vm;
            vm.index = i;
            vm.on = false;
            if (RoseRmlIcons::Resolve(sheets[s].iModule, i, vm.src, vm.rect))
                list.push_back(vm);
        }
        m_Model.DirtyVariable(sheets[s].pszVar);
    }
}

void
RoseRmlClanOrganize::Pick(bool bBack, int iIndex) {
    std::vector<MarkVM>& list = bBack ? m_Backs : m_Centers;
    const MarkVM* pPicked = NULL;
    for (size_t i = 0; i < list.size(); ++i) {
        list[i].on = list[i].index == iIndex;
        if (list[i].on)
            pPicked = &list[i];
    }
    if (pPicked == NULL)
        return;
    if (bBack) {
        m_iBack = iIndex;
        m_strBackSrc = pPicked->src;
        m_strBackRect = pPicked->rect;
        m_Model.DirtyVariable("backs");
        m_Model.DirtyVariable("back_src");
        m_Model.DirtyVariable("back_rect");
    } else {
        m_iCenter = iIndex;
        m_strCenterSrc = pPicked->src;
        m_strCenterRect = pPicked->rect;
        m_Model.DirtyVariable("centers");
        m_Model.DirtyVariable("center_src");
        m_Model.DirtyVariable("center_rect");
    }
}

void
RoseRmlClanOrganize::SetOpen(bool bOpen) {
    if (bOpen == m_bOpen)
        return;
    RoseUi2::PlayWindowSound(DLG_TYPE_CLAN_ORGANIZE, bOpen);
    m_bOpen = bOpen;
    if (!bOpen || m_pDocument == NULL)
        return;

    /// A fresh form each time, with a random emblem as the classic dialog.
    BuildSheets();
    if (!m_Backs.empty())
        Pick(true, m_Backs[rand() % m_Backs.size()].index);
    if (!m_Centers.empty())
        Pick(false, m_Centers[rand() % m_Centers.size()].index);
    const char* kFields[] = {"cname", "cslogan"};
    for (int i = 0; i < 2; ++i) {
        if (Rml::Element* pField = m_pDocument->GetElementById(kFields[i]))
            pField->SetAttribute("value", Rml::String());
    }
    m_bFocusName = true;
    Sample();
}

void
RoseRmlClanOrganize::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlClanOrganize::Sample() {
    if (g_pAVATAR == NULL)
        return;
    const int iLevel = g_pAVATAR->Get_LEVEL();
    if (iLevel != m_iMyLevel) {
        m_iMyLevel = iLevel;
        m_Model.DirtyVariable("my_level");
    }
    const __int64 i64Money = g_pAVATAR->Get_MONEY();
    const Rml::String strMoney = Thousands(i64Money);
    if (strMoney != m_strMyMoney) {
        m_strMyMoney = strMoney;
        m_Model.DirtyVariable("my_money");
    }
    const bool bLevelOk = iLevel >= kNeedLevel;
    if (bLevelOk != m_bLevelOk) {
        m_bLevelOk = bLevelOk;
        m_Model.DirtyVariable("level_ok");
    }
    const bool bMoneyOk = i64Money >= kNeedMoney;
    if (bMoneyOk != m_bMoneyOk) {
        m_bMoneyOk = bMoneyOk;
        m_Model.DirtyVariable("money_ok");
    }
}

void
RoseRmlClanOrganize::OnFound() {
    if (m_pDocument == NULL)
        return;
    Rml::Element* pName = m_pDocument->GetElementById("cname");
    Rml::Element* pSlogan = m_pDocument->GetElementById("cslogan");
    if (pName == NULL || pSlogan == NULL)
        return;
    const std::string strName = Trimmed(ToGame(pName->GetAttribute<Rml::String>("value", "")));
    const std::string strSlogan = Trimmed(ToGame(pSlogan->GetAttribute<Rml::String>("value", "")));
    /// The classic Confirm did nothing without a name.
    if (strName.empty()) {
        g_itMGR.OpenMsgBox("Name your clan first.");
        return;
    }
    CClanOrganizeDlg::RequestOrganize(m_iBack, m_iCenter, strName.c_str(), strSlogan.c_str());
}

void
RoseRmlClanOrganize::PlaceDefault() {
    /// Centred, a little high -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fLeft = floorf(((float)view.x - size.x) * 0.5f);
    const float fTop = floorf(((float)view.y - size.y) * 0.35f);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlClanOrganize::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen && !IsInWorld())
        SetOpen(false);

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    Sample();
    PlaceDefault();

    if (m_bFocusName) {
        m_bFocusName = false;
        if (Rml::Element* pField = m_pDocument->GetElementById("cname")) {
            pField->Focus();
            if (CTEditBox::s_pFocusEdit != NULL)
                CTEditBox::s_pFocusEdit->SetFocus(false);
        }
    }

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
