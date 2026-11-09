#include "stdafx.h"

#include "RoseRmlZoneTimers.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include "../CObjUSER.h"
#include "../Game.h"
#include "../System/CGame.h"
#include "../zoneobjects.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>

namespace {

const int kSoonSeconds = 60; ///< under this the time is drawn in the accent colour

Rml::String
FormatTime(int iSeconds) {
    char szBuf[32];
    if (iSeconds >= 3600)
        sprintf_s(szBuf, "%d:%02d:%02d", iSeconds / 3600, (iSeconds % 3600) / 60, iSeconds % 60);
    else
        sprintf_s(szBuf, "%d:%02d", iSeconds / 60, iSeconds % 60);
    return szBuf;
}

} // namespace

RoseRmlZoneTimers::RoseRmlZoneTimers():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false) {}

bool
RoseRmlZoneTimers::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("zonetimers");
    if (!constructor)
        return false;

    if (auto row = constructor.RegisterStruct<RowVM>()) {
        row.RegisterMember("label", &RowVM::label);
        row.RegisterMember("time", &RowVM::time);
        row.RegisterMember("soon", &RowVM::soon);
        row.RegisterMember("used", &RowVM::used);
    }
    constructor.RegisterArray<std::vector<RowVM>>();
    constructor.Bind("rows", &m_Rows);

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "timers.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load zone timers document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("timers");
    RoseRmlLayout::Track(m_pPanel, "timers");

    LOG_INFO("[rmlui] zone timers document loaded");
    return true;
}

void
RoseRmlZoneTimers::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

bool
RoseRmlZoneTimers::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlZoneTimers::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlZoneTimers::PlaceDefault() {
    /// Under the minimap, right-aligned with it -- only while no position is
    /// set ( dragged, saved or reset ). Without a minimap on screen, the top
    /// right corner, a minimap's height down.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fScale = RoseRmlLayout::GetScaleRatio();
    const float fGap = 3.0f * fScale;
    float fLeft = (float)view.x - size.x - fGap;
    float fTop = 240.0f * fScale;
    for (int i = 0; i < m_pContext->GetNumDocuments(); ++i) {
        Rml::ElementDocument* pDoc = m_pContext->GetDocument(i);
        Rml::Element* pMap = (pDoc != NULL && pDoc->IsVisible()) ? pDoc->GetElementById("minimap") : NULL;
        if (pMap == NULL)
            continue;
        const Rml::Vector2f mapPos = pMap->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f mapSize = pMap->GetBox().GetSize(Rml::BoxArea::Border);
        if (mapSize.x > 0.0f) {
            fLeft = mapPos.x + mapSize.x - size.x;
            fTop = mapPos.y + mapSize.y + fGap;
        }
        break;
    }
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(floorf(fLeft > 0.0f ? fLeft : 0.0f), Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(floorf(fTop), Rml::Unit::PX));
}

void
RoseRmlZoneTimers::Update() {
    if (m_pDocument == NULL)
        return;

    std::vector<CZoneTimers::Entry> timers;
    if (IsInWorld())
        CZoneTimers::Instance().List(timers);

    SetVisible(!timers.empty());
    if (!m_bVisible)
        return;

    /// Grow-only rows, hidden when unused. The text only changes once a second,
    /// so the model is dirtied at most that often.
    std::vector<RowVM> rows = m_Rows;
    if (rows.size() < timers.size())
        rows.resize(timers.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        RowVM& row = rows[i];
        row.used = i < timers.size();
        if (row.used) {
            row.label = timers[i].label.c_str();
            row.time = FormatTime(timers[i].iSecondsLeft);
            row.soon = timers[i].iSecondsLeft < kSoonSeconds;
        } else {
            row.label.clear();
            row.time.clear();
            row.soon = false;
        }
    }
    if (rows != m_Rows) {
        m_Rows.swap(rows);
        m_Model.DirtyVariable("rows");
    }

    PlaceDefault();
    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
