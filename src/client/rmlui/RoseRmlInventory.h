#ifndef _ROSE_RML_INVENTORY_H_
#define _ROSE_RML_INVENTORY_H_

/**
 * UI2 inventory: replaces CItemDlg ( DLG_TYPE_ITEM ).
 *
 * A VIEW over the hidden CItemDlg, which stays the inventory's model: its
 * slots receive every item event and hold the per-PC arrangement ( saved on
 * exit, re-applied on entering the world ). This window draws those slots
 * and acts through the same icons, drag items and commands:
 *
 * - a drag starts from CItemDlg's own drag item ( bag or equipment ), so
 *   every drop target it knows ( shops, bank, trade, skill bar, ground ... )
 *   works unchanged;
 * - a drop onto this window is DLG_TYPE_ITEM ( drop-target ), and the drop
 *   commands' slot questions ( CItemDlg::GetInvenSlot & co ) are answered
 *   here while UI2 is on;
 * - a click does what CSlot::Process did: Alt previews, Shift links to chat,
 *   Ctrl adds to the wishlist, a double-click uses / equips / unequips;
 *   repair and appraisal modes take the click through
 *   CItemDlg::HandleStateClick.
 *
 * Laid out for widescreen: the equipment on the left, the 6x5 bag on the
 * right, money and weight underneath. The left column has sections:
 *   Gear -- worn gear as a paper doll, the ammo row, a combat summary; the
 *           bag shows the Equip / Use / Etc tabs.
 *   PAT  -- the five cart / castle gear parts and the mounted stats the
 *           server previews for them ( CItemDlg::DriveTuningPreview ); the
 *           bag shows the riding-parts page.
 *   Costume -- the costume slots, each where the gear piece it covers sits
 *           on the gear doll; the bag shows the Equip page. While it is on
 *           screen, equipping armour puts it in the costume slot
 *           ( CostumeOpen -> CItemDlg::is_costume_tab_open ).
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CItemDlg;
class CSlot;
class CDragItem;
class CInfo;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlInventory {
public:
    RoseRmlInventory();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// The player's intent ( OpenDialog / CloseDialog ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    /// --- CItemDlg's questions, answered from this window ------------------
    /// Is the point over the bag ( an unequip drop lands there )?
    bool BagAt(int x, int y) const;
    /// The bag slot under the point: page ( INV_* ) and slot in that page.
    bool BagSlotAt(int x, int y, int& iPage, int& iSlot) const;
    /// Is the point over the equipment ( equip / socket drops )?
    bool EquipAt(int x, int y) const;
    /// The EQUIP_IDX_* slot under the point, or -1.
    int EquipSlotAt(int x, int y) const;
    /// Equipping goes to the costume slots.
    bool CostumeOpen() const;

    /// Which classic slots a cell stands for.
    enum Kind {
        KIND_BAG = 0, ///< index: slot in the page on screen
        KIND_EQUIP = 1, ///< index: EQUIP_IDX_* ( 0: a spacer in the doll )
        KIND_AMMO = 2, ///< index: SHOT_TYPE_*
        KIND_PAT = 3, ///< index: RIDE_PART_*
        KIND_COSTUME = 4, ///< index: COSTUME_IDX_* ( 0: a spacer in the doll )
    };

    /// The left column's sections.
    enum Section {
        SECTION_GEAR = 0,
        SECTION_PAT = 1,
        SECTION_COSTUME = 2,
    };

    /// One row of the mounted-stats table.
    struct TuneVM {
        Rml::String label;
        Rml::String value; ///< an em dash when the parts do not allow it
        bool fuel; ///< the fuel row has its own tooltip

        bool operator==(const TuneVM& o) const {
            return label == o.label && value == o.value && fuel == o.fuel;
        }
        bool operator!=(const TuneVM& o) const { return !(*this == o); }
    };

    struct CellVM {
        int kind;
        int index;
        Rml::String label; ///< what goes here, shown while it is empty
        bool filled;
        Rml::String src;
        Rml::String rect;
        int count; ///< stack, 0 = not stackable
        float cd; ///< reload curtain, percent
        bool dim; ///< locked ( in a trade, a shop list ... )
        bool worn; ///< durability under 50: the classic red tint
        int socket; ///< 0 none, 1 empty, 2 gem
        Rml::String gem_src;
        Rml::String gem_rect;

        bool operator==(const CellVM& o) const {
            return kind == o.kind && index == o.index && label == o.label && filled == o.filled
                && src == o.src && rect == o.rect && count == o.count && cd == o.cd
                && dim == o.dim && worn == o.worn && socket == o.socket && gem_src == o.gem_src
                && gem_rect == o.gem_rect;
        }
        bool operator!=(const CellVM& o) const { return !(*this == o); }
    };

private:
    CItemDlg* ItemDlg() const;
    int CurrentPage() const; ///< the bag page on screen ( INV_* )
    CSlot* SlotFor(int iKind, int iIndex) const;
    CDragItem* DragItemFor(int iKind) const;
    bool IsInWorld() const;

    void SetVisible(bool bVisible);
    void Sample();
    void OnPress(int iKind, int iIndex);
    void UpdateDragStart();
    void UpdateTooltip();
    void SampleTuning();
    void UpdateTuningTooltip();
    /// A legacy tooltip beside the window, on the side chosen by where the
    /// WINDOW sits ( as the other UI2 windows ).
    void PlaceTooltip(CInfo& ToolTip);

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    /// A press on a filled cell: a drag once it moves past the slop.
    int m_iPressKind;
    int m_iPressIndex; ///< -1: none
    int m_iPressX;
    int m_iPressY;

    /// --- bound to inventory.rml ------------------------------------------
    int m_iDropType; ///< DLG_TYPE_ITEM, the window's drop-target
    int m_iSection; ///< Section
    int m_iPage; ///< bag tab in the gear section: INV_WEAPON / INV_USE / INV_ETC
    int m_iCount[4]; ///< filled slots per page ( INV_* )
    std::vector<CellVM> m_Cells; ///< the bag page on screen
    std::vector<CellVM> m_Gear; ///< the paper doll, row by row
    std::vector<CellVM> m_Ammo;
    std::vector<CellVM> m_Pat;
    std::vector<CellVM> m_Costume;
    std::vector<TuneVM> m_Tune;
    Rml::String m_strAtk;
    Rml::String m_strDef;
    Rml::String m_strRes;
    Rml::String m_strMoney;
    Rml::String m_strWeight;
    float m_fWeightPct;
    int m_iWeightLevel; ///< 0 fine, 1 heavy ( 80% ), 2 over ( 100% )
};

#endif /// _ROSE_RML_INVENTORY_H_
