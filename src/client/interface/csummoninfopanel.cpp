#include "stdafx.h"

#include "CSummonInfoPanel.h"

#include "..\\Game.h"
#include "../CApplication.h"
#include "../Object.h"
#include "../CObjUSER.h"

#include "CTDrawImpl.h"
#include "IO_ImageRes.h"
#include "tgamectrl/resourcemgr.h"

namespace {

/// 패널 레이아웃 상수 (픽셀)
const int kBoxW = 156;  /// 박스 너비
const int kBoxH = 84;   /// 박스 높이
const int kBoxGap = 4;  /// 소환몹 박스 사이 간격
const int kPad = 12;    /// 좌우 안쪽 여백

const int kNameY = 6;   /// 이름 줄 Y
const int kNameH = 18;
const int kStatY1 = 26; /// 능력치 1번째 줄 Y (ATK / DEF)
const int kStatY2 = 42; /// 능력치 2번째 줄 Y (LV / RES)
const int kStatH = 16;
const int kHpBarY = 62; /// HP 게이지 Y
const int kHpBarH = 16;

/// 색상
const D3DCOLOR kColName = D3DCOLOR_ARGB(255, 120, 255, 110);  /// 소환몹 이름 (녹색)
const D3DCOLOR kColStat = D3DCOLOR_ARGB(255, 235, 235, 235);  /// 능력치
const D3DCOLOR kColHp = D3DCOLOR_ARGB(255, 255, 255, 255);    /// HP 숫자
const D3DCOLOR kColBg = D3DCOLOR_ARGB(205, 255, 255, 255);    /// 배경(ID_BLACK_PANEL 틴트)

/// 화면 스프라이트 블록 안에서 절대 좌표에 텍스트를 그린다.
void
DrawTextAt(int x, int y, int w, int h, D3DCOLOR color, DWORD format, int iFont, const char* msg) {
    D3DXMATRIX mat;
    D3DXMatrixTranslation(&mat, (float)x, (float)y, 0.0f);
    ::setTransformSprite(mat);

    RECT rc = {0, 0, w, h};
    ::drawFont(g_GameDATA.m_hFONT[iFont], true, &rc, color, format | DT_SINGLELINE, msg);
}

/// 소환몹 한 마리분의 박스를 (boxX, boxY) 좌상단에 그린다.
/// 능력치(ATK/DEF/RES/LV)는 소환 시점에 서버 공식으로 스케일되어 info 에 저장된 값을
/// 사용한다(NPC 테이블 기본값이 아님). HP / 이름은 살아있는 오브젝트에서 실시간으로 읽는다.
void
DrawSummonBox(int boxX, int boxY, CObjCHAR* pMob, const SummonMobInfo& info) {
    CResourceMgr* pRes = CResourceMgr::GetInstance();

    /// 배경 패널
    int iBlackPanel = pRes->GetImageNID(IMAGE_RES_UI, "ID_BLACK_PANEL");
    g_DrawImpl.DrawFit(boxX, boxY, IMAGE_RES_UI, iBlackPanel, kBoxW, kBoxH, kColBg);

    /// 이름
    const char* pName = pMob->Get_NAME();
    DrawTextAt(boxX, boxY + kNameY, kBoxW, kNameH, kColName, DT_CENTER | DT_VCENTER, FONT_NORMAL_BOLD,
        (pName != NULL) ? pName : "?");

    /// 능력치 (2열 x 2행) — 진행도 반영 스케일 값
    int iAtk = info.iAtk;
    int iDef = info.iDef;
    int iLv = info.iSummonLevel;
    int iRes = info.iRes;

    char szBuf[64];
    int iColLX = boxX + kPad;
    int iColRX = boxX + kBoxW / 2;
    int iColW = kBoxW / 2 - kPad;

    _snprintf(szBuf, sizeof(szBuf), "ATK %d", iAtk);
    szBuf[sizeof(szBuf) - 1] = '\0';
    DrawTextAt(iColLX, boxY + kStatY1, iColW, kStatH, kColStat, DT_LEFT | DT_VCENTER, FONT_NORMAL,
        szBuf);

    _snprintf(szBuf, sizeof(szBuf), "DEF %d", iDef);
    szBuf[sizeof(szBuf) - 1] = '\0';
    DrawTextAt(iColRX, boxY + kStatY1, iColW, kStatH, kColStat, DT_LEFT | DT_VCENTER, FONT_NORMAL,
        szBuf);

    _snprintf(szBuf, sizeof(szBuf), "LV %d", iLv);
    szBuf[sizeof(szBuf) - 1] = '\0';
    DrawTextAt(iColLX, boxY + kStatY2, iColW, kStatH, kColStat, DT_LEFT | DT_VCENTER, FONT_NORMAL,
        szBuf);

    _snprintf(szBuf, sizeof(szBuf), "RES %d", iRes);
    szBuf[sizeof(szBuf) - 1] = '\0';
    DrawTextAt(iColRX, boxY + kStatY2, iColW, kStatH, kColStat, DT_LEFT | DT_VCENTER, FONT_NORMAL,
        szBuf);

    /// HP 게이지 + 숫자
    int iHP = pMob->Get_HP();
    if (iHP < 0)
        iHP = 0;
    int iMaxHP = pMob->Get_MaxHP();

    int iBarX = boxX + kPad;
    int iBarW = kBoxW - kPad * 2;

    int iBgNID = pRes->GetImageNID(IMAGE_RES_UI, "UI00_GUAGE_BACKGROUND");
    int iRedNID = pRes->GetImageNID(IMAGE_RES_UI, "UI00_GUAGE_RED");

    g_DrawImpl.DrawFit(iBarX, boxY + kHpBarY, IMAGE_RES_UI, iBgNID, iBarW, kHpBarH,
        D3DCOLOR_ARGB(255, 255, 255, 255));

    if (iMaxHP > 0) {
        int iFillW = iBarW * iHP / iMaxHP;
        if (iFillW > 0)
            g_DrawImpl.DrawFit(iBarX, boxY + kHpBarY, IMAGE_RES_UI, iRedNID, iFillW, kHpBarH,
                D3DCOLOR_ARGB(255, 255, 255, 255));
    }

    _snprintf(szBuf, sizeof(szBuf), "%d / %d", iHP, iMaxHP);
    szBuf[sizeof(szBuf) - 1] = '\0';
    DrawTextAt(iBarX, boxY + kHpBarY, iBarW, kHpBarH, kColHp, DT_CENTER | DT_VCENTER,
        FONT_OUTLINE_11_BOLD, szBuf);
}

} // namespace

CSummonInfoPanel::CSummonInfoPanel()
    : m_bPositionInit(false)
    , m_bDragging(false) {
    m_ptPos.x = 0;
    m_ptPos.y = 0;
    m_ptDragGrab.x = 0;
    m_ptDragGrab.y = 0;
}

CSummonInfoPanel::~CSummonInfoPanel() {}

int
CSummonInfoPanel::GetSummonCount() const {
    if (g_pAVATAR == NULL)
        return 0;

    int iCount = 0;
    const std::list<SummonMobInfo>& list = g_pAVATAR->GetSummonedMobList();
    for (std::list<SummonMobInfo>::const_iterator it = list.begin(); it != list.end(); ++it) {
        CObjCHAR* pMob = g_pObjMGR->Get_ClientCharOBJ(it->iIndex, false);
        if (pMob != NULL)
            ++iCount;
    }
    return iCount;
}

void
CSummonInfoPanel::EnsureDefaultPosition() {
    if (m_bPositionInit)
        return;

    /// 기본 위치: 화면 우측, 미니맵 아래로 충분히 내려온 곳
    int iScrW = g_pCApp->GetWIDTH();
    m_ptPos.x = iScrW - kBoxW - 16;
    m_ptPos.y = 240;
    m_bPositionInit = true;
}

bool
CSummonInfoPanel::GetStackRect(RECT& rcOut) const {
    int iCount = GetSummonCount();
    if (iCount <= 0)
        return false;

    rcOut.left = m_ptPos.x;
    rcOut.top = m_ptPos.y;
    rcOut.right = m_ptPos.x + kBoxW;
    rcOut.bottom = m_ptPos.y + iCount * kBoxH + (iCount - 1) * kBoxGap;
    return true;
}

void
CSummonInfoPanel::Draw() {
    if (g_pAVATAR == NULL)
        return;
    if (GetSummonCount() <= 0)
        return;

    EnsureDefaultPosition();

    int iBoxY = m_ptPos.y;
    const std::list<SummonMobInfo>& list = g_pAVATAR->GetSummonedMobList();
    for (std::list<SummonMobInfo>::const_iterator it = list.begin(); it != list.end(); ++it) {
        CObjCHAR* pMob = g_pObjMGR->Get_ClientCharOBJ(it->iIndex, false);
        if (pMob == NULL)
            continue;

        DrawSummonBox(m_ptPos.x, iBoxY, pMob, *it);
        iBoxY += kBoxH + kBoxGap;
    }
}

bool
CSummonInfoPanel::OnLButtonDown(int x, int y) {
    RECT rc;
    if (!GetStackRect(rc))
        return false;

    if (x < rc.left || x > rc.right || y < rc.top || y > rc.bottom)
        return false;

    m_bDragging = true;
    m_ptDragGrab.x = x - m_ptPos.x;
    m_ptDragGrab.y = y - m_ptPos.y;
    return true;
}

bool
CSummonInfoPanel::OnMouseMove(int x, int y) {
    if (!m_bDragging)
        return false;

    m_ptPos.x = x - m_ptDragGrab.x;
    m_ptPos.y = y - m_ptDragGrab.y;

    /// 화면 밖으로 완전히 사라지지 않도록 살짝 클램프
    int iScrW = g_pCApp->GetWIDTH();
    int iScrH = g_pCApp->GetHEIGHT();
    if (m_ptPos.x < 0)
        m_ptPos.x = 0;
    if (m_ptPos.y < 0)
        m_ptPos.y = 0;
    if (m_ptPos.x > iScrW - kBoxW)
        m_ptPos.x = iScrW - kBoxW;
    if (m_ptPos.y > iScrH - kBoxH)
        m_ptPos.y = iScrH - kBoxH;

    return true;
}

bool
CSummonInfoPanel::OnLButtonUp(int /*x*/, int /*y*/) {
    if (!m_bDragging)
        return false;

    m_bDragging = false;
    return true;
}
