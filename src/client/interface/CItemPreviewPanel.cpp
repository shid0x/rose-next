#include "stdafx.h"

#include "CItemPreviewPanel.h"

#include "..\\Game.h"
#include "../CApplication.h"
#include "../Object.h"
#include "../CObjUSER.h"

#include "rose/io/stb.h"

#include "OverlayPanelUtil.h"
#include "../GameCommon/Item.h"

namespace {

/// 패널 레이아웃 상수 (픽셀)
const int kPanelW = 250;
const int kTitleH = 26;
const int kPad = 10;

const int kNameY = kTitleH + 4;
const int kNameH = 18;

/// 3D 프리뷰 영역( 패널이 칠하지 않는 구멍 — 그 사이로 퍼핏 모델이 보인다 )
const int kPreviewX = kPad;
const int kPreviewY = kNameY + kNameH + 6;
const int kPreviewW = kPanelW - kPad * 2;
const int kPreviewH = 300;

/// 색상
const D3DCOLOR kColTitle = D3DCOLOR_ARGB(255, 255, 255, 255);
const D3DCOLOR kColBg = D3DCOLOR_ARGB(215, 255, 255, 255); /// 배경(ID_BLACK_PANEL 틴트)
const D3DCOLOR kColClose = D3DCOLOR_ARGB(255, 255, 120, 120);

/// 퍼핏 이름( 엔진 노드 이름 충돌 방지용 카운터 포함 )
char*
NextPuppetName() {
    static DWORD s_dwOrder = 0;
    return CStr::Printf("ITEMPREVIEW_PUPPET_%u", s_dwOrder++);
}

/// 아이템 타입 -> 퍼핏에 덮어쓸 바디 파트. 미리보기 불가 타입은 -1.
short
ItemType2BodyPart(int iItemType) {
    switch (iItemType) {
        case ITEM_TYPE_FACE_ITEM:
            return BODY_PART_FACE_ITEM;
        case ITEM_TYPE_HELMET:
            return BODY_PART_HELMET;
        case ITEM_TYPE_ARMOR:
            return BODY_PART_ARMOR;
        case ITEM_TYPE_GAUNTLET:
            return BODY_PART_GAUNTLET;
        case ITEM_TYPE_BOOTS:
            return BODY_PART_BOOTS;
        case ITEM_TYPE_KNAPSACK:
            return BODY_PART_KNAPSACK;
        case ITEM_TYPE_WEAPON:
            return BODY_PART_WEAPON_R;
        case ITEM_TYPE_SUBWPN:
            return BODY_PART_WEAPON_L;
        default:
            return -1;
    }
}

} // namespace

CItemPreviewPanel::CItemPreviewPanel()
    : m_bOpen(false)
    , m_dwItemNameColor(0xFFFFFFFF)
    , m_bPositionInit(false)
    , m_bDragging(false)
    , m_bPuppetLoaded(false)
    , m_fPuppetModelHeight(170.0f) {
    m_ptPos.x = 0;
    m_ptPos.y = 0;
    m_ptDragGrab.x = 0;
    m_ptDragGrab.y = 0;
    ClearOverrides();
}

CItemPreviewPanel::~CItemPreviewPanel() {
    PuppetDestroy();
}

//--------------------------------------------------------------------------------

bool
CItemPreviewPanel::IsPreviewableItem(tagITEM& sItem) {
    if (sItem.IsEmpty())
        return false;

    int iType = sItem.GetTYPE();
    if (ItemType2BodyPart(iType) < 0)
        return false;

    if (!tagBaseITEM::IsValidITEM(iType, sItem.GetItemNO()))
        return false;

    /// 이름 없는( 비워진 ) STB 행 방어 — stale-quest get_cstr 크래시와 같은 규칙
    const char* szName = ITEM_NAME(iType, sItem.GetItemNO());
    if (szName == NULL || szName[0] == '\0')
        return false;

    return true;
}

bool
CItemPreviewPanel::Open(tagITEM& sItem) {
    if (!IsPreviewableItem(sItem))
        return false;
    if (g_pAVATAR == NULL)
        return false;

    /// 닫힌 상태에서 여는 거라면 새 세션 — 이전 오버라이드는 버린다.
    /// 열려있는 동안의 ALT+클릭은 해당 파트만 추가/교체( 누적 미리보기 ).
    if (!m_bOpen)
        ClearOverrides();

    short nPart = ItemType2BodyPart(sItem.GetTYPE());
    bool bHadOverride = m_bOverride[nPart];
    int iPrevItemNo = m_OverrideItemNo[nPart];
    m_bOverride[nPart] = true;
    m_OverrideItemNo[nPart] = sItem.GetItemNO();

    if (!PuppetBuild()) {
        /// 실패시 방금 추가한 오버라이드를 되돌린다( 열려있는 창은 이전 상태 유지 )
        m_bOverride[nPart] = bHadOverride;
        m_OverrideItemNo[nPart] = iPrevItemNo;
        if (m_bOpen)
            PuppetBuild();
        return false;
    }

    const char* szName = ITEM_NAME(sItem.GetTYPE(), sItem.GetItemNO());
    char szLabel[128];
    int iOthers = CountOverrides() - 1;
    if (!tagBaseITEM::is_stackable(sItem.GetTYPE()) && sItem.GetGrade() > 0)
        _snprintf(szLabel, sizeof(szLabel) - 1, "%s (%d)", szName, sItem.GetGrade());
    else
        _snprintf(szLabel, sizeof(szLabel) - 1, "%s", szName);
    szLabel[sizeof(szLabel) - 1] = '\0';
    if (iOthers > 0) {
        size_t iLen = strlen(szLabel);
        _snprintf(szLabel + iLen, sizeof(szLabel) - 1 - iLen, "  [+%d]", iOthers);
        szLabel[sizeof(szLabel) - 1] = '\0';
    }
    m_strItemLabel = szLabel;
    m_dwItemNameColor = CItem::GetItemNameColor(sItem.GetTYPE(), sItem.GetItemNO());

    m_bOpen = true;
    m_bDragging = false;
    EnsureDefaultPosition();
    return true;
}

void
CItemPreviewPanel::Close() {
    m_bOpen = false;
    m_bDragging = false;
    ClearOverrides();
    PuppetDestroy();
}

void
CItemPreviewPanel::ClearOverrides() {
    for (int i = 0; i < MAX_BODY_PART; i++) {
        m_bOverride[i] = false;
        m_OverrideItemNo[i] = 0;
    }
}

int
CItemPreviewPanel::CountOverrides() const {
    int iCount = 0;
    for (int i = 0; i < MAX_BODY_PART; i++) {
        if (m_bOverride[i])
            iCount++;
    }
    return iCount;
}

//--------------------------------------------------------------------------------

void
CItemPreviewPanel::EnsureDefaultPosition() {
    if (m_bPositionInit)
        return;

    int iScrW = g_pCApp->GetWIDTH();
    int iScrH = g_pCApp->GetHEIGHT();
    RECT rc;
    GetPanelRect(rc);
    int iH = rc.bottom - rc.top;
    /// 인벤토리 다이얼로그와 겹치지 않게 좌측 1/4 지점에 기본 배치
    m_ptPos.x = iScrW / 4 - kPanelW / 2;
    m_ptPos.y = (iScrH - iH) / 2;
    if (m_ptPos.x < 0)
        m_ptPos.x = 0;
    if (m_ptPos.y < 0)
        m_ptPos.y = 0;
    m_bPositionInit = true;
}

void
CItemPreviewPanel::GetPanelRect(RECT& rcOut) const {
    rcOut.left = m_ptPos.x;
    rcOut.top = m_ptPos.y;
    rcOut.right = m_ptPos.x + kPanelW;
    rcOut.bottom = m_ptPos.y + kPreviewY + kPreviewH + kPad;
}

void
CItemPreviewPanel::GetCloseRect(RECT& rcOut) const {
    rcOut.left = m_ptPos.x + kPanelW - 26;
    rcOut.top = m_ptPos.y + 4;
    rcOut.right = m_ptPos.x + kPanelW - 6;
    rcOut.bottom = m_ptPos.y + 22;
}

void
CItemPreviewPanel::GetPreviewRect(RECT& rcOut) const {
    rcOut.left = m_ptPos.x + kPreviewX;
    rcOut.top = m_ptPos.y + kPreviewY;
    rcOut.right = rcOut.left + kPreviewW;
    rcOut.bottom = rcOut.top + kPreviewH;
}

//--------------------------------------------------------------------------------

void
CItemPreviewPanel::Draw() {
    if (!m_bOpen)
        return;

    EnsureDefaultPosition();

    RECT rcPanel;
    GetPanelRect(rcPanel);
    int iX = rcPanel.left;
    int iY = rcPanel.top;
    int iH = rcPanel.bottom - rcPanel.top;

    /// 배경: 패널 전체 + 프리뷰 영역은 한 번 더 겹쳐 그려 더 어둡게( 모델 배경 ).
    RECT rcPv;
    GetPreviewRect(rcPv);
    OverlayPanel::DrawPanelBg(iX, iY, kPanelW, iH, kColBg);
    OverlayPanel::DrawPanelBg(rcPv.left, rcPv.top, kPreviewW, kPreviewH, kColBg);

    /// 타이틀 + 닫기
    OverlayPanel::DrawTextAt(iX + kPad, iY + 4, kPanelW - kPad * 2, 18, kColTitle,
        DT_LEFT | DT_VCENTER, FONT_NORMAL_BOLD, "Item Preview");
    OverlayPanel::DrawTextAt(iX + kPanelW - 26, iY + 4, 20, 18, kColClose, DT_CENTER | DT_VCENTER,
        FONT_NORMAL_BOLD, "X");

    /// 아이템 이름( 레어도 색 )
    OverlayPanel::DrawTextAt(iX + kPad, iY + kNameY, kPanelW - kPad * 2, kNameH,
        m_dwItemNameColor, DT_CENTER | DT_VCENTER, FONT_NORMAL_BOLD, m_strItemLabel.c_str());

    /// 3D 퍼핏은 맨 마지막에: 지금까지의 패널 스프라이트를 flush 한 뒤 그 위에 그린다.
    PuppetDrawInPane(rcPv);
}

//--------------------------------------------------------------------------------

bool
CItemPreviewPanel::OnLButtonDown(int x, int y) {
    if (!m_bOpen)
        return false;

    RECT rcClose;
    GetCloseRect(rcClose);
    if (x >= rcClose.left && x <= rcClose.right && y >= rcClose.top && y <= rcClose.bottom) {
        Close();
        return true;
    }

    RECT rc;
    GetPanelRect(rc);
    if (x < rc.left || x > rc.right || y < rc.top || y > rc.bottom)
        return false;

    /// 패널 위 클릭은 전부 소비( 클릭이 월드로 새어나가 이동/공격하지 않도록 ) + 드래그 시작
    m_bDragging = true;
    m_ptDragGrab.x = x - m_ptPos.x;
    m_ptDragGrab.y = y - m_ptPos.y;
    return true;
}

bool
CItemPreviewPanel::OnMouseMove(int x, int y) {
    if (!m_bOpen || !m_bDragging)
        return false;

    m_ptPos.x = x - m_ptDragGrab.x;
    m_ptPos.y = y - m_ptDragGrab.y;

    int iScrW = g_pCApp->GetWIDTH();
    int iScrH = g_pCApp->GetHEIGHT();
    if (m_ptPos.x < -kPanelW + 60)
        m_ptPos.x = -kPanelW + 60;
    if (m_ptPos.y < 0)
        m_ptPos.y = 0;
    if (m_ptPos.x > iScrW - 60)
        m_ptPos.x = iScrW - 60;
    if (m_ptPos.y > iScrH - 40)
        m_ptPos.y = iScrH - 40;

    return true;
}

bool
CItemPreviewPanel::OnLButtonUp(int /*x*/, int /*y*/) {
    if (!m_bDragging)
        return false;

    m_bDragging = false;
    return true;
}

//--------------------------------------------------------------------------------
/// 3D 아바타 퍼핏: 캐릭터 셀렉트 화면의 CJustModelAVT 를 재사용한다.
/// 현재 장비 파트( g_pAVATAR->GetPartITEM — 헬멧-머리 오프셋이 적용되지 않은
/// 원본값 )에 누적 오버라이드( m_bOverride / m_OverrideItemNo )를 덮어쓰고,
/// 10-인자 Init 이 헬멧에 맞는 머리모양 오프셋을 CObjAVT::Update 와 동일하게
/// 적용한다( 오버라이드된 헬멧 기준 ).
///
/// 씬에는 넣지 않는다( SetVisible(false) 로 UnloadModelVisible 의
/// RemoveFromScene 도 no-op ). 렌더링은 몬스터 인스펙터와 같은
/// setAvatarViewPort / RenderSelectedAvatar / setDefaultViewPort 파이프라인.
/// 스킨 메시/텍스처 디바이스 리소스는 화면의 실제 아바타와 이름으로 공유된다.
//--------------------------------------------------------------------------------

bool
CItemPreviewPanel::PuppetBuild() {
    PuppetDestroy();

    int iParts[MAX_BODY_PART];
    for (int i = 0; i < MAX_BODY_PART; i++) {
        if (m_bOverride[i])
            iParts[i] = m_OverrideItemNo[i];
        else
            iParts[i] = g_pAVATAR->GetPartITEM((short)i);
    }

    m_Puppet.SetRace(g_pAVATAR->IsFemale() ? 1 : 0);
    m_Puppet.Init(NextPuppetName(),
        iParts[BODY_PART_FACE],
        iParts[BODY_PART_HAIR], /// Init 이 헬멧-머리 오프셋을 적용한다
        iParts[BODY_PART_HELMET],
        iParts[BODY_PART_ARMOR],
        iParts[BODY_PART_GAUNTLET],
        iParts[BODY_PART_BOOTS],
        iParts[BODY_PART_FACE_ITEM],
        iParts[BODY_PART_KNAPSACK],
        iParts[BODY_PART_WEAPON_R],
        iParts[BODY_PART_WEAPON_L]);
    m_Puppet.UpdateModel();
    m_Puppet.LoadModelVisible();

    HNODE hModel = m_Puppet.GetModelNode();
    if (hModel == 0) {
        LogString(LOG_DEBUG_, "ItemPreview puppet: loadModel failed (%d overrides)\n",
            CountOverrides());
        PuppetDestroy();
        return false;
    }

    /// 씬 미삽입 + 픽킹/충돌 대상 아님( LoadModelNODE 가 4 로 켠 것을 끈다 )
    ::setCollisionLevel(hModel, 0);
    m_Puppet.SetVisible(false);

    ::setRepeatCount(hModel, 0); /// 0 = 무한반복( 대기 모션 루프 )

    m_fPuppetModelHeight = ::getModelHeight(hModel); /// 월드 단위(cm)
    if (m_fPuppetModelHeight < 10.0f)
        m_fPuppetModelHeight = 170.0f;

    m_bPuppetLoaded = true;
    return true;
}

void
CItemPreviewPanel::PuppetDestroy() {
    if (!m_bPuppetLoaded)
        return;

    /// SetVisible(false) 상태라 내부 RemoveFromScene 은 no-op — 씬에 넣은 적 없음.
    m_Puppet.UnloadModelVisible();
    m_bPuppetLoaded = false;
}

void
CItemPreviewPanel::PuppetDrawInPane(const RECT& rcPane) {
    if (!m_bPuppetLoaded || !m_bOpen)
        return;

    HNODE hModel = m_Puppet.GetModelNode();
    if (hModel == 0)
        return;

    float fPaneW = (float)(rcPane.right - rcPane.left);
    float fPaneH = (float)(rcPane.bottom - rcPane.top);
    if (fPaneW < 8.0f || fPaneH < 8.0f)
        return;

    /// 프레이밍은 몬스터 인스펙터와 동일( 엔진 내부 단위 = m, getModelHeight 는 cm ).
    float fModelH = m_fPuppetModelHeight * 0.01f; /// cm -> m
    float fRatio = fPaneW / fPaneH;

    float fWantLength = fModelH * 1.2f * fRatio; /// 세로 fit + 20% 여유
    if (fWantLength < 0.7f)
        fWantLength = 0.7f;
    float fWantHeight = -1.4f + fModelH * 0.5f; /// 모델 세로 중심

    OverlayPanel::PaneCameraSetFrame(fWantLength, fWantHeight);
    OverlayPanel::PaneCameraSpin(0.01f); /// 천천히 궤도 회전

    /// 지금까지 그린 패널 스프라이트를 먼저 백버퍼로 밀어낸 뒤,
    /// 팬 영역 뷰포트에서 모델을 그리고 원래 파이프라인으로 복귀한다.
    ::flushSprite();
    ::setAvatarViewPort((float)rcPane.left, (float)rcPane.top, fPaneW, fPaneH);
    ::RenderSelectedAvatar(hModel);
    ::setDefaultViewPort();
}
