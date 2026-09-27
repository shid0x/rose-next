#ifndef _ROSE_RML_NOTIFY_BUTTONS_H_
#define _ROSE_RML_NOTIFY_BUTTONS_H_

/**
 * UI2 tutorial notify buttons: replaces the classic strip CNotifyButtonDlg
 * draws above the chat ( RoseUi2::PIECE_NOTIFY_BUTTONS ).
 *
 * The tutorial script adds a blinking button when it has something to tell
 * ( first login, a level up ... -- EventButton.STB ); a click runs that row's
 * Lua, which opens the notice ( SC_ShowNotifyMessage, already the UI2 message
 * box ), and the button goes. CNotifyButtonDlg stays the model: it keeps the
 * list the script fills, and its Activate() is the click for both UI2 and
 * classic. The table has no player-facing title ( column 0 is a Korean note
 * for the designers ), so the tooltip only says what a click does.
 *
 * Shown only while a button is pending; movable by its grip and saved.
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

class RoseRmlNotifyButtons {
public:
    RoseRmlNotifyButtons();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void Update();

    struct ButtonVM {
        int event; ///< EventButton.STB row
        bool used; ///< rows are never removed, only hidden

        bool operator==(const ButtonVM& o) const { return event == o.event && used == o.used; }
        bool operator!=(const ButtonVM& o) const { return !(*this == o); }
    };

private:
    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void PlaceDefault();
    void UpdateTooltip();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;

    /// --- bound to notify.rml -------------------------------------------------
    std::vector<ButtonVM> m_Buttons;
    Rml::String m_strIconSrc; ///< the classic sprites: normal, blink, hover
    Rml::String m_strIconRect;
    Rml::String m_strBlinkSrc;
    Rml::String m_strBlinkRect;
    Rml::String m_strOverSrc;
    Rml::String m_strOverRect;
};

#endif /// _ROSE_RML_NOTIFY_BUTTONS_H_
