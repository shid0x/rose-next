#include "stdafx.h"

#include "csystemmsgdlg.h"
#include "../../Game.h"
#include "../CHelpMgr.h"
#include "../CTDrawImpl.h"
#include "../IO_ImageRes.h"
#include "../../util/Localizing.h"
#include "tgamectrl/resourcemgr.h"
#include "zz_interface.h"
CSystemMsgDlg::CSystemMsgDlg(void) {
    m_hFont = g_GameDATA.m_hFONT[FONT_NORMAL];
    m_dwShowTime = 0;
}

CSystemMsgDlg::~CSystemMsgDlg(void) {}

void
CSystemMsgDlg::Draw() {
    if (!IsVision())
        return;
    if (m_stTitle.empty() || m_stMsg.empty())
        return;

    int iLineCount = m_Notice.GetLineCount();
    int iPosY = m_sPosition.y + 3;
    for (int i = 0; i < iLineCount; ++i) {
        // Scaled, not widened: Draw(width) widens the atlas source rect, which
        // clips at ID_BLACK_PANEL's 512 px and capped the banner there.
        g_DrawImpl.DrawFitW(m_sPosition.x,
            iPosY,
            IMAGE_RES_UI,
            m_iImageIndex,
            m_iWidth,
            D3DCOLOR_ARGB(128, 255, 255, 255));
        // DrawFitW leaves a scaled transform: the text gets the translation only.
        D3DXMATRIX mat;
        D3DXMatrixTranslation(&mat, (float)m_sPosition.x, (float)iPosY, 0);
        ::setTransformSprite(mat);
        ::drawFontf(m_hFont, true, 2, 2, m_Color, "%s", m_Notice.GetString(i));
        iPosY += 17;
    }
}

void
CSystemMsgDlg::Update(POINT ptMouse) {
    CTDialog::Update(ptMouse);

    DWORD dwNowTime = g_GameDATA.GetGameTime();

    if (dwNowTime - m_dwMsgSetTime >= m_dwShowTime) {
        m_Notice.Clear();
        m_stTitle.clear();
        m_stMsg.clear();
        Hide();
    }
}

unsigned int
CSystemMsgDlg::Process(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    return 0;
}

bool
CSystemMsgDlg::SetMessage(const char* szTitle,
    const char* szMsg,
    short iType,
    D3DCOLOR color,
    DWORD dwShowTime,
    HNODE hFont) {
    if (szMsg == NULL || szTitle == NULL)
        return false;

    /// 기존 팁이나 헬프일경우 공지사항을 우선순위로 표시한다.
    /// 공지사항일 경우에도 새로운 공지사항이 우선한다.
    if (m_Notice.GetLineCount() && m_iMsgType == MSG_TYPE_NOTICE)
        if (iType != MSG_TYPE_NOTICE)
            return false;

    if (hFont == NULL)
        m_hFont = g_GameDATA.m_hFONT[FONT_NORMAL];
    else
        m_hFont = hFont;

    if (color == NULL)
        m_Color = D3DCOLOR_XRGB(255, 255, 255);
    else
        m_Color = color;

    m_stTitle = szTitle;
    m_stMsg = szMsg;

    m_dwShowTime = dwShowTime;
    m_iMsgType = iType;

    m_dwMsgSetTime = g_GameDATA.GetGameTime();

    // The whole width between the status panel and the minimap, centred: the same
    // margin both sides covers the classic and the UI2 layouts (UI2's minimap is
    // wider than the classic dialog). It used to stop at 512 px, the background
    // sprite's width, which broke a server announcement in three on a wide screen.
    int iBaseInfoDlgWidth = 252;
    int iMapDlgWidth = 260;

    CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_MINIMAP);
    if (pDlg && pDlg->GetWidth() > iMapDlgWidth)
        iMapDlgWidth = pDlg->GetWidth();

    const int iMargin = (std::max)(iBaseInfoDlgWidth, iMapDlgWidth);
    m_iWidth = g_pCApp->GetWIDTH() - 2 * iMargin;
    if (m_iWidth < 512) { // a narrow window: fall back to the old placement
        m_iWidth = (std::min)(512, (std::max)(64, g_pCApp->GetWIDTH() - iBaseInfoDlgWidth - iMapDlgWidth));
        m_sPosition.x = iBaseInfoDlgWidth;
    } else {
        m_sPosition.x = iMargin;
    }

#ifdef _NEWUI
    m_sPosition.y = 30;
#else
    m_sPosition.y = 2;
#endif

    m_iImageIndex = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "ID_BLACK_PANEL");
    if (m_iImageIndex < 0)
        return false;

    m_Notice.Clear();

    std::string TempString;
    TempString = m_stTitle;
    TempString.append(" ");
    TempString.append(m_stMsg);

    m_Notice.Split(FONT_NORMAL,
        (char*)TempString.c_str(),
        m_iWidth,
        CLocalizing::GetSingleton().GetCurrentCodePageNO());

    Show();
    return true;
}
