#ifndef _ROSE_RML_LAYOUT_H_
#define _ROSE_RML_LAYOUT_H_

/**
 * Where the player put each RmlUi panel, and how big the panels are.
 *
 * Position: a panel is dragged by a <handle move_target> in its markup; this
 * remembers the result in rose-next.ini and puts it back on the next run. The
 * stylesheet's left/top ( or the panel's PlaceDefault ) stay the default for a
 * player who never moved anything.
 *
 * A saved spot is an anchor, not pixels: [UI_LAYOUT] <key>=R-250,T0 hangs the
 * panel's top-left corner 250 dp left of the right edge, at the top. Each axis
 * anchors to the near edge or the middle ( L/C/R, T/M/B ), picked by the third
 * of the screen the panel's centre is in when it is dropped, and offsets are
 * dp. So a layout holds at any resolution and UI scale -- a panel on the right
 * edge stays there on a wider screen -- and a [UI_LAYOUT] section can be copied
 * between players as is. A resolution or scale change re-places every anchored
 * panel; Clamp() still keeps panels on screen. Older clients saved "x,y" screen
 * pixels: those are converted, from where the panel stands, on the first run.
 *
 * Scale: every stylesheet measures in dp, not px, so one ratio on the context
 * ( [VIDEO] UI_SCALE, percent ) scales every panel, fonts included, and the
 * text is re-rasterised at the new size rather than stretched.
 *
 * Lock: stops every panel from being dragged ( [VIDEO] UI_LOCK ), so a
 * misclick in a fight cannot move the HUD.
 */

namespace Rml {
class Context;
class Element;
} // namespace Rml

namespace RoseRmlLayout {

/// Once, after every panel document is loaded: applies the saved scale and
/// lock. Panels loaded later would not see the lock.
void Initialise(Rml::Context* pContext);
void Shutdown();

/// Apply the saved position, if any, save it again whenever a drag of this
/// panel ends, and remember it for Reset(). pPanel is the move target ( a
/// direct child of <body> ).
void Track(Rml::Element* pPanel, const char* pszKey);

/// Save where a tracked panel is now ( what a drag's end does ), for a panel
/// that moves another way: the chat's top edge follows its resize grip.
void SavePosition(Rml::Element* pPanel, const char* pszKey);

/// Every frame, before the context lays out: re-places anchored panels after a
/// resolution or UI-scale change, and converts saved pixel positions of older
/// clients once their panels have a size.
void Update();

/// The element under the mouse, if it belongs to pDocument; NULL otherwise.
/// Panels find their cells by walking up to an attribute ( slot-kind ... ), and
/// two panels can use the same one: the inventory read an empty trade slot
/// ( slot-kind 0 = your offer ) as bag slot 0 and showed that item's tooltip.
Rml::Element* HoverIn(Rml::Context* pContext, Rml::Element* pDocument);

/// Keep the whole panel on screen. Cheap when it already is; call per frame
/// while the panel is visible.
void Clamp(Rml::Element* pPanel, int iViewportW, int iViewportH);

/// Forget every saved position and put tracked panels back where their
/// stylesheets put them.
void Reset();

/// Percent, clamped to [kMinScale, kMaxScale]. Applied live and saved.
const int kMinScale = 75;
const int kMaxScale = 200;
void SetScale(int iPercent);
int GetScale();

/// The dp ratio in effect, for C++ that positions things in pixels.
float GetScaleRatio();

void SetLocked(bool bLocked);
bool IsLocked();

} // namespace RoseRmlLayout

#endif /// _ROSE_RML_LAYOUT_H_
