#include "stdafx.h"

#include "RoseRmlNotifyButtons.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CObjUSER.h"
#include "../Game.h"
#include "../System/CGame.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/Dlgs/NotifyButtonDlg.h"
#include "tgamectrl/resourcemgr.h"

#include "rose/common/log.h"

#include <math.h>

RoseRmlNotifyButtons::RoseRmlNotifyButtons():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false) {}

bool
RoseRmlNotifyButtons::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("notify");
    if (!constructor)
        return false;

    if (auto button = constructor.RegisterStruct<ButtonVM>()) {
        button.RegisterMember("event", &ButtonVM::event);
        button.RegisterMember("used", &ButtonVM::used);
    }
    constructor.RegisterArray<std::vector<ButtonVM>>();

    constructor.Bind("buttons", &m_Buttons);
    constructor.Bind("icon_src", &m_strIconSrc);
    constructor.Bind("icon_rect", &m_strIconRect);
    constructor.Bind("blink_src", &m_strBlinkSrc);
    constructor.Bind("blink_rect", &m_strBlinkRect);
    constructor.Bind("over_src", &m_strOverSrc);
    constructor.Bind("over_rect", &m_strOverRect);

    constructor.BindEventCallback("activate",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            if (CNotifyButtonDlg* pDlg = g_itMGR.GetNotifyButtonDlg())
                pDlg->Activate(args[0].Get<int>());
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "notify.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load notify buttons document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("notify");
    RoseRmlLayout::Track(m_pPanel, "notify");

    LOG_INFO("[rmlui] notify buttons document loaded");
    return true;
}

void
RoseRmlNotifyButtons::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

bool
RoseRmlNotifyButtons::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlNotifyButtons::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlNotifyButtons::PlaceDefault() {
    /// Bottom left, above the chat, where the classic strip sat ( its
    /// UpdatePosition: 10 px in, 360 px up ) -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fScale = RoseRmlLayout::GetScaleRatio();
    const float fTop = floorf((float)view.y - 360.0f * fScale - size.y);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(floorf(10.0f * fScale), Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop > 0.0f ? fTop : 0.0f, Rml::Unit::PX));
}

void
RoseRmlNotifyButtons::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("event")) {
            CInfo ToolTip;
            ToolTip.Clear();
            ToolTip.AddString("A new tip. Click to read it.");
            RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
            return;
        }
    }
}

void
RoseRmlNotifyButtons::Update() {
    if (m_pDocument == NULL)
        return;

    CNotifyButtonDlg* pDlg = g_itMGR.GetNotifyButtonDlg();
    const int iCount = (IsInWorld() && pDlg != NULL) ? pDlg->GetCount() : 0;

    SetVisible(iCount > 0);
    if (!m_bVisible)
        return;

    /// The button art: the classic sprites ( AddNotifybutton's ), once the UI
    /// atlas is known -- drawn as they are, no frame.
    if (m_strIconSrc.empty()) {
        struct { const char* pszSprite; Rml::String* pSrc; Rml::String* pRect; } kArt[] = {
            {"UI13_BTN_EVENTNOTIFY_NORMAL", &m_strIconSrc, &m_strIconRect},
            {"UI13_BTN_EVENTNOTIFY_BLINK", &m_strBlinkSrc, &m_strBlinkRect},
            {"UI13_BTN_EVENTNOTIFY_OVER", &m_strOverSrc, &m_strOverRect},
        };
        for (int i = 0; i < 3; ++i) {
            const int iGid = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, kArt[i].pszSprite);
            RoseRmlIcons::Resolve(IMAGE_RES_UI, iGid, *kArt[i].pSrc, *kArt[i].pRect);
        }
        const char* kVars[] = {"icon_src", "icon_rect", "blink_src", "blink_rect", "over_src", "over_rect"};
        for (int i = 0; i < 6; ++i)
            m_Model.DirtyVariable(kVars[i]);
    }

    /// Grow-only rows, hidden when unused.
    std::vector<ButtonVM> buttons = m_Buttons;
    if ((int)buttons.size() < iCount)
        buttons.resize(iCount);
    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].used = (int)i < iCount;
        buttons[i].event = buttons[i].used ? pDlg->GetEventAt((int)i) : 0;
    }
    if (buttons != m_Buttons) {
        m_Buttons.swap(buttons);
        m_Model.DirtyVariable("buttons");
    }

    PlaceDefault();
    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateTooltip();
}
