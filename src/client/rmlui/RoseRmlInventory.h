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
 * Laid out for widescreen: the equipment on the left ( worn gear as a paper
 * doll, the ammo row, a combat summary ), the 6x5 bag on the right, money
 * and weight underneath. PAT and costume sections follow.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CItemDlg;
class CSlot;
class CDragItem;

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
    int m_iPage; ///< bag tab: INV_WEAPON / INV_USE / INV_ETC
    int m_iCount[3]; ///< filled slots per tab
    std::vector<CellVM> m_Cells; ///< the bag page on screen
    std::vector<CellVM> m_Gear; ///< the paper doll, row by row
    std::vector<CellVM> m_Ammo;
    Rml::String m_strAtk;
    Rml::String m_strDef;
    Rml::String m_strRes;
    Rml::String m_strMoney;
    Rml::String m_strWeight;
    float m_fWeightPct;
    int m_iWeightLevel; ///< 0 fine, 1 heavy ( 80% ), 2 over ( 100% )
};

#endif /// _ROSE_RML_INVENTORY_H_
