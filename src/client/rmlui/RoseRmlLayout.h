#ifndef _ROSE_RML_LAYOUT_H_
#define _ROSE_RML_LAYOUT_H_

/**
 * Where the player put each RmlUi panel, and how big the panels are.
 *
 * Position: a panel is dragged by a <handle move_target> in its markup; this
 * remembers the result in rose-next.ini ( [UI_LAYOUT] <key>=x,y ) and puts it
 * back on the next run. The stylesheet's left/top stay the default for a
 * player who never moved anything. Positions are screen pixels, so a saved
 * spot can fall off a smaller screen after a resolution change -- Clamp()
 * pulls the panel back inside.
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
