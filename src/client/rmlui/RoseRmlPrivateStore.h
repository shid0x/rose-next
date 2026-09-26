#ifndef _ROSE_RML_PRIVATE_STORE_H_
#define _ROSE_RML_PRIVATE_STORE_H_

/**
 * UI2 own shop: replaces CPrivateStoreDlg ( DLG_TYPE_PRIVATESTORE ).
 *
 * A VIEW over the hidden CPrivateStoreDlg, which stays the model: it observes
 * CPrivateStore, its slots hold the icons ( selling list, and the wish list
 * with the priced "exhibited" ones ), its drag items take goods off, and the
 * bag's drop command ( CTCmdDragItem2PrivateStoreDlg ) reads its tab -- kept
 * in step here. CPrivateStoreDlg::Hide() closes the shop, so this window
 * calls Prepare() / Teardown() instead of showing or hiding it.
 *
 * - The sign: a text field ( 30 characters, the classic default is
 *   "<name>'s shop" ), fixed while the shop is open.
 * - Selling: drop an item from the bag to price it ( RoseRmlGoodsForm );
 *   double-click or drag it off to take it back.
 * - Wanted: the wish list. Dropping a bag item asks its price at once
 *   ( classic only put it on the wish list, unpriced, where visitors never
 *   saw it ); double-click an unpriced item to price it, a priced one to take
 *   the price off; drag an unpriced one off to forget it.
 * - Open / Close shop, with what selling earns and buying costs.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CIcon;
class CPrivateStoreDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlPrivateStore {
public:
    RoseRmlPrivateStore();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    enum Tab {
        TAB_SELL = 0, ///< CPrivateStoreDlg::TAB_SELL
        TAB_WANTED = 1, ///< CPrivateStoreDlg::TAB_BUY
    };

    struct CellVM {
        int index;
        bool filled;
        Rml::String src;
        Rml::String rect;
        int count;
        int socket; ///< 0 none, 1 empty, 2 gem
        Rml::String gem_src;
        Rml::String gem_rect;
        Rml::String price; ///< unit price, abbreviated; "" = not priced
        bool priced; ///< on the list for visitors ( always, selling )
        bool unpriced; ///< filled, not priced ( a wish-list-only item )

        bool operator==(const CellVM& o) const {
            return index == o.index && filled == o.filled && src == o.src && rect == o.rect
                && count == o.count && socket == o.socket && gem_src == o.gem_src
                && gem_rect == o.gem_rect && price == o.price && priced == o.priced
                && unpriced == o.unpriced;
        }
        bool operator!=(const CellVM& o) const { return !(*this == o); }
    };

private:
    CPrivateStoreDlg* StoreDlg() const;
    CIcon* IconAt(int iIndex, bool* pbPriced = NULL) const;
    bool IsInWorld() const;

    void SetVisible(bool bVisible);
    void SetTab(int iTab);
    void Sample();
    void OnPress(int iIndex);
    void OnUse(int iIndex);
    void OnOpenShop();
    void OnCloseShop();
    void UpdateDragStart();
    void UpdateTooltip();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    int m_iPressIndex; ///< -1: none
    int m_iPressX;
    int m_iPressY;

    /// --- bound to privatestore.rml ------------------------------------------
    int m_iDropType; ///< DLG_TYPE_PRIVATESTORE
    bool m_bSelling; ///< the shop is open
    Rml::String m_strSign; ///< the sign while open ( the field is hidden )
    int m_iTab;
    int m_iSellCount;
    int m_iWishCount;
    int m_iPricedCount; ///< wanted items with a price
    std::vector<CellVM> m_Cells;
    Rml::String m_strEarn; ///< the whole selling list at its prices
    Rml::String m_strCost; ///< the whole priced wanted list
    Rml::String m_strMoney; ///< yours
    bool m_bShort; ///< buying costs more than you have
    bool m_bCanOpen; ///< something to trade and the money for it
    Rml::String m_strHint;
};

#endif /// _ROSE_RML_PRIVATE_STORE_H_
