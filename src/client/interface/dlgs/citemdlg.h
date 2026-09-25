#ifndef _CItemDlg_
#define _CItemDlg_
#include "tgamectrl/tdialog.h"

#include <map>
#include "subclass/CSlot.h"
#include "../../gamecommon/IObserver.h"
#include "../../CClientStorage.h"

#include "tgamectrl/iactionlistener.h"
class CDragItem;
class CInfo;
class CTCmdNumberInput;
/**
 * 보유한 아이템및 장착된 아이템의 정보를 보여주는 다이얼로그
 *	- 서버와는 별도로 클라이언트에서만 인벤토리의 아이템의 위치를 이동시킬수 있으면 저장되어 진다.(
 *당연히 이동된 아이템의 위치는 같은 PC에서만 적용된다 )
 *	- Observable : CItemSlot
 *
 * @Author		최종진
 * @Date			2005/9/12
 */
class CItemDlg: public CTDialog, public IObserver, public IActionListener {
public:
    CItemDlg(int iType);
    virtual ~CItemDlg(void);

    virtual void Draw();
    virtual void Update(POINT ptMouse);
    virtual void Show();
    virtual void Hide();
    virtual void MoveWindow(POINT pt);
    virtual unsigned Process(unsigned uiMsg, WPARAM wParam, LPARAM lParam);

    virtual void Update(CObservable* pObservable, CTObject* pObj);

    virtual unsigned ActionPerformed(CActionEvent* e);

    bool IsInsideInven(POINT pt); /// 좌표가 인벤토리 슬롯 영역	위인가?
    bool IsInsideEquip(POINT pt); /// 좌표가 장착 슬롯 영역 위인가?

    int GetEquipSlot(POINT pt); /// 좌표로 장비 장착창 슬롯 번호를 구한다.
    CSlot* GetInvenSlot(POINT pt); /// 좌표로 인벤토리 슬롯 번호를 구한다.

    void ApplySavedVirtualInventory(std::list<S_InventoryData>& list);
    void GetVirtualInventory(std::list<S_InventoryData>& list);

    void AddActionEventListener2Slots();
    void RemoveActionEventListener2Slots();

    bool is_costume_tab_open();

    /// --- UI2 ( RoseRmlInventory ) -------------------------------------------
    /// While UI2 replaces this dialog it stays alive hidden as the inventory's
    /// model: its slots hold the per-PC arrangement and receive every item
    /// event. The UI2 window draws these slots and acts through the methods
    /// below; IsInsideInven / IsInsideEquip / GetEquipSlot / GetInvenSlot and
    /// is_costume_tab_open answer from the UI2 window instead.

    /// One bag slot: iPage is INV_WEAPON / INV_USE / INV_ETC / INV_RIDING.
    CSlot* GetBagSlot(int iPage, int iSlot);
    /// An equipment slot by EQUIP_IDX_* ( 1-based ), an ammo slot by SHOT_TYPE_*.
    CSlot* GetEquipSlotCtrl(int iEquipIdx);
    CSlot* GetAmmoSlot(int iShotType);
    /// A PAT part slot by RIDE_PART_*.
    CSlot* GetPatSlot(int iPart);
    /// A costume slot by COSTUME_IDX_* ( 1-based ).
    CSlot* GetCostumeSlot(int iCostumeIdx);
    /// Keeps the server's mounted-stats preview ( g_pNet->tuning_preview )
    /// requested while bActive -- the PAT section is on screen -- and
    /// invalidated when the worn parts change. Call every frame.
    void DriveTuningPreview(bool bActive);
    /// The mounted-stats tooltip: the fuel row's, or the rest of the table's.
    static void BuildTuningTooltip(CInfo& Info, bool bFuelRow);
    /// The drags a bag / an equipped item starts ( every drop target kept ).
    CDragItem* GetInvenDragItem() { return m_pInvenDragItem; }
    CDragItem* GetEquipDragItem() { return m_pEquipDragItem; }
    /// The money button: drop money, or add it to an open trade.
    void OnMoneyButton();
    /// A click on a slot while repairing or appraising. True when it was
    /// taken ( the click must then not start a drag ).
    bool HandleStateClick(CSlot* pSlot);

private:
    void DrawTuningStats();
    void UpdateTuningStats(POINT mouse);
    uint64_t m_TuningEquipmentSignature = 0;
    void SwitchIcon(int iReal,
        int iVirtual); /// 실제 인벤토리 인덱스와 가상 인벤토리인덱스로 아이콘 위치 이동
    CSlot* GetInvenSlotByRealIndex(int iIndex); /// 실제 인벤토리 인덱스로 슬롯 인덱스를 구한다.

    void OnLButtonDown(unsigned uiProcID, WPARAM wParam, LPARAM lParam);
    void OnLButtonUp(unsigned uiProcID, WPARAM wParam, LPARAM lParam);

    bool
    ProcessSlots(unsigned uiMsg, WPARAM wParam, LPARAM lParam); /// 슬롯들의 CTDialog::Process처리

    CWinCtrl* FindChildInPane(unsigned uiPaneID, unsigned uiChildID);
    
    bool IsAvailableRepair(CIcon* pIcon); /// 수리 가능한 아이템인가?

private:
    enum {
        IID_BTN_CLOSE = 10,
        IID_BTN_ICONIZE = 11,
        IID_BTN_MONEY = 12,
        IID_BTN_EQUIP_PAT = 23,
        IID_BTN_EQUIP_AVATAR = 33,
        IID_BTN_EQUIP_COSTUME = 43,
        IID_TABBEDPANE_INVEN_ITEM = 50,
        IID_BTN_INVEN_EQUIP = 53,
        IID_BTN_INVEN_USE = 63,
        IID_BTN_INVEN_ETC = 73,
        IID_TABBEDPANE_INVEN_PAT = 100,
        IID_PANE_EQUIP = 200,
        IID_PANE_INVEN = 300
    };

    enum {
        EQUIPMENT_TAB_ITEMS,
        EQUIPMENT_TAB_TUNING,
        EQUIPMENT_TAB_COSTUME
    };
    int m_iEquipTab; /// 0 ~ 2
    int m_iInventoryTab; /// 0 ~ 2

    CSlot m_AvatarEquipSlots[MAX_EQUIP_IDX - 1]; /// 캐릭터 아이템 장착 슬롯
    CSlot m_BulletEquipSlots[MAX_SHOT_TYPE]; /// 소모탄 아이콘 슬롯
    CSlot m_PatEquipSlots[MAX_RIDING_PART]; /// PAT 아이템 장착 슬롯
    CSlot costume_slots[MAX_COSTUME_IDX];
    CSlot m_ItemSlots[MAX_INV_TYPE][INVENTORY_PAGE_SIZE]; /// 모든 인벤토리 아이템 슬롯

    CDragItem* m_pInvenDragItem; /// 인벤토리의 아이템 아이콘 드래그 아이템
    CDragItem* m_pEquipDragItem; /// 장착중인 아이템 아이콘 드래그 아이템

    CTCmdNumberInput* m_pCmdDropMoney;
    CTCmdNumberInput* m_pCmdAddMyMoney2Exchange;
};
#endif