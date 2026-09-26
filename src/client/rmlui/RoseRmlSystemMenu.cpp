#include "stdafx.h"

#include "RoseRmlSystemMenu.h"
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
#include "../interface/Dlgs/CSystemDLG.h"

#include "rose/common/log.h"

#include <math.h>

RoseRmlSystemMenu::RoseRmlSystemMenu():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false) {}

bool
RoseRmlSystemMenu::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("sysmenu");
    if (!constructor)
        return false;

    constructor.BindEventCallback("resume",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_SYSTEM);
        });
    constructor.BindEventCallback("char_select",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Leave(true); });
    constructor.BindEventCallback("exit_game",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Leave(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "sysmenu.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load system menu document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("sysmenu");
    RoseRmlLayout::Track(m_pPanel, "sysmenu");

    LOG_INFO("[rmlui] system menu document loaded");
    return true;
}

void
RoseRmlSystemMenu::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

bool
RoseRmlSystemMenu::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlSystemMenu::SetOpen(bool bOpen) {
    if (bOpen == m_bOpen)
        return;
    RoseUi2::PlayWindowSound(DLG_TYPE_SYSTEM, bOpen);
    m_bOpen = bOpen;
}

void
RoseRmlSystemMenu::Leave(bool bCharacterSelect) {
    /// CSystemDLG's buttons: the window closes, then the request ( or the
    /// "not while fighting" line in the chat ).
    g_itMGR.CloseDialog(DLG_TYPE_SYSTEM);
    CSystemDLG::RequestLeave(bCharacterSelect);
}

void
RoseRmlSystemMenu::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlSystemMenu::PlaceDefault() {
    /// Centred, a little high -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fLeft = floorf(((float)view.x - size.x) * 0.5f);
    const float fTop = floorf(((float)view.y - size.y) * 0.4f);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlSystemMenu::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen && !IsInWorld())
        m_bOpen = false;

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    PlaceDefault();
    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
