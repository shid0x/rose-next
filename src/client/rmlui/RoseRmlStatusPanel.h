#ifndef _ROSE_RML_STATUS_PANEL_H_
#define _ROSE_RML_STATUS_PANEL_H_

/**
 * UI2 replacement for CAvatarInfoDlg ( DLG_TYPE_INFO ): the always-on panel
 * with the avatar's name, level, job, HP/MP/EXP bars and the MENU button.
 *
 * Layout and behaviour follow the 667 build's compact HUD
 * ( doc/667UI/667-avatar-hud.md ): numbers change at once while the bars ease
 * towards them over 400 ms, and clicking the panel targets yourself. The
 * legacy panel's weapon/ammo icon is deliberately not carried over.
 *
 * Read-only against game state: it samples the same accessors the legacy
 * dialog used and never feeds anything back except the two clicks.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>

#include <string>

#include "RoseRmlEasedBar.h"

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlStatusPanel {
public:
    RoseRmlStatusPanel();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// Per frame: decides visibility ( UI2 active + in the world ), samples the
    /// avatar and advances the bar easing. Only changed values are dirtied, so
    /// an idle frame costs no relayout.
    void Update();

    /// The panel element ( the move target ), for panels that hang off it.
    Rml::Element* GetPanel() const { return m_pPanel; }

private:
    void SetVisible(bool bVisible);
    void Sample();

    template <typename T>
    void Assign(T& field, const T& value, const char* pszName) {
        if (field != value) {
            field = value;
            m_Model.DirtyVariable(pszName);
        }
    }

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;
    Rml::Element* m_pPanel; ///< the move target, for position clamping

    /// Where the last left press on the panel started, to tell a click from a
    /// drag: RmlUi fires click on release even after the panel was moved.
    int m_iPressX;
    int m_iPressY;

    /// --- bound to status.rml ---------------------------------------------
    Rml::String m_strName;
    Rml::String m_strLevel;
    Rml::String m_strJob;
    Rml::String m_strHp;
    Rml::String m_strHpPct;
    Rml::String m_strMp;
    Rml::String m_strMpPct;
    Rml::String m_strExpPct;
    Rml::String m_strWeight;
    float m_fHpWidth;
    float m_fMpWidth;
    float m_fExpWidth;
    bool m_bHpLow;

    RoseRmlEasedBar m_HpBar;
    RoseRmlEasedBar m_MpBar;
    RoseRmlEasedBar m_ExpBar;
};

#endif /// _ROSE_RML_STATUS_PANEL_H_
