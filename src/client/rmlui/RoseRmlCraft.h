#ifndef _ROSE_RML_CRAFT_H_
#define _ROSE_RML_CRAFT_H_

/**
 * UI2 crafting window: replaces CMakeDLG ( DLG_TYPE_MAKE ).
 *
 * Opened by a crafting skill ( SKILL_POWER 11-39 -> IT_MGR::OpenMakeDlg ).
 * Pick what to make -- a kind on the left, an item in the middle ( the lists
 * CManufacture loads for the skill and its level ) -- then drop the
 * materials each row asks for from the bag. Each row shows how hard it is
 * ( the mark on its bar: the roll must pass it ); the MP cost is the
 * skill's.
 *
 * CManufacture is the model; CMakeDLG, kept hidden, holds the slots, the drag
 * item, the Start checks ( CMakeDLG::Start ) and the server's answer
 * ( RecvResult -- routed here through IT_MGR::IsDlgOpened, or it was
 * dropped ). **A hidden dialog does not update**, and the classic result
 * state is where the bars ran and the result reached the bag
 * ( CMakeStateResult::Update: SubItemsAfterRecvResult + Add_ITEM ), so this
 * window plays the bars and applies the result itself, then puts the dialog
 * back to NORMAL. It cannot close while a request or a result is pending.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>
#include <string>
#include <vector>

#include "citem.h"

class CIcon;
class CMakeDLG;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlCraft {
public:
    RoseRmlCraft();
    ~RoseRmlCraft();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog. An open with nothing to make, and a
    /// close while busy, are refused.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    struct ClassVM {
        int index;
        bool used; ///< rows are never removed ( see RoseRmlMinimap's marks )
        bool on;
        Rml::String name;

        bool operator==(const ClassVM& o) const {
            return index == o.index && used == o.used && on == o.on && name == o.name;
        }
        bool operator!=(const ClassVM& o) const { return !(*this == o); }
    };

    struct ItemVM {
        int index;
        bool used;
        bool on;
        Rml::String src;
        Rml::String rect;
        Rml::String name;

        bool operator==(const ItemVM& o) const {
            return index == o.index && used == o.used && on == o.on && src == o.src
                && rect == o.rect && name == o.name;
        }
        bool operator!=(const ItemVM& o) const { return !(*this == o); }
    };

    struct MatVM {
        int index;
        bool needed;
        bool placed;
        bool missing; ///< needed, not placed
        Rml::String src;
        Rml::String rect;
        Rml::String name;
        Rml::String count; ///< "x 3"
        Rml::String mark; ///< the success point along the bar, "57%"
        Rml::String fill; ///< "scaleX(0.4)"
        bool green;

        bool operator==(const MatVM& o) const {
            return index == o.index && needed == o.needed && placed == o.placed
                && missing == o.missing && src == o.src && rect == o.rect && name == o.name
                && count == o.count && mark == o.mark && fill == o.fill && green == o.green;
        }
        bool operator!=(const MatVM& o) const { return !(*this == o); }
    };

private:
    enum Phase {
        PHASE_NONE, ///< no result showing
        PHASE_BARS, ///< the bars filling one after another
        PHASE_DONE, ///< applied; the bars stay until the next change
    };

    CMakeDLG* Dlg() const;
    CIcon* MaterialIcon(int iIndex) const;
    bool IsInWorld() const;
    bool IsBusy() const;

    void SetVisible(bool bVisible);
    void SampleLists();
    void Sample();
    void SelectClass(int iIndex);
    void SelectItem(int iIndex);

    void BeginResult();
    void UpdateResult();
    void FinishResult();
    void ClearResult();

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

    /// What the lists hold ( CManufacture's makable classes and items ).
    std::vector<int> m_ClassIds;
    std::vector<tagITEM> m_ItemList;
    int m_iClass; ///< the chosen kind ( CManufacture keeps no getter for it )

    /// The recipe list's tooltip: an icon made for the hovered row, kept
    /// while the mouse stays on it.
    int m_iHoverItem;
    CIcon* m_pHoverIcon;

    /// The result bars ( CMakeStateResult's numbers: each fills to the
    /// server's point / 10 at 50 a second, one after another ).
    int m_iPhase;
    DWORD m_dwBarsStart;
    DWORD m_dwDoneAt; ///< the bars hold their fill a moment, then empty
    int m_iBarCount; ///< how many bars run ( up to the failed one )
    int m_iBarTarget[4];
    DWORD m_dwWaitStart; ///< when the request went ( a safety timeout )

    /// --- bound to craft.rml -------------------------------------------------
    int m_iDropType; ///< DLG_TYPE_MAKE, 0 while busy ( drops cancel )
    std::vector<ClassVM> m_Classes;
    std::vector<ItemVM> m_Items;
    bool m_bHasItem;
    Rml::String m_strItemSrc;
    Rml::String m_strItemRect;
    Rml::String m_strItemName;
    std::vector<MatVM> m_Mats;
    Rml::String m_strCost;
    Rml::String m_strHave;
    bool m_bShort;
    int m_iState; ///< CMakeDLG's
    bool m_bCanStart;
    bool m_bLocked; ///< busy: the lists and slots do not take clicks
    int m_iButton; ///< 0 Craft, 1 Working... ( sent ), 2 Crafting... ( the result )
    Rml::String m_strOutcome;
    bool m_bOutcomeGood;
};

#endif /// _ROSE_RML_CRAFT_H_
