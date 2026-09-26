#ifndef _ROSE_RML_GOODS_FORM_H_
#define _ROSE_RML_GOODS_FORM_H_

/**
 * UI2 price form for your own shop: replaces CGoodsDlg ( DLG_TYPE_GOODS ).
 *
 * Asked when an item goes on your shop's selling list ( dropped from the bag
 * on the Selling tab ) or its wanted list ( dropped on the Wanted tab, or a
 * wanted item without a price double-clicked ): the price each and, for a
 * stack, how many. The classic defaults are kept -- 60% of the item's base
 * price to sell, 70% to buy; the whole stack to sell, 1 to buy.
 *
 * The hidden CGoodsDlg stays the model: its openers set the item and the kind
 * ( SetIcon / SetType ) as before, this form reads them and answers through
 * CGoodsDlg::Confirm. Enter in a field answers OK, Escape cancels.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>
#include <string>

class CGoodsDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlGoodsForm {
public:
    RoseRmlGoodsForm();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog ( never toggled: see RoseUi2 ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

private:
    CGoodsDlg* Dlg() const;
    void SetVisible(bool bVisible);
    void Describe();
    void Recount();
    void Answer(bool bOk);
    void PlaceAtMouse();
    void FocusPrice();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    bool m_bPlace;
    bool m_bFocus; ///< put the caret in the price once the form shows
    POINT m_ptOpen;
    int m_iMaxQuantity; ///< selling a stack: what you have; 0 = no limit

    /// --- bound to goods.rml ------------------------------------------------
    Rml::String m_strTitle;
    bool m_bSelling;
    Rml::String m_strItemName;
    Rml::String m_strItemSrc;
    Rml::String m_strItemRect;
    Rml::String m_strHave; ///< "You have 12" ( selling a stack )
    bool m_bStack; ///< the quantity field shows
    Rml::String m_strPrice;
    Rml::String m_strQuantity;
    Rml::String m_strTotal;
    bool m_bValid; ///< a price and a quantity above 0
    bool m_bShort; ///< buying: the total is more than your zuly
};

#endif /// _ROSE_RML_GOODS_FORM_H_
