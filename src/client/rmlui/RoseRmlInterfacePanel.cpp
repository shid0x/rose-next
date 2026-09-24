#include "stdafx.h"

#include "RoseRmlInterfacePanel.h"
#include "RoseRmlLayout.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../Object.h"
#include "../System/CGame.h"

#include "rose/common/log.h"

namespace {

/// The scale presets offered. Anything in RoseRmlLayout's range also works
/// from the INI; these are the steps worth a button.
const int kScalePresets[] = {90, 100, 110, 125, 150, 175, 200};

} // namespace

RoseRmlInterfacePanel::RoseRmlInterfacePanel():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_iScale(100),
    m_bLocked(false),
    m_bBarVertical(false) {
    for (int iPreset : kScalePresets)
        m_Scales.push_back(iPreset);
}

bool
RoseRmlInterfacePanel::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("interface");
    if (!constructor)
        return false;

    constructor.RegisterArray<std::vector<int>>();
    constructor.Bind("scales", &m_Scales);
    constructor.Bind("scale", &m_iScale);
    constructor.Bind("locked", &m_bLocked);
    constructor.Bind("bar_vertical", &m_bBarVertical);

    constructor.BindEventCallback("set_scale",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            RoseRmlLayout::SetScale(args[0].Get<int>());
            Refresh();
        });
    constructor.BindEventCallback("toggle_lock",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            RoseRmlLayout::SetLocked(!RoseRmlLayout::IsLocked());
            Refresh();
        });
    constructor.BindEventCallback("set_bar",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            RoseRmlUi::SetSkillBarVertical(args[0].Get<bool>());
            Refresh();
        });
    constructor.BindEventCallback("reset_layout",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            RoseRmlLayout::Reset();
        });
    constructor.BindEventCallback("classic",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            /// Live, since this window only exists while RmlUi is up. The way
            /// back is Options > Play > "Use new interface", or /ui2.
            RoseUi2::SetActive(false);
            Hide();
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Hide(); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "interface.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load interface panel document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("interface");
    RoseRmlLayout::Track(m_pPanel, "interface");

    LOG_INFO("[rmlui] interface panel document loaded");
    return true;
}

void
RoseRmlInterfacePanel::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

void
RoseRmlInterfacePanel::Refresh() {
    m_iScale = RoseRmlLayout::GetScale();
    m_bLocked = RoseRmlLayout::IsLocked();
    m_bBarVertical = RoseRmlUi::IsSkillBarVertical();
    m_Model.DirtyVariable("scale");
    m_Model.DirtyVariable("locked");
    m_Model.DirtyVariable("bar_vertical");
}

void
RoseRmlInterfacePanel::Show() {
    if (m_pDocument == NULL)
        return;
    Refresh();
    m_pDocument->Show();
    m_bVisible = true;
}

void
RoseRmlInterfacePanel::Hide() {
    if (m_pDocument == NULL)
        return;
    m_pDocument->Hide();
    m_bVisible = false;
}

void
RoseRmlInterfacePanel::Toggle() {
    if (m_bVisible)
        Hide();
    else
        Show();
}

void
RoseRmlInterfacePanel::Update() {
    if (!m_bVisible || m_pDocument == NULL)
        return;

    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
    if (!bInWorld) {
        Hide();
        return;
    }

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
