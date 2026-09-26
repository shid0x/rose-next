#ifndef _CPrivateStoreDlg_
#define _CPrivateStoreDlg_
#include "tgamectrl/tdialog.h"
#include "../../GameCommon/IObserver.h"
#include "../../GameData/CPrivateStore.h"
#include "subclass/CSlot.h"

const int c_iMaxSlotCount = 30;
class CDragItem;

/**
 * 자신이 개설한 개인상점을 위한 Dialog
 *	- Observable : CPrivateStore
 *
 * @Author		최종진
 * @Date			2005/9/12
 */
class CPrivateStoreDlg: public CTDialog, public IObserver {
public:
    CPrivateStoreDlg(int iDlgType);
    virtual ~CPrivateStoreDlg(void);

    virtual void Show();
    virtual void Hide();
    virtual void Draw();
    virtual void Update(POINT ptMouse);
    virtual void MoveWindow(POINT pt);
    virtual unsigned Process(unsigned uiMsg, WPARAM wParam, LPARAM lParam);

    virtual void Update(CObservable* pObservable, CTObject* pObj);

    void SortItemSellList();

    int GetTabType();

    /// The halves of Show() / Hide() that are not about the dialog being on
    /// screen. UI2 ( RoseRmlPrivateStore ) keeps this dialog hidden as the
    /// shop's model and calls these instead: Hide() closes the shop.
    void Prepare(); ///< on opening: lists cleared, observer, wish list attached
    void Teardown(); ///< on closing: the shop closed, lists cleared

    /// UI2 draws these slots and drags with these drag items, and keeps the
    /// tab in step: the drop command ( CTCmdDragItem2PrivateStoreDlg ) reads it.
    void SetTab(int iTab) { m_iTab = iTab; }
    CSlot* GetSellSlot(int i) { return (i >= 0 && i < c_iMaxSlotCount) ? &m_SellSlots[i] : NULL; }
    CSlotBuyPrivateStore* GetBuySlot(int i) {
        return (i >= 0 && i < c_iMaxSlotCount) ? &m_BuySlots[i] : NULL;
    }
    CDragItem* GetSellDragItem() { return m_pSellDragItem; }
    CDragItem* GetBuyDragItem() { return m_pBuyDragItem; }
    enum {
        TAB_SELL,
        TAB_BUY,
    };

private:
    void OnLButtonDown(unsigned uiProcID, WPARAM wParam, LPARAM lParam);
    void OnLButtonUp(unsigned uiProcID, WPARAM wParam, LPARAM lParam);

private:
    enum {
        IID_EDITBOX = 19,
        IID_BTN_CLOSE = 20,
        IID_BTN_START = 21,
        IID_BTN_STOP = 22,
        IID_TEXT_CLOSED = 23,
        IID_TEXT_OPENED = 24,
        IID_BTN_ADD_BUYLIST = 25,
        IID_RADIOBOX = 30,
        IID_BTN_SELL = 31,
        IID_BTN_BUY = 32,
    };

    int m_iTab;

    CSlot m_SellSlots[c_iMaxSlotCount]; /// 구입 희망 아이템의 아이콘이 Attach 될 슬롯들
    CSlotBuyPrivateStore
        m_BuySlots[c_iMaxSlotCount]; /// 판매 희망 아이템의 아이콘이 Attach 될 슬롯들

    CDragItem* m_pSellDragItem;
    CDragItem* m_pBuyDragItem;
};
#endif