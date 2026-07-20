#ifndef _CITEMPREVIEWPANEL_
#define _CITEMPREVIEWPANEL_

#include <windows.h>
#include <string>

#include "citem.h"

#include "../CJustModelAVT.h"

/**
 * "Item Preview" — 장비 아이템을 내 캐릭터에 입혀보는 순수 클라이언트 창.
 * ALT+좌클릭( 인벤토리/상점/은행의 아이템 아이콘, 또는 채팅 아이템 링크 )으로 연다.
 *
 * - 실제 장착/인벤토리는 전혀 건드리지 않는다. 서버와 어떤 통신도 하지 않는다.
 * - 아바타 퍼핏: CJustModelAVT( 캐릭터 셀렉트 화면의 모델 뷰어 )를 재사용해
 *   현재 장비( g_pAVATAR->GetPartITEM )에 미리보기 아이템 한 파트만 덮어쓴
 *   복제 모델을 만든다. 씬에는 삽입하지 않고, 몬스터 인스펙터와 같은
 *   setAvatarViewPort / RenderSelectedAvatar / setDefaultViewPort 파이프라인으로
 *   패널 안 프리뷰 영역에 직접 렌더링한다( 월드 카메라와 무관 ).
 * - 미리보기 가능한 타입: 장식/모자/갑옷/장갑/신발/등가방/무기/보조무기.
 *   장신구( JEWEL )는 모델 파트가 없어 제외.
 * - 누적 미리보기: 창이 열려있는 동안 ALT+클릭할 때마다 해당 파트의 오버라이드가
 *   추가/교체되어 여러 아이템을 한꺼번에 입혀볼 수 있다( 모자 + 갑옷 + 무기 ... ).
 *   같은 파트를 다시 클릭하면 교체. 창을 닫으면 오버라이드가 모두 초기화된다.
 */
class CItemPreviewPanel {
public:
    CItemPreviewPanel();
    ~CItemPreviewPanel();

    /// 장비 아이템이면 퍼핏에 입혀 창을 연다.
    /// 이미 열려있으면 그 파트의 오버라이드만 추가/교체( 누적 미리보기 ).
    /// true = 열림( 호출측은 클릭을 소비 ), false = 미리보기 불가( 소비하지 않음 ).
    bool Open(tagITEM& sItem);
    void Close();
    bool IsOpen() const { return m_bOpen; }

    /// HUD 스프라이트 블록 안에서 호출 (beginSprite/endSprite 사이).
    void Draw();

    /// 입력. 화면 좌표(클라이언트 픽셀). true 반환 = 메시지 소비.
    bool OnLButtonDown(int x, int y);
    bool OnMouseMove(int x, int y); /// 드래그중일 때만 true
    bool OnLButtonUp(int x, int y);

    bool IsDragging() const { return m_bDragging; }

    /// 입혀볼 수 있는 아이템인가( 타입 + STB 범위 + 이름 존재 )
    static bool IsPreviewableItem(tagITEM& sItem);

private:
    void EnsureDefaultPosition();
    void GetPanelRect(RECT& rcOut) const;
    void GetCloseRect(RECT& rcOut) const;
    void GetPreviewRect(RECT& rcOut) const;

    /// ---- 3D 아바타 퍼핏 ----
    bool PuppetBuild(); /// 현재 장비 + m_Override* 전체를 반영해 재생성
    void PuppetDestroy();
    void PuppetDrawInPane(const RECT& rcPane);

    void ClearOverrides();
    int CountOverrides() const;

    bool m_bOpen;

    /// 파트별 미리보기 오버라이드( 누적 ). m_bOverride[part] 가 true 인 파트만
    /// 현재 장비 대신 m_OverrideItemNo[part] 를 입힌다.
    bool m_bOverride[MAX_BODY_PART];
    int m_OverrideItemNo[MAX_BODY_PART];

    std::string m_strItemLabel; /// "마지막 아이템 이름 (등급)  [+n more]"
    DWORD m_dwItemNameColor;    /// 마지막 아이템의 레어도 이름 색

    POINT m_ptPos; /// 패널 좌상단 (화면 픽셀)
    bool m_bPositionInit;
    bool m_bDragging;
    POINT m_ptDragGrab;

    /// 퍼핏( 씬에 삽입하지 않는 클라이언트 전용 복제 모델 )
    CJustModelAVT m_Puppet;
    bool m_bPuppetLoaded;
    float m_fPuppetModelHeight; /// cm
};

#endif // _CITEMPREVIEWPANEL_
