#include "stdafx.h"

#include "RoseRmlLayout.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>

#include "rose/common/log.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

const char* kIniPath = ".\\rose-next.ini";
const char* kSection = "UI_LAYOUT";

Rml::Context* g_pContext = NULL;
int g_iScale = 100;
bool g_bLocked = false;

/// A saved spot: the screen point a panel's top-left corner hangs from, per
/// axis ( L / C / R, T / M / B ), and how far from it, in dp. "R-250,T0" is a
/// panel 250 dp left of the right edge, at the top: on a wider screen it is
/// still against the right edge, and at another UI scale its offset scales
/// with it -- so a layout holds on any screen and can be shared as text.
struct Anchor {
    char cX;
    char cY;
    int iX;
    int iY;
};

struct TrackedPanel {
    Rml::Element* pPanel;
    std::string strKey;
    /// anchor holds the saved spot.
    bool bAnchored;
    Anchor anchor;
    /// Saved as "x,y" screen pixels by an older client: converted from where
    /// the panel stands once it has a size ( Update ).
    bool bLegacy;
};
std::vector<TrackedPanel> g_Tracked;

/// What the anchors were last applied for: a new screen size or UI scale
/// puts every anchored panel back on its anchor.
Rml::Vector2i g_LastView(0, 0);
int g_iLastScale = 0;

/// Always whole pixels: a panel on a fractional position samples every image
/// and glyph between two texels, which reads as blur.
void
SetPosition(Rml::Element* pPanel, float x, float y) {
    pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(floorf(x), Rml::Unit::PX));
    pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(floorf(y), Rml::Unit::PX));
}

/// The anchor point on one axis: 0, the middle, or the far edge.
float
Base(char cAnchor, int iSize) {
    if (cAnchor == 'C' || cAnchor == 'M')
        return iSize * 0.5f;
    if (cAnchor == 'R' || cAnchor == 'B')
        return (float)iSize;
    return 0.0f;
}

/// The third of the screen a panel's centre is in names its anchor.
char
Pick(float fCentre, int iSize, const char* pszThree) {
    if (fCentre < iSize / 3.0f)
        return pszThree[0];
    if (fCentre > iSize * 2.0f / 3.0f)
        return pszThree[2];
    return pszThree[1];
}

/// "R-250,T0". An older client's "x,y" ( pixels ) is not one.
bool
ParseAnchor(const char* pszValue, Anchor& out) {
    char cX = 0, cY = 0;
    int x = 0, y = 0;
    if (sscanf(pszValue, " %c%d , %c%d", &cX, &x, &cY, &y) != 4)
        return false;
    cX = (char)toupper((unsigned char)cX);
    cY = (char)toupper((unsigned char)cY);
    if (strchr("LCR", cX) == NULL || strchr("TMB", cY) == NULL)
        return false;
    out.cX = cX;
    out.cY = cY;
    out.iX = x;
    out.iY = y;
    return true;
}

float
ScaleRatio() {
    return g_iScale / 100.0f;
}

/// The anchor of a panel where it stands now ( needs its size: after layout ).
Anchor
AnchorOf(Rml::Element* pPanel, const Rml::Vector2i& view) {
    const Rml::Vector2f pos = pPanel->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    const float fRatio = ScaleRatio();
    Anchor a;
    a.cX = Pick(pos.x + size.x * 0.5f, view.x, "LCR");
    a.cY = Pick(pos.y + size.y * 0.5f, view.y, "TMB");
    a.iX = (int)floorf((pos.x - Base(a.cX, view.x)) / fRatio + 0.5f);
    a.iY = (int)floorf((pos.y - Base(a.cY, view.y)) / fRatio + 0.5f);
    return a;
}

void
WriteAnchor(const char* pszKey, const Anchor& a) {
    char szBuf[32];
    _snprintf(szBuf, sizeof(szBuf), "%c%d,%c%d", a.cX, a.iX, a.cY, a.iY);
    szBuf[sizeof(szBuf) - 1] = '\0';
    WritePrivateProfileStringA(kSection, pszKey, szBuf, kIniPath);
}

void
ApplyAnchor(const TrackedPanel& tracked, const Rml::Vector2i& view) {
    const float fRatio = ScaleRatio();
    SetPosition(tracked.pPanel,
        Base(tracked.anchor.cX, view.x) + tracked.anchor.iX * fRatio,
        Base(tracked.anchor.cY, view.y) + tracked.anchor.iY * fRatio);
}

TrackedPanel*
FindTracked(Rml::Element* pPanel) {
    for (TrackedPanel& tracked : g_Tracked) {
        if (tracked.pPanel == pPanel)
            return &tracked;
    }
    return NULL;
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
        RoseRmlLayout::SavePosition(ev.GetCurrentElement(), m_strKey.c_str());
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
SavePosition(Rml::Element* pPanel, const char* pszKey) {
    if (pPanel == NULL || pszKey == NULL || pPanel->GetContext() == NULL)
        return;
    const Anchor a = AnchorOf(pPanel, pPanel->GetContext()->GetDimensions());
    WriteAnchor(pszKey, a);
    if (TrackedPanel* pTracked = FindTracked(pPanel)) {
        pTracked->anchor = a;
        pTracked->bAnchored = true;
        pTracked->bLegacy = false;
    }
}

void
Update() {
    if (g_pContext == NULL)
        return;
    const Rml::Vector2i view = g_pContext->GetDimensions();
    if (view.x <= 0 || view.y <= 0)
        return;
    const bool bChanged = view != g_LastView || g_iScale != g_iLastScale;
    g_LastView = view;
    g_iLastScale = g_iScale;

    for (TrackedPanel& tracked : g_Tracked) {
        if (tracked.bAnchored) {
            if (bChanged)
                ApplyAnchor(tracked, view);
            continue;
        }
        if (!tracked.bLegacy)
            continue;
        /// The pixels were this screen's: where the panel stands now is what
        /// the player arranged, so its anchor keeps the layout as it is.
        const Rml::Vector2f size = tracked.pPanel->GetBox().GetSize(Rml::BoxArea::Border);
        if (size.x <= 0.0f || size.y <= 0.0f)
            continue;
        tracked.anchor = AnchorOf(tracked.pPanel, view);
        tracked.bAnchored = true;
        tracked.bLegacy = false;
        WriteAnchor(tracked.strKey.c_str(), tracked.anchor);
    }
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

    TrackedPanel tracked;
    tracked.pPanel = pPanel;
    tracked.strKey = pszKey;
    tracked.bAnchored = false;
    tracked.anchor = Anchor();
    tracked.bLegacy = false;

    char szBuf[32] = {0};
    GetPrivateProfileStringA(kSection, pszKey, "", szBuf, sizeof(szBuf), kIniPath);
    int x = 0, y = 0;
    if (szBuf[0] != '\0' && ParseAnchor(szBuf, tracked.anchor)) {
        tracked.bAnchored = true;
        /// Placed now so the panel's own default never applies; Update
        /// places it again once the UI scale is read.
        if (pPanel->GetContext() != NULL)
            ApplyAnchor(tracked, pPanel->GetContext()->GetDimensions());
    } else if (szBuf[0] != '\0' && sscanf(szBuf, "%d,%d", &x, &y) == 2) {
        SetPosition(pPanel, (float)x, (float)y);
        tracked.bLegacy = true;
    }

    pPanel->AddEventListener(Rml::EventId::Dragend, new SaveOnDragEnd(pszKey));
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
    const float fMaxX = max(0.0f, floorf((float)iViewportW - size.x));
    const float fMaxY = max(0.0f, floorf((float)iViewportH - size.y));

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
    for (TrackedPanel& tracked : g_Tracked) {
        /// A drag or a restore writes left/top inline; removing them hands the
        /// panel back to its stylesheet.
        tracked.pPanel->RemoveProperty(Rml::PropertyId::Left);
        tracked.pPanel->RemoveProperty(Rml::PropertyId::Top);
        tracked.bAnchored = false;
        tracked.bLegacy = false;
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
    return ScaleRatio();
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

namespace RoseRmlLayout {

Rml::Element*
HoverIn(Rml::Context* pContext, Rml::Element* pDocument) {
    if (pContext == NULL || pDocument == NULL)
        return NULL;
    Rml::Element* pHover = pContext->GetHoverElement();
    return (pHover != NULL && pHover->GetOwnerDocument() == pDocument) ? pHover : NULL;
}

} // namespace RoseRmlLayout
