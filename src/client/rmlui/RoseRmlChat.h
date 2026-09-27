#ifndef _ROSE_RML_CHAT_H_
#define _ROSE_RML_CHAT_H_

/**
 * UI2 chat: replaces CChatDLG ( DLG_TYPE_CHAT ) and its filter window
 * CChatFilterDlg ( DLG_TYPE_CHATFILTER ).
 *
 * The window: the system strip on top ( notices, system and quest lines --
 * the classic top list ), the log, five tabs under it ( All, Whisper, Trade,
 * Party, Clan; the alliance tab is gone ), and the input. Moved by its grip, resized
 * from its top-right corner ( it grows up, from the bottom-left where it
 * sits ); both are saved.
 *
 * Lines: IT_MGR::AppendChatMsg feeds every line here too ( ChatAppend ), from
 * the start of the session, so the log is whole whichever interface shows.
 * Each tab keeps its filter ( the Filter button at the end of the tab row:
 * which kinds of line the tab on screen shows; its own channel cannot be
 * switched off, whispers always show, as in the classic window ); saved to
 * [UI_CHAT]. Lines are added to the page
 * directly -- one element per line, the oldest dropped past a cap -- not
 * through a data binding, so a busy chat costs one new element per message
 * rather than a re-render of the log. Item links ( <il:...> tokens ) are
 * coloured spans: hover for the item's tooltip, Alt+click to preview it.
 *
 * Sending is CChatDLG::SendLine, the classic path ( local commands, prefixes
 * ! @ $ # &, spam and shout limits, GM chat block, item-link tokens ); it
 * says what the input holds afterwards ( "@name " after a whisper ). A tab
 * click puts its prefix in the input, as the classic tabs did.
 *
 * The keyboard, by the Options chat mode: "Press Enter to type" -- Enter
 * focuses the input ( RoseRmlUi::ProcessWndMsg ), Enter sends and lets go,
 * Escape lets go; "Always typing" -- the input takes the keyboard whenever no
 * other field has it, keeps it on Enter, and hands Escape to the game ( which
 * closes windows ). F-keys reach the game from any field ( the skill bar ).
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>

#include <deque>
#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
} // namespace Rml

class RoseRmlChat {
public:
    RoseRmlChat();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void Update();

    /// A line ( game code page ): iFilter is CChatDLG::FILTER_*, -1 = the
    /// system strip.
    void Append(const char* pszMsg, DWORD dwColor, int iFilter);

    bool IsActive() const { return m_bVisible; }
    std::string GetInput() const; ///< game code page
    bool AppendInput(const char* pszText); ///< game code page; focuses the input
    /// Enter with no field focused: the input takes the keyboard.
    bool FocusInput();

    enum { kTabCount = 5, kFilterCount = 5 };

    struct TabVM {
        int index;
        Rml::String label;
        bool on;

        bool operator==(const TabVM& o) const {
            return index == o.index && label == o.label && on == o.on;
        }
        bool operator!=(const TabVM& o) const { return !(*this == o); }
    };

    struct FilterVM {
        int index;
        Rml::String label;
        bool on;
        bool locked; ///< the tab's own channel

        bool operator==(const FilterVM& o) const {
            return index == o.index && label == o.label && on == o.on && locked == o.locked;
        }
        bool operator!=(const FilterVM& o) const { return !(*this == o); }
    };

private:
    struct Line {
        Rml::String rml;
        int iFilter;
    };

    bool IsInWorld() const;
    bool Passes(int iTab, int iFilter) const;
    void SetVisible(bool bVisible);
    void SelectTab(int iTab);
    void RebuildLog();
    void AddLineElement(Rml::Element* pLog, const Rml::String& strRml, int iCap);
    void RefreshTabs();
    void RefreshMenu();
    void OpenMenu(int iTab);
    void ToggleFilter(int iFilter);
    void LoadFilters();
    void SaveFilters();
    void Send();
    void SetInput(const Rml::String& strUtf8);
    void OnLogPress(Rml::Event& ev);
    void UpdateTooltip();
    void UpdateResize();
    void ApplySize(float fW, float fH);
    void LoadSize();
    void PlaceDefault();
    void ScrollToEnd(Rml::Element* pList);
    bool IsAtEnd(Rml::Element* pList) const;

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::Element* m_pLog;
    Rml::Element* m_pSystem;
    Rml::Element* m_pInput;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;
    bool m_bAutoEnter; ///< the Options chat mode, as last applied to the input

    std::deque<Line> m_Chat; ///< every tabbed line of the session ( capped )
    std::deque<Rml::String> m_System;
    bool m_bShow[kTabCount][kFilterCount];
    int m_iTab;
    int m_iScrollLog; ///< frames left to keep the log at its end
    int m_iScrollSystem;

    /// The resize grip: pressed at, and the size then ( dp ).
    bool m_bResizing;
    int m_iPressX;
    int m_iPressY;
    float m_fPressW;
    float m_fPressH;
    float m_fPressTop;
    float m_fW; ///< dp
    float m_fH;

    /// --- bound to chat.rml -----------------------------------------------------
    std::vector<TabVM> m_Tabs;
    std::vector<FilterVM> m_Filters;
    bool m_bMenu;
    int m_iMenuTab;
    Rml::String m_strMenuTab; ///< the tab the menu is for
};

#endif /// _ROSE_RML_CHAT_H_
