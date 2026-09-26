#ifndef _CUpgradeDlg_
#define _CUpgradeDlg_
#include "tgamectrl/tdialog.h"
#include "subclass/CSlot.h"
#include "../../GameCommon/IObserver.h"

class CDragItem;
class CUpgradeDlgState;
class CUpgradeDlgStateNormal;
class CUpgradeDlgStateWait;
class CUpgradeDlgStateResult;

/**
 * 제련에 사용되는 다이얼로그
 *	- Observable : CUpgrade
 *
 * @Author		최종진
 * @Date			2005/9/14
 */
class CUpgradeDlg: public CTDialog, public IObserver {
    friend CUpgradeDlgStateNormal;
    friend CUpgradeDlgStateWait;
    friend CUpgradeDlgStateResult;

public:
    CUpgradeDlg(int iType);
    virtual ~CUpgradeDlg(void);

    virtual void Update(POINT ptMouse);
    virtual void Show();
    virtual void Hide();
    virtual void Draw();
    virtual void MoveWindow(POINT pt);
    virtual unsigned Process(unsigned uiMsg, WPARAM wParam, LPARAM lParam);

    virtual void Update(CObservable* pObservable, CTObject* pObj);

    void ChangeState(int iID);

    enum STATE_TYPE { STATE_NORMAL, STATE_WAIT, STATE_RESULT, STATE_MAX };

    /// UI2 ( RoseRmlUpgrade ) keeps this dialog hidden as the model: it reads
    /// the state and the slots, drags with the drag items, and starts through
    /// Start() -- the Start button's checks, the request, the wait.
    int GetState();
    bool Start();
    CSlot* GetTargetSlot() { return &m_TargetItemSlot; }
    CSlot* GetMaterialSlot(int i) { return (i >= 0 && i < 3) ? &m_MaterialSlots[i] : NULL; }
    CDragItem* GetTargetDragItem() { return m_pDragItemTarget; }
    CDragItem* GetMaterialDragItem() { return m_pDragItemMaterial; }

private:
    enum { IID_TEXT_COST = 5 };

    CUpgradeDlgState* m_pCurrState;
    CUpgradeDlgState* m_pStates[STATE_MAX];

    CDragItem* m_pDragItemTarget;
    CDragItem* m_pDragItemMaterial;

    CSlot m_TargetItemSlot;
    CSlot m_MaterialSlots[3];
};
#endif