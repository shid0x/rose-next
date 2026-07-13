#include "stdafx.h"

#include "CMonsterInspectorPanel.h"

#include "..\\Game.h"
#include "../CApplication.h"
#include "../Object.h"
#include "../CObjUSER.h"
#include "../CModelCHAR.h"
#include "../GameCommon/StringManager.h"

#include "rose/io/stb.h"

#include "CTDrawImpl.h"
#include "IO_ImageRes.h"
#include "CInfo.h"
#include "CToolTipMgr.h"
#include "../GameCommon/Item.h"
#include "tgamectrl/resourcemgr.h"

#include <math.h>

namespace {

/// 패널 레이아웃 상수 (픽셀)
const int kPanelW = 560;
const int kTitleH = 26;
const int kPad = 10;

/// 3D 프리뷰 영역( 패널이 칠하지 않는 구멍 — 그 사이로 퍼핏 모델이 보인다 )
const int kPreviewX = kPad;
const int kPreviewY = kTitleH + 10;
const int kPreviewW = 220;
const int kPreviewH = 280;

/// 우측 정보 컬럼
const int kInfoX = kPreviewX + kPreviewW + 10;
const int kInfoW = kPanelW - kInfoX - kPad;

const int kNameY = kTitleH + 10;
const int kNameH = 20;
const int kHpBarY = kNameY + 24;
const int kHpBarH = 16;
const int kStatY = kHpBarY + 24;
const int kStatRowH = 18;
const int kStatRows = 4;
const int kDropLabelY = kStatY + kStatRows * kStatRowH + 8;
const int kDropGridY = kDropLabelY + 20;
const int kIconCell = 42; /// 40px 아이콘 + 2px 간격
const int kIconsPerRow = 7;
const int kMaxDropShown = 35; /// 5줄


/// 색상
const D3DCOLOR kColTitle = D3DCOLOR_ARGB(255, 255, 255, 255);
const D3DCOLOR kColName = D3DCOLOR_ARGB(255, 255, 220, 80);   /// 몬스터 이름 (노랑)
const D3DCOLOR kColLabel = D3DCOLOR_ARGB(255, 170, 170, 170); /// 능력치 라벨
const D3DCOLOR kColValue = D3DCOLOR_ARGB(255, 240, 240, 240); /// 능력치 값
const D3DCOLOR kColHp = D3DCOLOR_ARGB(255, 255, 255, 255);
const D3DCOLOR kColBg = D3DCOLOR_ARGB(215, 255, 255, 255);    /// 배경(ID_BLACK_PANEL 틴트)
const D3DCOLOR kColClose = D3DCOLOR_ARGB(255, 255, 120, 120);

/// 화면 스프라이트 블록 안에서 절대 좌표에 텍스트를 그린다.
void
DrawTextAt(int x, int y, int w, int h, D3DCOLOR color, DWORD format, int iFont, const char* msg) {
    D3DXMATRIX mat;
    D3DXMatrixTranslation(&mat, (float)x, (float)y, 0.0f);
    ::setTransformSprite(mat);

    RECT rc = {0, 0, w, h};
    ::drawFont(g_GameDATA.m_hFONT[iFont], true, &rc, color, format | DT_SINGLELINE, msg);
}

void
DrawPanelBg(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return;

    /// ID_BLACK_PANEL 원본은 상단에 밝아지는 그라데이션이 구워져 있다:
    /// - 통째로 늘리면 위쪽에 넓은 밝은 띠가 생기고,
    /// - 얇은 스트립으로 타일링하면 스트립마다 밝은 윗줄이 반복돼 흰 줄이 생긴다.
    /// 그래서 DrawFit 을 쓰지 않고, 스프라이트 아래쪽의 평탄한 소스 영역만 잘라
    /// 한 번에 늘려 그린다( DrawFit 과 동일한 지오메트리 스케일 방식 ).
    CImageRes* pImageRes = CImageResManager::GetSingleton().GetImageRes(IMAGE_RES_UI);
    if (pImageRes == NULL)
        return;

    int iBlackPanel = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "ID_BLACK_PANEL");
    stTexture* pTextureInfo = pImageRes->GetTexture(iBlackPanel);
    stSprite* pSpriteInfo = pImageRes->GetSprite(iBlackPanel);
    if (pTextureInfo == NULL || pSpriteInfo == NULL || pTextureInfo->m_Texture == NULL)
        return;

    RECT rcSrc = pSpriteInfo->m_Rect;
    int iSrcH = rcSrc.bottom - rcSrc.top;
    int iKeepH = iSrcH / 4;
    if (iKeepH < 2)
        iKeepH = 2;
    rcSrc.top = rcSrc.bottom - iKeepH - 1;
    rcSrc.bottom -= 1; /// 확대 필터링시 경계 텍셀 블리딩 방지

    float fScaleWidth = (float)w / (rcSrc.right - rcSrc.left);
    float fScaleHeight = (float)h / (rcSrc.bottom - rcSrc.top);

    D3DXMATRIX mat;
    D3DXVECTOR2 scale(fScaleWidth, fScaleHeight);
    D3DXVECTOR2 pos((float)x, (float)y);
    D3DXMatrixTransformation2D(&mat, NULL, NULL, &scale, NULL, NULL, &pos);
    ::setTransformSprite(mat);

    ::drawSprite(pTextureInfo->m_Texture, &rcSrc, NULL, &D3DXVECTOR3(0, 0, 0), kColBg);
}

/// 퍼핏 이름( 엔진 노드 이름 충돌 방지용 카운터 포함 )
char*
NextPuppetName() {
    static DWORD s_dwOrder = 0;
    return CStr::Printf("MINSPECT_PUPPET_%u", s_dwOrder++);
}

} // namespace

CMonsterInspectorPanel::CMonsterInspectorPanel()
    : m_bOpen(false)
    , m_iTargetObjIdx(0)
    , m_nCharIdx(0)
    , m_iLastHP(0)
    , m_iLastMaxHP(0)
    , m_bPositionInit(false)
    , m_bDragging(false)
    , m_hPuppetModel(0)
    , m_nPuppetCharIdx(0)
    , m_fPuppetModelHeight(160.0f) {
    m_ptPos.x = 0;
    m_ptPos.y = 0;
    m_ptDragGrab.x = 0;
    m_ptDragGrab.y = 0;
    m_ptHover.x = -1;
    m_ptHover.y = -1;
    for (int i = 0; i < MAX_BODY_PART; i++)
        m_pPuppetPartVIS[i] = NULL;
}

CMonsterInspectorPanel::~CMonsterInspectorPanel() {
    PuppetDestroy();
}

//--------------------------------------------------------------------------------

CObjCHAR*
CMonsterInspectorPanel::GetTargetOBJ() const {
    if (m_iTargetObjIdx <= 0)
        return NULL;

    /// bCheckHP = false: 죽은 직후에도 오브젝트가 남아있는 동안 HP 0 을 읽어
    /// 게이지가 0 으로 떨어지게 한다( true 면 마지막 생존 값에서 얼어붙음 ).
    CObjCHAR* pCHAR = g_pObjMGR->Get_CharOBJ(m_iTargetObjIdx, false);
    if (pCHAR == NULL || !pCHAR->IsA(OBJ_MOB))
        return NULL;

    /// 오브젝트 슬롯이 다른 몬스터로 재사용됐을 수 있다.
    if (((CObjMOB*)pCHAR)->Get_CharNO() != m_nCharIdx)
        return NULL;

    return pCHAR;
}

void
CMonsterInspectorPanel::Open(int iClientObjIdx) {
    CObjCHAR* pCHAR = g_pObjMGR->Get_CharOBJ(iClientObjIdx, true);
    if (pCHAR == NULL || !pCHAR->IsA(OBJ_MOB))
        return;

    short nCharIdx = ((CObjMOB*)pCHAR)->Get_CharNO();
    if (nCharIdx <= 0 || nCharIdx >= g_TblNPC.row_count)
        return;

    m_iTargetObjIdx = iClientObjIdx;
    m_nCharIdx = nCharIdx;
    m_iLastHP = pCHAR->Get_HP();
    m_iLastMaxHP = pCHAR->Get_MaxHP();
    m_bOpen = true;
    m_bDragging = false;

    EnsureDefaultPosition();
    BuildDropList();
    PuppetCreate(nCharIdx);
}

void
CMonsterInspectorPanel::Close() {
    m_bOpen = false;
    m_bDragging = false;
    m_iTargetObjIdx = 0;
    m_Drops.clear();
    PuppetDestroy();
}

//--------------------------------------------------------------------------------
/// 드랍 목록: 서버 CCal::Get_DropITEM 과 같은 테이블 참조 규칙으로 나열한다.
/// - 몹 전용 테이블( NPC_DROP_TYPE )은 NPC_DROP_ITEM 확률이 있을 때만 사용됨
/// - 아니면 존 번호 행( iZoneNO )을 사용
/// 표시용으로는 두 테이블의 합집합을 보여준다( 몹 전용 우선 ).
/// 슬롯 값 1~4 는 리다이렉트 그룹( 인덱스 26+5g ~ 30+5g )이다.
//--------------------------------------------------------------------------------

void
CMonsterInspectorPanel::AppendDropTable(int iDropTBL, std::vector<DropEntry>& out) const {
    if (iDropTBL <= 0 || iDropTBL >= g_TblDropITEM.row_count)
        return;

    /// 기본 슬롯 0~29 + 리다이렉트 그룹 확장
    for (int iSlot = 0; iSlot < 30; iSlot++) {
        if (1 + iSlot >= g_TblDropITEM.col_count)
            break;

        int iValue = DROPITEM_ITEMNO(iDropTBL, iSlot);

        int iExpanded[6];
        int iCount = 0;
        if (iValue >= 1 && iValue <= 4) {
            for (int iG = 26 + iValue * 5; iG < 26 + iValue * 5 + 5; iG++) {
                if (1 + iG >= g_TblDropITEM.col_count)
                    break;
                iExpanded[iCount++] = DROPITEM_ITEMNO(iDropTBL, iG);
            }
        } else {
            iExpanded[iCount++] = iValue;
        }

        for (int iE = 0; iE < iCount; iE++) {
            int v = iExpanded[iE];
            if (v <= 1000)
                continue;

            short nType = (short)(v / 1000);
            short nItemNo = (short)(v % 1000);
            if (nType < 1 || nType > ITEM_TYPE_RIDE_PART)
                continue;
            if (g_pTblSTBs[nType] == NULL || nItemNo <= 0
                || nItemNo >= g_pTblSTBs[nType]->row_count)
                continue;

            bool bDup = false;
            for (size_t i = 0; i < out.size(); i++) {
                if (out[i].nType == nType && out[i].nItemNo == nItemNo) {
                    bDup = true;
                    break;
                }
            }
            if (bDup)
                continue;

            DropEntry entry;
            entry.nType = nType;
            entry.nItemNo = nItemNo;
            entry.iIconNo = ITEM_ICON_NO(nType, nItemNo);
            out.push_back(entry);
        }
    }
}

void
CMonsterInspectorPanel::BuildDropList() {
    m_Drops.clear();

    if (m_nCharIdx <= 0 || m_nCharIdx >= g_TblNPC.row_count)
        return;

    /// 몹 전용 드랍 테이블( 확률 값이 있을 때만 실제로 굴려짐 )
    if (NPC_DROP_ITEM(m_nCharIdx) > 0)
        AppendDropTable(NPC_DROP_TYPE(m_nCharIdx), m_Drops);

    /// 존 공용 드랍 테이블
    AppendDropTable(g_pTerrain->GetZoneNO(), m_Drops);
}

//--------------------------------------------------------------------------------

void
CMonsterInspectorPanel::EnsureDefaultPosition() {
    if (m_bPositionInit)
        return;

    int iScrW = g_pCApp->GetWIDTH();
    int iScrH = g_pCApp->GetHEIGHT();
    RECT rc;
    GetPanelRect(rc);
    int iH = rc.bottom - rc.top;
    m_ptPos.x = (iScrW - kPanelW) / 2;
    m_ptPos.y = (iScrH - iH) / 2 + 40;
    if (m_ptPos.x < 0)
        m_ptPos.x = 0;
    if (m_ptPos.y < 0)
        m_ptPos.y = 0;
    m_bPositionInit = true;
}

void
CMonsterInspectorPanel::GetPanelRect(RECT& rcOut) const {
    int iShown = (int)m_Drops.size();
    if (iShown > kMaxDropShown)
        iShown = kMaxDropShown;
    int iRows = (iShown + kIconsPerRow - 1) / kIconsPerRow;
    if (iRows < 1)
        iRows = 1;

    int iDropsBottom = kDropGridY + iRows * kIconCell;
    int iContentBottom = kPreviewY + kPreviewH;
    if (iDropsBottom > iContentBottom)
        iContentBottom = iDropsBottom;

    rcOut.left = m_ptPos.x;
    rcOut.top = m_ptPos.y;
    rcOut.right = m_ptPos.x + kPanelW;
    rcOut.bottom = m_ptPos.y + iContentBottom + kPad;
}

void
CMonsterInspectorPanel::GetCloseRect(RECT& rcOut) const {
    rcOut.left = m_ptPos.x + kPanelW - 26;
    rcOut.top = m_ptPos.y + 4;
    rcOut.right = m_ptPos.x + kPanelW - 6;
    rcOut.bottom = m_ptPos.y + 22;
}

void
CMonsterInspectorPanel::GetPreviewRect(RECT& rcOut) const {
    rcOut.left = m_ptPos.x + kPreviewX;
    rcOut.top = m_ptPos.y + kPreviewY;
    rcOut.right = rcOut.left + kPreviewW;
    rcOut.bottom = rcOut.top + kPreviewH;
}

//--------------------------------------------------------------------------------

void
CMonsterInspectorPanel::Update() {
    if (!m_bOpen)
        return;

    /// 살아있는 동안 HP 스냅샷 갱신( 죽거나 사라지면 마지막 값 유지 )
    CObjCHAR* pMob = GetTargetOBJ();
    if (pMob != NULL) {
        m_iLastHP = pMob->Get_HP();
        if (m_iLastHP < 0)
            m_iLastHP = 0;
        m_iLastMaxHP = pMob->Get_MaxHP();
    }
}

void
CMonsterInspectorPanel::Draw() {
    if (!m_bOpen)
        return;
    if (m_nCharIdx <= 0 || m_nCharIdx >= g_TblNPC.row_count)
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
    DrawPanelBg(iX, iY, kPanelW, iH);
    DrawPanelBg(rcPv.left, rcPv.top, kPreviewW, kPreviewH);

    /// 타이틀 + 닫기
    DrawTextAt(iX + kPad, iY + 4, kPanelW - kPad * 2, 18, kColTitle, DT_LEFT | DT_VCENTER,
        FONT_NORMAL_BOLD, "Monster Inspector");
    DrawTextAt(iX + kPanelW - 26, iY + 4, 20, 18, kColClose, DT_CENTER | DT_VCENTER,
        FONT_NORMAL_BOLD, "X");

    /// 이름 + 레벨
    const char* pName = NPC_NAME(m_nCharIdx);
    char szBuf[128];
    _snprintf(szBuf, sizeof(szBuf), "%s  (Lv. %d)", (pName != NULL) ? pName : "?",
        NPC_LEVEL(m_nCharIdx));
    szBuf[sizeof(szBuf) - 1] = '\0';
    DrawTextAt(iX + kInfoX, iY + kNameY, kInfoW, kNameH, kColName, DT_CENTER | DT_VCENTER,
        FONT_NORMAL_BOLD, szBuf);

    /// HP 게이지
    CResourceMgr* pRes = CResourceMgr::GetInstance();
    int iBgNID = pRes->GetImageNID(IMAGE_RES_UI, "UI00_GUAGE_BACKGROUND");
    int iRedNID = pRes->GetImageNID(IMAGE_RES_UI, "UI00_GUAGE_RED");

    int iBarX = iX + kInfoX;
    int iBarW = kInfoW;
    g_DrawImpl.DrawFit(iBarX, iY + kHpBarY, IMAGE_RES_UI, iBgNID, iBarW, kHpBarH,
        D3DCOLOR_ARGB(255, 255, 255, 255));
    if (m_iLastMaxHP > 0) {
        int iFillW = (int)((__int64)iBarW * m_iLastHP / m_iLastMaxHP);
        if (iFillW > iBarW)
            iFillW = iBarW;
        if (iFillW > 0)
            g_DrawImpl.DrawFit(iBarX, iY + kHpBarY, IMAGE_RES_UI, iRedNID, iFillW, kHpBarH,
                D3DCOLOR_ARGB(255, 255, 255, 255));
    }
    _snprintf(szBuf, sizeof(szBuf), "%d / %d", m_iLastHP, m_iLastMaxHP);
    szBuf[sizeof(szBuf) - 1] = '\0';
    DrawTextAt(iBarX, iY + kHpBarY, iBarW, kHpBarH, kColHp, DT_CENTER | DT_VCENTER,
        FONT_OUTLINE_11_BOLD, szBuf);

    /// 능력치 2컬럼 그리드
    struct StatLine {
        const char* szLabel;
        int iValue;
    };
    StatLine lines[kStatRows * 2] = {
        {"Attack", NPC_ATK(m_nCharIdx)},        {"M. Defense", NPC_RES(m_nCharIdx)},
        {"Accuracy", NPC_HIT(m_nCharIdx)},      {"Dodge", NPC_AVOID(m_nCharIdx)},
        {"Defense", NPC_DEF(m_nCharIdx)},       {"Mov. Speed", NPC_RUN_SPEED(m_nCharIdx)},
        {"Atk. Speed", NPC_ATK_SPEED(m_nCharIdx)}, {"EXP", NPC_GIVE_EXP(m_nCharIdx)},
    };

    int iColW = kInfoW / 2;
    for (int i = 0; i < kStatRows * 2; i++) {
        int iRow = i / 2;
        int iCol = i % 2;
        int iLineX = iX + kInfoX + iCol * iColW;
        int iLineY = iY + kStatY + iRow * kStatRowH;

        DrawTextAt(iLineX, iLineY, iColW - 60, kStatRowH, kColLabel, DT_LEFT | DT_VCENTER,
            FONT_NORMAL, lines[i].szLabel);
        _snprintf(szBuf, sizeof(szBuf), "%d", lines[i].iValue);
        szBuf[sizeof(szBuf) - 1] = '\0';
        DrawTextAt(iLineX + iColW - 64, iLineY, 56, kStatRowH, kColValue, DT_RIGHT | DT_VCENTER,
            FONT_NORMAL, szBuf);
    }

    /// 드랍 목록
    int iShown = (int)m_Drops.size();
    if (iShown > kMaxDropShown)
        iShown = kMaxDropShown;

    if ((int)m_Drops.size() > iShown)
        _snprintf(szBuf, sizeof(szBuf), "Drops (%d shown of %d)", iShown, (int)m_Drops.size());
    else
        _snprintf(szBuf, sizeof(szBuf), "Drops (%d)", iShown);
    szBuf[sizeof(szBuf) - 1] = '\0';
    DrawTextAt(iX + kInfoX, iY + kDropLabelY, kInfoW, 18, kColLabel, DT_LEFT | DT_VCENTER,
        FONT_NORMAL_BOLD, szBuf);

    int iHoverIdx = -1;
    for (int i = 0; i < iShown; i++) {
        int iIconX = iX + kInfoX + (i % kIconsPerRow) * kIconCell;
        int iIconY = iY + kDropGridY + (i / kIconsPerRow) * kIconCell;

        g_DrawImpl.Draw(iIconX, iIconY, IMAGE_RES_ITEM, m_Drops[i].iIconNo);

        if (m_ptHover.x >= iIconX && m_ptHover.x < iIconX + 40 && m_ptHover.y >= iIconY
            && m_ptHover.y < iIconY + 40) {
            iHoverIdx = i;
        }
    }

    /// 마우스가 올라간 드랍 아이템: 인벤토리와 동일한 아이템 툴팁 표시.
    /// CToolTipMgr 는 이 뒤( g_itMGR.Update -> DrawDLGs 끝 )에서 그려지므로
    /// 같은 프레임에 등록만 하면 UI 위에 정상 출력된다.
    if (iHoverIdx >= 0) {
        tagITEM sItem;
        sItem.Init((unsigned int)m_Drops[iHoverIdx].nType,
            (unsigned int)m_Drops[iHoverIdx].nItemNo,
            1);
        if (!sItem.IsEmpty()) {
            CItem TipItem(&sItem);
            CInfo Info;

            /// 인벤토리 아이콘( CIconItem::GetToolTip )과 동일: 우클릭 유지시 상세 정보
            DWORD dwInfoType = 0;
            if (GetAsyncKeyState(VK_RBUTTON) < 0)
                dwInfoType |= INFO_STATUS_DETAIL;

            TipItem.GetToolTip(Info, 0 /*dlg type: 가격 표기 없음*/, dwInfoType);

            POINT ptTip = {m_ptHover.x + 18, m_ptHover.y};
            Info.SetPosition(ptTip);
            CToolTipMgr::GetInstance().RegistInfo(Info);
        }
    }

    /// 3D 퍼핏은 맨 마지막에: 지금까지의 패널 스프라이트를 flush 한 뒤 그 위에 그린다.
    PuppetDrawInPane(rcPv);
}

//--------------------------------------------------------------------------------

bool
CMonsterInspectorPanel::OnLButtonDown(int x, int y) {
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

    /// 패널 위 클릭은 전부 소비( 클릭이 월드로 새어나가 공격하지 않도록 ) + 드래그 시작
    m_bDragging = true;
    m_ptDragGrab.x = x - m_ptPos.x;
    m_ptDragGrab.y = y - m_ptPos.y;
    return true;
}

bool
CMonsterInspectorPanel::OnMouseMove(int x, int y) {
    /// hover 는 항상 추적( 드랍 아이콘 이름 표시용 )
    m_ptHover.x = x;
    m_ptHover.y = y;

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
CMonsterInspectorPanel::OnLButtonUp(int /*x*/, int /*y*/) {
    if (!m_bDragging)
        return false;

    m_bDragging = false;
    return true;
}

//--------------------------------------------------------------------------------
/// 3D 프리뷰 퍼핏: 몬스터 모델의 클라이언트 전용 복제.
/// CObjMOB 생성 경로( CObjCHAR::CreateCHAR )의 최소 재현: 스켈리톤 + 정지모션으로
/// loadModel, CCharMODEL::CreatePART 로 각 파트 로드.
///
/// 씬에는 넣지 않는다. 렌더링은 엔진의 아바타 셀렉션 파이프라인으로 한다:
///   setAvatarViewPort( 팬 영역 ) -> RenderSelectedAvatar( 모델 ) -> setDefaultViewPort()
/// RenderSelectedAvatar 가 자체 직교 카메라( 원점 주시 )로 모델을 (0,0,-1.4) 에 두고
/// update_animation + render 를 직접 호출하므로 월드 카메라와 완전히 분리된다.
/// 메시/텍스처 디바이스 리소스는 화면에 보이는 실제 몬스터와 이름으로 공유되어
/// 이미 로드돼 있다( 인스펙터는 살아있는 타겟에만 열린다 ).
//--------------------------------------------------------------------------------

bool
CMonsterInspectorPanel::PuppetCreate(short nCharIdx) {
    PuppetDestroy();

    if (nCharIdx <= 0 || nCharIdx >= g_TblNPC.row_count)
        return false;

    CCharMODEL* pMODEL = g_MOBandNPC.GetMODEL(nCharIdx);
    if (pMODEL == NULL || pMODEL->GetPartCNT() <= 0) {
        LogString(LOG_DEBUG_, "MonsterInspector puppet: no CHR model for npc %d\n", nCharIdx);
        return false;
    }

    HNODE hSkel = pMODEL->GetZSKEL();
    if (hSkel == 0) {
        LogString(LOG_DEBUG_, "MonsterInspector puppet: no skeleton for npc %d\n", nCharIdx);
        return false;
    }

    tagMOTION* pMotion = pMODEL->GetMOTION(0 /*MOB_ANI_STOP*/);
    HNODE hMotion = (pMotion != NULL) ? pMotion->m_hMotion : 0;

    char* szName = NextPuppetName();
    m_hPuppetModel = ::loadModel(szName, hSkel, hMotion, 1.0f);
    if (m_hPuppetModel == 0) {
        LogString(LOG_DEBUG_, "MonsterInspector puppet: loadModel failed for npc %d\n", nCharIdx);
        return false;
    }

    /// 픽킹/충돌 대상이 되지 않도록
    ::setCollisionLevel(m_hPuppetModel, 0);

    for (short nP = 0; nP < MAX_BODY_PART; nP++)
        m_pPuppetPartVIS[nP] = pMODEL->CreatePART(szName, m_hPuppetModel, nP);

    /// 모델 높이는 월드 단위(cm). 0 이면 기본 160cm.
    m_fPuppetModelHeight = ::getModelHeight(m_hPuppetModel);
    if (m_fPuppetModelHeight < 10.0f)
        m_fPuppetModelHeight = 160.0f;

    if (hMotion)
        ::setRepeatCount(m_hPuppetModel, 0); /// 0 = 무한반복

    m_nPuppetCharIdx = nCharIdx;
    return true;
}

void
CMonsterInspectorPanel::PuppetDestroy() {
    if (m_hPuppetModel == 0)
        return;

    /// 씬에 삽입하지 않았으므로 removeFromScene 은 불필요.
    ::clearRenderUnit(m_hPuppetModel);

    CCharMODEL* pMODEL = NULL;
    if (m_nPuppetCharIdx > 0 && m_nPuppetCharIdx < g_TblNPC.row_count)
        pMODEL = g_MOBandNPC.GetMODEL(m_nPuppetCharIdx);

    for (short nP = 0; nP < MAX_BODY_PART; nP++) {
        if (m_pPuppetPartVIS[nP] != NULL && pMODEL != NULL)
            pMODEL->DeletePART(nP, m_pPuppetPartVIS[nP]);
        m_pPuppetPartVIS[nP] = NULL;
    }

    ::unloadModel(m_hPuppetModel);
    m_hPuppetModel = 0;
    m_nPuppetCharIdx = 0;
}

void
CMonsterInspectorPanel::PuppetDrawInPane(const RECT& rcPane) {
    if (m_hPuppetModel == 0 || !m_bOpen)
        return;

    float fPaneW = (float)(rcPane.right - rcPane.left);
    float fPaneH = (float)(rcPane.bottom - rcPane.top);
    if (fPaneW < 8.0f || fPaneH < 8.0f)
        return;

    /// 아바타 셀렉션 카메라 프레이밍( 엔진 내부 단위 = m, getModelHeight 는 cm ).
    /// RenderSelectedAvatar 는 모델(원점=발)을 z = -1.4 에 두므로, 모델 중심에
    /// 카메라 높이를 맞추고 직교 세로 폭( view_length / ratio )이 모델 높이보다
    /// 여유있게 크도록 view_length 를 잡는다.
    ///
    /// 엔진에는 증감( update* ) 인터페이스만 있어서 절대값을 지정하려면 현재값을
    /// 알아야 한다 — 싱글턴 기본값( 2.15 / -0.10 )에서 시작해 이 파일이 유일한
    /// 사용자이므로 정적 미러로 추적한다.
    static float s_fCurLength = 2.15f;
    static float s_fCurHeight = -0.10f;

    float fModelH = m_fPuppetModelHeight * 0.01f; /// cm -> m
    float fRatio = fPaneW / fPaneH;

    float fWantLength = fModelH * 1.2f * fRatio; /// 세로 fit + 20% 여유
    if (fWantLength < 0.7f)
        fWantLength = 0.7f; /// 초소형 몹 과도한 줌인 방지
    float fWantHeight = -1.4f + fModelH * 0.5f;  /// 모델 세로 중심

    ::updateAvatarSelectionCameraLength(fWantLength - s_fCurLength);
    s_fCurLength = fWantLength;
    ::updateAvatarSelectionCameraHeight(fWantHeight - s_fCurHeight);
    s_fCurHeight = fWantHeight;

    /// 천천히 궤도 회전( 라디안/프레임, 모델이 도는 것처럼 보인다 )
    ::updateAvatarSelectionCameraSeta(0.01f);

    /// 지금까지 그린 패널 스프라이트를 먼저 백버퍼로 밀어낸 뒤,
    /// 팬 영역 뷰포트에서 모델을 그리고 원래 파이프라인으로 복귀한다.
    ::flushSprite();
    ::setAvatarViewPort((float)rcPane.left, (float)rcPane.top, fPaneW, fPaneH);
    ::RenderSelectedAvatar(m_hPuppetModel);
    ::setDefaultViewPort();
}
