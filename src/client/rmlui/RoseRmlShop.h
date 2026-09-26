#ifndef _ROSE_RML_SHOP_H_
#define _ROSE_RML_SHOP_H_

/**
 * UI2 NPC shop: one window standing in for the two classic ones, the store
 * ( CStoreDLG, DLG_TYPE_STORE ) and the basket ( CDealDLG, DLG_TYPE_DEAL ).
 *
 * A VIEW over the hidden classic dialogs, which stay the shop's model: they
 * observe CStore and CDealData, so their slots hold the icons, and their
 * icons, drag items and commands are what this window acts through:
 *
 * - a drag starts from the classic dialog's own drag item, so its targets
 *   work unchanged: store -> bag buys now, store -> basket adds to the
 *   buying list, basket -> anywhere else takes it off the list;
 * - the window is a drop target ( drop-target ): the store side is
 *   DLG_TYPE_STORE ( an item from the bag sells now ), the basket side
 *   DLG_TYPE_DEAL ( an item from the bag goes on the selling list );
 * - a double-click on a store item asks how many and puts them in the
 *   basket ( the store icon's own command ), a double-click in the basket
 *   takes the item back out -- the classic basket did nothing on one.
 *
 * What the classic dialogs did besides drawing, kept: closing when the NPC
 * goes away or you walk off ( CStoreDLG::Update ), and emptying the basket
 * on close ( CDealDLG::Hide ). Added: each item's price under it, red when
 * you cannot afford it, and the money you are left with after the trade.
 * Union shops price in union points and show the union's mark.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CSlot;
class CDragItem;
class CInfo;
class CStoreDLG;
class CDealDLG;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlShop {
public:
    RoseRmlShop();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// The game's intent ( OpenDialog / CloseDialog on the store or the
    /// basket: both mean the whole shop ). Closing empties the basket.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    /// Which classic slots a cell stands for.
    enum Kind {
        KIND_STORE = 0, ///< index: slot in the tab on screen
        KIND_BUY = 1, ///< index: slot in the buying list
        KIND_SELL = 2, ///< index: slot in the selling list
    };

    struct TabVM {
        int index; ///< the store tab ( 0-3 )
        Rml::String name;

        bool operator==(const TabVM& o) const { return index == o.index && name == o.name; }
        bool operator!=(const TabVM& o) const { return !(*this == o); }
    };

    struct CellVM {
        int kind;
        int index;
        bool filled;
        Rml::String src;
        Rml::String rect;
        int count; ///< stack in the basket, 0 = none shown
        int socket; ///< 0 none, 1 empty, 2 gem
        Rml::String gem_src;
        Rml::String gem_rect;
        Rml::String price; ///< store: under the icon, abbreviated
        bool dear; ///< store: more than you have

        bool operator==(const CellVM& o) const {
            return kind == o.kind && index == o.index && filled == o.filled && src == o.src
                && rect == o.rect && count == o.count && socket == o.socket
                && gem_src == o.gem_src && gem_rect == o.gem_rect && price == o.price
                && dear == o.dear;
        }
        bool operator!=(const CellVM& o) const { return !(*this == o); }
    };

private:
    CStoreDLG* StoreDlg() const;
    CDealDLG* DealDlg() const;
    CSlot* SlotFor(int iKind, int iIndex) const;
    CDragItem* DragItemFor(int iKind) const;
    bool IsInWorld() const;

    void Close();
    void SetVisible(bool bVisible);
    void Sample();
    void SampleName();
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
    int m_iNpcAtOpen; ///< the store's NPC ( client index ) when it opened

    /// A press on a filled cell: a drag once it moves past the slop.
    int m_iPressKind;
    int m_iPressIndex; ///< -1: none
    int m_iPressX;
    int m_iPressY;

    /// --- bound to shop.rml ------------------------------------------------
    int m_iStoreDrop; ///< DLG_TYPE_STORE
    int m_iDealDrop; ///< DLG_TYPE_DEAL
    Rml::String m_strName; ///< the NPC
    Rml::String m_strRole; ///< "Weapon Merchant" from "[Weapon Merchant] Raffle"
    int m_iTab;
    std::vector<TabVM> m_Tabs;
    std::vector<CellVM> m_Store;
    std::vector<CellVM> m_Buy;
    std::vector<CellVM> m_Sell;
    int m_iBuyCount;
    int m_iSellCount;
    bool m_bUnion; ///< prices in union points
    Rml::String m_strUnionSrc;
    Rml::String m_strUnionRect;
    Rml::String m_strBuyTotal;
    Rml::String m_strSellTotal;
    Rml::String m_strMoney; ///< what you have ( zuly, or union points )
    Rml::String m_strAfter; ///< ... after the trade
    bool m_bShort; ///< the trade costs more than you have
};

#endif /// _ROSE_RML_SHOP_H_
