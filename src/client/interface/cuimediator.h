#ifndef _UIMEDIATOR_
#define _UIMEDIATOR_

#include "CChatBox.h"
#include "CNameBox.h"
#include "CDigitEffect.h"
#include "PersonalStoreManager.h"
#include "CSummonInfoPanel.h"

#include "CObjCHAR.h"

class CUIMediator {
private:
    CChatBoxManager m_ChatBoxMgr;
    CNameBox m_NameBox;
    CDigitEffect m_DigitEffect;
    CPersonalStoreManager m_PersonalStoreManager;
    CSummonInfoPanel m_SummonPanel;

public:
    CUIMediator();
    ~CUIMediator();

    bool InitMediator();
    void FreeResource();

    void Update();
    void Draw();
    void DrawAvataName(CObjCHAR* pCharOBJ);

    void AddChatMsg(int iCharIndex, const char* pMsg, DWORD Color = D3DCOLOR_ARGB(255, 0, 0, 0));
    void DrawChatBox() { m_ChatBoxMgr.Draw(); };
    void CreateDamageDigit(int iPoint, float x, float y, float z, bool bIsUSER);

    void AddPersonalStoreIndex(int iObjIDX);
    void SubPersonalStoreIndex(int iObjIDX);
    void ResetPersonalStore();

    /// 소환몹 정보 패널 드래그 입력 (CGameStateMain 의 즉시 메시지 경로에서 호출)
    bool SummonPanelLButtonDown(int x, int y) { return m_SummonPanel.OnLButtonDown(x, y); }
    bool SummonPanelMouseMove(int x, int y) { return m_SummonPanel.OnMouseMove(x, y); }
    bool SummonPanelLButtonUp(int x, int y) { return m_SummonPanel.OnLButtonUp(x, y); }
    bool IsSummonPanelDragging() { return m_SummonPanel.IsDragging(); }
};

extern CUIMediator g_UIMed;

#endif //_UIMEDIATOR_