#ifndef _ROSE_RML_UPGRADE_H_
#define _ROSE_RML_UPGRADE_H_

/**
 * UI2 upgrade ( refine ) window: replaces CUpgradeDlg ( DLG_TYPE_UPGRADE ).
 *
 * Opened by the refine skill ( SKILL_POWER 42: costs MP ) or an NPC
 * ( GF_openUpgrade: costs zuly, closes when you walk away ). Drop the item to
 * refine, then its materials ( the rows say what is needed ); the window
 * shows the chance, the cost and the grade it goes to.
 *
 * CUpgrade is the model; CUpgradeDlg, kept hidden, holds the slots, the drag
 * items, the Start checks ( CUpgradeDlg::Start ) and the state machine.
 * **A hidden dialog does not update**, and the classic result state is where
 * the result bar ran and the OK box was opened -- the result reaching the bag
 * waits for that OK ( CUpgradeDlgStateResult::Leave -> ApplyResultItemSet ).
 * So this window plays the result itself: the 5 s bar, turning green at the
 * success point, then the classic box ( routed to UI2 ) whose OK returns the
 * dialog to NORMAL and applies the result. It cannot close while a request or
 * a result is pending, or the result would never reach the bag.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>
#include <string>
#include <vector>

class CIcon;
class CUpgradeDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlUpgrade {
public:
    RoseRmlUpgrade();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog. A close is refused while busy.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    struct MatVM {
        int index;
        bool needed; ///< the refine asks for something in this row
        bool placed;
        bool missing; ///< needed, not placed
        Rml::String src;
        Rml::String rect;
        Rml::String name; ///< what is needed
        Rml::String count; ///< "x 3"

        bool operator==(const MatVM& o) const {
            return index == o.index && needed == o.needed && placed == o.placed
                && missing == o.missing && src == o.src
                && rect == o.rect && name == o.name && count == o.count;
        }
        bool operator!=(const MatVM& o) const { return !(*this == o); }
    };

private:
    enum Phase {
        PHASE_NONE, ///< no result showing
        PHASE_BAR, ///< the result bar filling
        PHASE_DONE, ///< the bar is full, the OK box is up
    };

    CUpgradeDlg* Dlg() const;
    CIcon* IconAt(int iKind, int iIndex) const;
    bool IsInWorld() const;
    bool IsBusy() const;

    void SetVisible(bool bVisible);
    void Sample();
    void UpdateResult();
    void BeginResult();
    void FinishResult();
    void OnPress(int iKind, int iIndex);
    void OnUse(int iKind, int iIndex);
    void UpdateDragStart();
    void UpdateTooltip();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    int m_iPressKind; ///< -1: none
    int m_iPressIndex;
    int m_iPressX;
    int m_iPressY;

    /// The result bar ( CUpgradeDlgStateResult's numbers ).
    int m_iPhase;
    DWORD m_dwBarStart;
    int m_iBarTarget; ///< 0..100 where the bar stops
    int m_iBarMark; ///< 0..100 where it turns green
    bool m_bSuccess;

    /// --- bound to upgrade.rml ------------------------------------------------
    int m_iDropType; ///< DLG_TYPE_UPGRADE, 0 while busy ( drops cancel )
    Rml::String m_strWhere;
    bool m_bHasTarget;
    Rml::String m_strTargetSrc;
    Rml::String m_strTargetRect;
    Rml::String m_strTargetName;
    Rml::String m_strGrade; ///< "+3 -> +4"
    std::vector<MatVM> m_Mats;
    Rml::String m_strChance; ///< "57%" or "-"
    Rml::String m_strCostLabel;
    Rml::String m_strCost;
    Rml::String m_strHave;
    bool m_bShort;
    int m_iState; ///< CUpgradeDlg's
    bool m_bCanStart;
    bool m_bShowBar; ///< a result is showing
    Rml::String m_strFill; ///< "scaleX(0.4)"
    Rml::String m_strMark; ///< "57%"
    bool m_bMarkHigh; ///< the label goes left of the mark ( near the end )
    bool m_bGreen;
    Rml::String m_strOutcome; ///< once the bar is full
    bool m_bOutcomeGood;
};

#endif /// _ROSE_RML_UPGRADE_H_
