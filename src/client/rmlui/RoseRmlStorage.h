#ifndef _ROSE_RML_STORAGE_H_
#define _ROSE_RML_STORAGE_H_

/**
 * UI2 storage: replaces CBankDlg ( DLG_TYPE_BANK ) and, for zuly,
 * CBankWindowDlg ( DLG_TYPE_BANKWINDOW, the amount box ).
 *
 * A VIEW over the hidden CBankDlg, which observes CBank: its slots hold the
 * icons, its drag item takes a stored item to the bag ( asking how many ),
 * and its current tab is kept in step with this window's -- the deposit
 * command ( CTCmdMoveItemInv2Bank ) asks CBankDlg::GetCurrentTab() whether
 * the item goes to the Platinum tab.
 *
 * - an item dragged from the bag onto the window is stored ( the window is
 *   drop-target DLG_TYPE_BANK ); the bag's tooltips show the storage fee;
 * - a stored item dragged to the bag, or double-clicked, comes out ( the
 *   classic window did nothing on a double-click );
 * - Deposit / Withdraw ask how much zuly through the UI2 quantity popup
 *   ( CBankWindowDlg drew under the UI2 windows ), with its checks.
 *
 * What CBankDlg did besides drawing, kept: closing when you walk away from
 * the storage keeper or it disappears ( only while visible, so the hidden
 * dialog no longer did ).
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CSlot;
class CBankDlg;
class CTCmdNumberInput;
class CTCmdOpenNumberInputDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlStorage {
public:
    RoseRmlStorage();
    ~RoseRmlStorage();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    struct TabVM {
        int index;
        Rml::String name;
        int count; ///< filled slots

        bool operator==(const TabVM& o) const {
            return index == o.index && name == o.name && count == o.count;
        }
        bool operator!=(const TabVM& o) const { return !(*this == o); }
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

        bool operator==(const CellVM& o) const {
            return index == o.index && filled == o.filled && src == o.src && rect == o.rect
                && count == o.count && socket == o.socket && gem_src == o.gem_src
                && gem_rect == o.gem_rect;
        }
        bool operator!=(const CellVM& o) const { return !(*this == o); }
    };

private:
    CBankDlg* BankDlg() const;
    CSlot* SlotFor(int iIndex) const;
    bool IsInWorld() const;

    void SetVisible(bool bVisible);
    void SetTab(int iTab);
    void Sample();
    void OnPress(int iIndex);
    void OnUse(int iIndex);
    void OnMoney(bool bDeposit);
    void UpdateDragStart();
    void UpdateTooltip();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    /// Double-click: how many? -> to the bag ( CBankDlg's drag command ).
    CTCmdNumberInput* m_pCmdWithdrawItem;
    CTCmdOpenNumberInputDlg* m_pCmdOpenWithdrawItem;
    /// Zuly in / out ( CBankWindowDlg's OK ).
    CTCmdNumberInput* m_pCmdDeposit;
    CTCmdNumberInput* m_pCmdWithdraw;

    int m_iPressIndex; ///< -1: none
    int m_iPressX;
    int m_iPressY;

    /// --- bound to storage.rml -----------------------------------------------
    int m_iDropType; ///< DLG_TYPE_BANK
    Rml::String m_strTitle;
    int m_iTab;
    std::vector<TabVM> m_Tabs;
    std::vector<CellVM> m_Cells;
    Rml::String m_strMoney;
    Rml::String m_strDeposit; ///< button labels, the game's words
    Rml::String m_strWithdraw;
    bool m_bPlatinumLocked; ///< the Platinum tab is on screen and you are not Platinum
    Rml::String m_strPlatinumNote;
};

#endif /// _ROSE_RML_STORAGE_H_
