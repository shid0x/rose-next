#ifndef _ROSE_RML_MENU_BAR_H_
#define _ROSE_RML_MENU_BAR_H_

/**
 * UI2 menu bar: replaces CMenuDlg ( DLG_TYPE_MENU ), the classic pop-up menu.
 *
 * An always-visible strip, bottom right by default ( where the classic menu
 * opened ), movable and saved: Character, Bag, Skills, Quests, Community,
 * Clan, Options, System. A button lights while its window is open; the
 * caption above the bar names the hovered one and its Alt shortcut.
 *
 * DLG_TYPE_MENU is routed here ( RoseUi2's replaced windows ): opening it is
 * a no-op since the bar is always there, closing it is ignored ( the classic
 * world click closed the pop-up ), and it always reads as open. The tutorial
 * opens the menu and blinks one of its buttons ( SC_SetButtonBlink with the
 * MENU_BTN_* ids ): the blink lands on the bar's button, until it is hovered
 * or clicked, as CTButton's did.
 *
 * Dropped from the classic menu: dragging an entry out to make a desktop
 * shortcut icon ( as for the inventory ), the Game Info button, which did
 * nothing, and Help ( the help / tutorial window, of no use -- Alt+H still
 * opens it ). The tutorial's blink on Help has nothing to land on.
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

class RoseRmlMenuBar {
public:
    RoseRmlMenuBar();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void Update();

    /// SC_SetButtonBlink( DLG_TYPE_MENU, MENU_BTN_* ): false for a button the
    /// bar does not have.
    bool Blink(int iMenuButtonId);

    struct EntryVM {
        int index;
        Rml::String label;
        bool on; ///< its window is open
        bool blink; ///< the tutorial points at it

        bool operator==(const EntryVM& o) const {
            return index == o.index && label == o.label && on == o.on && blink == o.blink;
        }
        bool operator!=(const EntryVM& o) const { return !(*this == o); }
    };

private:
    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void PlaceDefault();
    void OnOpen(int iIndex);
    void OnHover(int iIndex);

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;
    int m_iHover; ///< -1: none

    /// --- bound to menubar.rml ------------------------------------------------
    std::vector<EntryVM> m_Entries;
    Rml::String m_strCaption; ///< "Character  Alt+A"
    bool m_bCaption;
};

#endif /// _ROSE_RML_MENU_BAR_H_
