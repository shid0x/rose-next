#ifndef _ROSE_RML_SYSTEM_MENU_H_
#define _ROSE_RML_SYSTEM_MENU_H_

/**
 * UI2 system menu: replaces CSystemDLG ( DLG_TYPE_SYSTEM ).
 *
 * Back to the game, character select, or exit -- opened from the menu bar,
 * Alt+X, Escape's saved set, or the game window's close button ( WM_CLOSE ).
 * Leaving goes through CSystemDLG::RequestLeave: refused within 10 s of a
 * fight, as classic, and the logout countdown is the UI2 message box's.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlSystemMenu {
public:
    RoseRmlSystemMenu();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

private:
    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void PlaceDefault();
    void Leave(bool bCharacterSelect);

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
};

#endif /// _ROSE_RML_SYSTEM_MENU_H_
