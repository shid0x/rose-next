#ifndef _ROSE_RML_INTERFACE_PANEL_H_
#define _ROSE_RML_INTERFACE_PANEL_H_

/**
 * UI2 "Interface" settings window: UI scale presets, lock panels, reset panel
 * positions, and a way back to the classic interface. Opened from the status
 * panel's UI button or "/ui".
 *
 * The settings themselves live in RoseRmlLayout ( scale, lock, reset ) and
 * RoseUi2 ( classic/UI2 ); this is only their view, so they also work without
 * it ( rose-next.ini [VIDEO] UI_SCALE / UI_LOCK ).
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlInterfacePanel {
public:
    RoseRmlInterfacePanel();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void Toggle();

    /// Per frame: closes the window when UI2 is switched off or you leave the
    /// world, and keeps it on screen.
    void Update();

private:
    void Show();
    void Hide();
    void Refresh();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;

    /// --- bound to interface.rml -------------------------------------------
    std::vector<int> m_Scales; ///< the preset buttons, in percent
    int m_iScale;
    bool m_bLocked;
    bool m_bBarVertical;
};

#endif /// _ROSE_RML_INTERFACE_PANEL_H_
