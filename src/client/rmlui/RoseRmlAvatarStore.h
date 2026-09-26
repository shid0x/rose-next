#ifndef _ROSE_RML_AVATAR_STORE_H_
#define _ROSE_RML_AVATAR_STORE_H_

/**
 * UI2 visit to another player's shop: replaces CAvatarStoreDlg
 * ( DLG_TYPE_AVATARSTORE ).
 *
 * A VIEW over the hidden CAvatarStoreDlg, which stays the shop's model: the
 * list reply fills its items, its slots hold the icons, its drag item buys
 * ( how many? then the classic confirmation ), and the sell-to-them command
 * ( CTCmdDragItem2AvatarStoreDlg ) checks an item against its wanted list.
 * CAvatarStoreDlg::Hide() wipes the shop, so this window never shows or hides
 * it: it calls Prepare() on opening and Reset() on closing.
 *
 * - For sale: their goods, each with its unit price ( red when you cannot
 *   afford one ); drag one to your bag or double-click it to buy.
 * - Wanted: what they buy, with the price they pay; drop an item from your
 *   bag on the window to sell it to them ( the window is drop-target
 *   DLG_TYPE_AVATARSTORE ).
 *
 * Closes when the owner walks off or closes the shop ( CAvatarStoreDlg::Update
 * did, only while visible ).
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CSlot;
class CAvatarStoreDlg;
class CTCmdNumberInput;
class CTCmdOpenNumberInputDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlAvatarStore {
public:
    RoseRmlAvatarStore();
    ~RoseRmlAvatarStore();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog ( never toggled: see RoseUi2 ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    enum Tab {
        TAB_FOR_SALE = 0, ///< their goods ( CAvatarStoreDlg's sell list )
        TAB_WANTED = 1, ///< what they buy ( its buy list )
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
        Rml::String price; ///< unit price, abbreviated
        bool dear; ///< for sale, and more than you have

        bool operator==(const CellVM& o) const {
            return index == o.index && filled == o.filled && src == o.src && rect == o.rect
                && count == o.count && socket == o.socket && gem_src == o.gem_src
                && gem_rect == o.gem_rect && price == o.price && dear == o.dear;
        }
        bool operator!=(const CellVM& o) const { return !(*this == o); }
    };

private:
    CAvatarStoreDlg* StoreDlg() const;
    CSlot* SlotFor(int iIndex) const;
    bool IsInWorld() const;

    void SetVisible(bool bVisible);
    void SetTab(int iTab);
    void Sample();
    void OnPress(int iIndex);
    void OnUse(int iIndex);
    void UpdateDragStart();
    void UpdateTooltip();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    /// Double-click on their goods: how many? -> the classic buy command,
    /// which asks for confirmation.
    CTCmdNumberInput* m_pCmdBuy;
    CTCmdOpenNumberInputDlg* m_pCmdOpenBuy;

    int m_iPressIndex; ///< -1: none
    int m_iPressX;
    int m_iPressY;

    /// --- bound to avatarstore.rml -------------------------------------------
    int m_iDropType; ///< DLG_TYPE_AVATARSTORE
    Rml::String m_strTitle; ///< the shop's sign
    Rml::String m_strOwner;
    int m_iTab;
    int m_iCount[2]; ///< items per tab
    std::vector<CellVM> m_Cells;
    Rml::String m_strMoney; ///< yours
};

#endif /// _ROSE_RML_AVATAR_STORE_H_
