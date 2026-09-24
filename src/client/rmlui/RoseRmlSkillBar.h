#ifndef _ROSE_RML_SKILL_BAR_H_
#define _ROSE_RML_SKILL_BAR_H_

/**
 * UI2 skill bar: replaces the two legacy quickbars ( DLG_TYPE_QUICKBAR and
 * DLG_TYPE_QUICKBAR_EXT ) with one panel of two rows.
 *
 * It is a VIEW. The legacy CQuickBAR dialogs stay alive, hidden: they still
 * own the hotkeys ( F1-F8, Shift+F1-F4 pages, Ctrl+F1-F8 for the second row ),
 * the current page and the hot-icon observers, so switching back to the
 * classic UI finds everything as it was. This panel reads the hot icons for
 * their pages and draws them from the atlas ( RoseRmlIcons ), with cooldown,
 * stack count and key label.
 *
 * Drag and drop is the legacy system's, bridged ( see RoseRmlUi ):
 *   - dropping on a row ends the legacy drag at that row's dialog type, so the
 *     legacy command runs unchanged and asks CQuickBAR::GetMouseClickSlot,
 *     which with UI2 on asks SlotAt() here;
 *   - pressing a filled slot and moving starts the legacy bar's own drag
 *     ( remove anywhere / move within the bar ).
 *
 * A single click uses the slot ( the legacy bar wanted a double-click ).
 * The hover tooltip is the legacy one ( CIcon::GetToolTip ), placed above
 * the bar.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CQuickBAR;
class CIcon;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlSkillBar {
public:
    RoseRmlSkillBar();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// Per frame: visibility, sampling, drag start, tooltip.
    void Update();

    /// The absolute hot-icon index ( page * 8 + slot ) under the point in the
    /// row that stands for iDlgType, or -1. CQuickBAR::GetMouseClickSlot's
    /// answer while UI2 is on.
    short SlotAt(int x, int y, int iDlgType);

    struct SlotVM {
        int index; ///< absolute hot-icon index
        bool filled;
        bool off; ///< present but unusable ( IsEnable() false )
        Rml::String src;
        Rml::String rect;
        float cd; ///< cooldown left, percent 0..100 ( the curtain height )
        Rml::String cd_text; ///< seconds left, empty under a second
        int count; ///< stack count, 0 = none
        Rml::String key;

        bool operator==(const SlotVM& o) const {
            return index == o.index && filled == o.filled && off == o.off && src == o.src
                && rect == o.rect && cd == o.cd && cd_text == o.cd_text && count == o.count
                && key == o.key;
        }
        bool operator!=(const SlotVM& o) const { return !(*this == o); }
    };

private:
    void SetVisible(bool bVisible);
    void Sample();
    void SampleRow(CQuickBAR* pBar, bool bCtrl, std::vector<SlotVM>& out, int& iPage);
    void PlaceDefault();
    void UpdateDragStart();
    void UpdateTooltip();

    static CQuickBAR* BarForSlot(int iSlot);
    static CIcon* IconForSlot(int iSlot);

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;

    /// A left press on a filled slot, until it becomes a click or a drag.
    int m_iPressSlot;
    int m_iPressX;
    int m_iPressY;

    /// --- bound to skillbar.rml ----------------------------------------------
    std::vector<SlotVM> m_Main;
    std::vector<SlotVM> m_Ext;
    int m_iMainPage; ///< 1-based, for display
    int m_iExtPage;
    int m_iMainType; ///< DLG_TYPE_QUICKBAR, the main row's drop-target
    int m_iExtType; ///< DLG_TYPE_QUICKBAR_EXT
};

#endif /// _ROSE_RML_SKILL_BAR_H_
