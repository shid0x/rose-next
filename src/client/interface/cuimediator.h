#ifndef _UIMEDIATOR_
#define _UIMEDIATOR_

#include "CChatBox.h"
#include "CNameBox.h"
#include "CDigitEffect.h"
#include "PersonalStoreManager.h"
#include "CSummonInfoPanel.h"
#include "CMonsterInspectorPanel.h"
#include "CItemPreviewPanel.h"
#include "CDamageMeterPanel.h"

#include "CObjCHAR.h"

class CUIMediator {
private:
    CChatBoxManager m_ChatBoxMgr;
    CNameBox m_NameBox;
    CDigitEffect m_DigitEffect;
    CPersonalStoreManager m_PersonalStoreManager;
    CSummonInfoPanel m_SummonPanel;
    CMonsterInspectorPanel m_MonsterInspector;
    CItemPreviewPanel m_ItemPreview;
    CDamageMeterPanel m_DamageMeterPanel;

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

    /// 몬스터 인스펙터 창 (몬스터 인스펙터 스킬이 연다 — skill.cpp)
    void OpenMonsterInspector(int iClientObjIdx) { m_MonsterInspector.Open(iClientObjIdx); }
    void CloseMonsterInspector() { m_MonsterInspector.Close(); }
    bool MonsterInspectorLButtonDown(int x, int y) {
        return m_MonsterInspector.OnLButtonDown(x, y);
    }
    bool MonsterInspectorMouseMove(int x, int y) { return m_MonsterInspector.OnMouseMove(x, y); }
    bool MonsterInspectorLButtonUp(int x, int y) { return m_MonsterInspector.OnLButtonUp(x, y); }
    bool IsMonsterInspectorDragging() { return m_MonsterInspector.IsDragging(); }

    /// 아이템 미리보기 창 (ALT+클릭 — 인벤토리 CSlot / 채팅 아이템 링크가 연다)
    bool OpenItemPreview(tagITEM& sItem) { return m_ItemPreview.Open(sItem); }
    void CloseItemPreview() { m_ItemPreview.Close(); }
    bool ItemPreviewLButtonDown(int x, int y) { return m_ItemPreview.OnLButtonDown(x, y); }
    bool ItemPreviewMouseMove(int x, int y) { return m_ItemPreview.OnMouseMove(x, y); }
    bool ItemPreviewLButtonUp(int x, int y) { return m_ItemPreview.OnLButtonUp(x, y); }
    bool IsItemPreviewDragging() { return m_ItemPreview.IsDragging(); }

    /// 데미지 미터 패널 ( "/dps" 채팅 명령이 토글 — ChattingDlg )
    void ToggleDamageMeter() { m_DamageMeterPanel.Toggle(); }
    bool IsDamageMeterVisible() { return m_DamageMeterPanel.IsVisible(); }
    bool DamageMeterLButtonDown(int x, int y) { return m_DamageMeterPanel.OnLButtonDown(x, y); }
    bool DamageMeterMouseMove(int x, int y) { return m_DamageMeterPanel.OnMouseMove(x, y); }
    bool DamageMeterLButtonUp(int x, int y) { return m_DamageMeterPanel.OnLButtonUp(x, y); }
    bool IsDamageMeterDragging() { return m_DamageMeterPanel.IsDragging(); }
};

extern CUIMediator g_UIMed;

#endif //_UIMEDIATOR_