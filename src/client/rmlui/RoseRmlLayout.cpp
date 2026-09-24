#include "stdafx.h"

#include "RoseRmlLayout.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>

#include "rose/common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace {

const char* kIniPath = ".\\rose-next.ini";
const char* kSection = "UI_LAYOUT";

Rml::Context* g_pContext = NULL;
int g_iScale = 100;
bool g_bLocked = false;

struct TrackedPanel {
    Rml::Element* pPanel;
    std::string strKey;
};
std::vector<TrackedPanel> g_Tracked;

void
SetPosition(Rml::Element* pPanel, float x, float y) {
    pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
    pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
}

int
ReadVideoInt(const char* pszKey, int iDefault) {
    char szBuf[16] = {0};
    GetPrivateProfileStringA("VIDEO", pszKey, "", szBuf, sizeof(szBuf), kIniPath);
    return szBuf[0] ? atoi(szBuf) : iDefault;
}

void
WriteVideoInt(const char* pszKey, int iValue) {
    char szBuf[16];
    _snprintf(szBuf, sizeof(szBuf), "%d", iValue);
    szBuf[sizeof(szBuf) - 1] = '\0';
    WritePrivateProfileStringA("VIDEO", pszKey, szBuf, kIniPath);
}

/// Saves the panel's position when a drag inside it ends. dragend is
/// dispatched to the handle and bubbles, so listening on the panel catches a
/// handle anywhere inside it. Owned by the element: deletes itself on detach.
class SaveOnDragEnd: public Rml::EventListener {
public:
    explicit SaveOnDragEnd(const char* pszKey): m_strKey(pszKey) {}

    void ProcessEvent(Rml::Event& ev) override {
        Rml::Element* pPanel = ev.GetCurrentElement();
        if (pPanel == NULL)
            return;

        const Rml::Vector2f pos = pPanel->GetAbsoluteOffset(Rml::BoxArea::Border);
        char szBuf[32];
        _snprintf(szBuf, sizeof(szBuf), "%d,%d", (int)pos.x, (int)pos.y);
        szBuf[sizeof(szBuf) - 1] = '\0';
        WritePrivateProfileStringA(kSection, m_strKey.c_str(), szBuf, kIniPath);
    }

    void OnDetach(Rml::Element*) override { delete this; }

private:
    std::string m_strKey;
};

/// <handle> sets drag:drag as an *inline* property in its constructor, which
/// no stylesheet can override, so the lock has to flip it element by element.
void
ApplyLock() {
    if (g_pContext == NULL)
        return;

    for (int i = 0; i < g_pContext->GetNumDocuments(); ++i) {
        Rml::ElementDocument* pDoc = g_pContext->GetDocument(i);
        if (pDoc == NULL)
            continue;

        /// For the stylesheets ( cursor ): body.locked.
        pDoc->SetClass("locked", g_bLocked);

        Rml::ElementList handles;
        pDoc->GetElementsByTagName(handles, "handle");
        for (Rml::Element* pHandle : handles) {
            pHandle->SetProperty(Rml::PropertyId::Drag,
                Rml::Property(g_bLocked ? Rml::Style::Drag::None : Rml::Style::Drag::Drag));
        }
    }
}

} // namespace

namespace RoseRmlLayout {

void
Initialise(Rml::Context* pContext) {
    g_pContext = pContext;

    g_iScale = ReadVideoInt("UI_SCALE", 100);
    if (g_iScale < kMinScale || g_iScale > kMaxScale)
        g_iScale = 100;
    if (g_pContext != NULL)
        g_pContext->SetDensityIndependentPixelRatio(g_iScale / 100.0f);

    g_bLocked = ReadVideoInt("UI_LOCK", 0) != 0;
    ApplyLock();

    LOG_INFO("[rmlui] ui scale {}%, panels {}", g_iScale, g_bLocked ? "locked" : "movable");
}

void
Shutdown() {
    g_pContext = NULL;
    g_Tracked.clear();
}

void
Track(Rml::Element* pPanel, const char* pszKey) {
    if (pPanel == NULL || pszKey == NULL)
        return;

    char szBuf[32] = {0};
    GetPrivateProfileStringA(kSection, pszKey, "", szBuf, sizeof(szBuf), kIniPath);
    int x = 0, y = 0;
    if (szBuf[0] != '\0' && sscanf(szBuf, "%d,%d", &x, &y) == 2)
        SetPosition(pPanel, (float)x, (float)y);

    pPanel->AddEventListener(Rml::EventId::Dragend, new SaveOnDragEnd(pszKey));

    TrackedPanel tracked;
    tracked.pPanel = pPanel;
    tracked.strKey = pszKey;
    g_Tracked.push_back(tracked);
}

void
Clamp(Rml::Element* pPanel, int iViewportW, int iViewportH) {
    if (pPanel == NULL)
        return;

    /// Zero until the first layout; nothing to clamp against yet.
    const Rml::Vector2f size = pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return;

    const Rml::Vector2f pos = pPanel->GetAbsoluteOffset(Rml::BoxArea::Border);
    const float fMaxX = max(0.0f, (float)iViewportW - size.x);
    const float fMaxY = max(0.0f, (float)iViewportH - size.y);

    float x = pos.x;
    float y = pos.y;
    if (x < 0.0f)
        x = 0.0f;
    if (x > fMaxX)
        x = fMaxX;
    if (y < 0.0f)
        y = 0.0f;
    if (y > fMaxY)
        y = fMaxY;

    if (x != pos.x || y != pos.y)
        SetPosition(pPanel, x, y);
}

void
Reset() {
    for (const TrackedPanel& tracked : g_Tracked) {
        /// A drag or a restore writes left/top inline; removing them hands the
        /// panel back to its stylesheet.
        tracked.pPanel->RemoveProperty(Rml::PropertyId::Left);
        tracked.pPanel->RemoveProperty(Rml::PropertyId::Top);
        WritePrivateProfileStringA(kSection, tracked.strKey.c_str(), NULL, kIniPath);
    }
    LOG_INFO("[rmlui] panel layout reset ({} panels)", g_Tracked.size());
}

void
SetScale(int iPercent) {
    if (iPercent < kMinScale)
        iPercent = kMinScale;
    if (iPercent > kMaxScale)
        iPercent = kMaxScale;

    g_iScale = iPercent;
    if (g_pContext != NULL)
        g_pContext->SetDensityIndependentPixelRatio(g_iScale / 100.0f);
    WriteVideoInt("UI_SCALE", g_iScale);
}

int
GetScale() {
    return g_iScale;
}

float
GetScaleRatio() {
    return g_iScale / 100.0f;
}

void
SetLocked(bool bLocked) {
    g_bLocked = bLocked;
    ApplyLock();
    WriteVideoInt("UI_LOCK", g_bLocked ? 1 : 0);
}

bool
IsLocked() {
    return g_bLocked;
}

} // namespace RoseRmlLayout
