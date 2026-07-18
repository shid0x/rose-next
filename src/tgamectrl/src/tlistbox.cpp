#include "StdAfx.h"

#include ".\tlistbox.h"
#include "TScrollBar.h"
#include "TSplitString.h"
#include "TControlMgr.h"
#include "ITFont.h"
#include <assert.h>
CTListBox::CTListBox(void) {
    m_iFont = 0;
    m_nPutLnNum = 0;
    m_nMaxLnCnt = 0;
    m_nMaxPutChar = 0;
    m_iSelectedItem = -1;
    m_bSelectable = false;
    m_iLineSpace = 2;
    m_iCharWidth = 6;
    m_iCharHeight = 6;
    m_iMaxSize = 0;
    m_pFontMgr = NULL;
    m_ptLastMouse.x = -1;
    m_ptLastMouse.y = -1;
    m_bLinkHovered = false;
    ZeroMemory(m_HoveredLinkData, sizeof(m_HoveredLinkData));
    m_ptHoveredPos.x = 0;
    m_ptHoveredPos.y = 0;
}

CTListBox::~CTListBox(void) {
    m_ITM.clear();
}

bool
CTListBox::Create(int iScrX,
    int iScrY,
    int iWidth,
    int iHeight,
    int iExtent,
    int iCharWidth,
    int iCharHeight) {
    m_iCharWidth = iCharWidth;
    m_iCharHeight = iCharHeight;
    m_nPutLnNum = 0;

    if (m_iCharWidth == 0)
        m_iCharWidth = 6;

    m_nMaxPutChar = iWidth / m_iCharWidth - 1;
    m_nMaxLnCnt = iExtent;
    Init(CTRL_LISTBOX, iScrX, iScrY, iWidth, iHeight);

    m_pFontMgr = CTControlMgr::GetInstance()->GetFontMgr();
    return true;
}
void
CTListBox::SetCharWidth(short i) {
    m_iCharWidth = i;
    if (m_iCharWidth == 0)
        m_iCharWidth = 6;
    m_nMaxPutChar = m_iWidth / m_iCharWidth - 1;
}
void
CTListBox::Update(POINT ptMouse) {}
bool
CTListBox::ProcessSelectable(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    if (uiMsg == WM_KEYDOWN) {
        switch (wParam) {
            case VK_DOWN: {
                m_iSelectedItem++;
                if (m_iSelectedItem >= GetMaximum())
                    m_iSelectedItem = GetMaximum() - 1;
                return true;
            } break;
            case VK_UP: {
                m_iSelectedItem--;
                if (m_iSelectedItem < 0)
                    m_iSelectedItem = 0;
                return true;
            } break;
            default:
                break;
        }
        /*		case WM_MOUSEWHEEL:
                    {
                        RECT rc = { m_sPosition.x, m_sPosition.y, m_iWidth + m_sPosition.x,
           m_iHeight + m_sPosition.y
                    }
                default:
                    break;

                RECT rcModel = {0,0,0,0};

                if( m_pScrollModel )
                {
                    rcModel = m_pScrollModel->GetWindowRect();
                    if( PtInRect( &rcModel, ptMouse ) == 0 && !IsInside( ptMouse.x, ptMouse.y) )
                        return 0;
                }
                int zDelta = (int)wParam;
                if( zDelta > 0)
                    SetValue( --iValue );
                else
                    SetValue( ++iValue );
                return uiMsg;*/
    }

    return false;
}
unsigned int
CTListBox::Process(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    if (!IsVision())
        return 0;

    POINT ptMouse = {LOWORD(lParam), HIWORD(lParam)};

    if (uiMsg == WM_MOUSEMOVE)
        m_ptLastMouse = ptMouse;

    if (uiMsg == WM_LBUTTONDOWN)
        int mm = 0;

    if (IsSelectable() && ProcessSelectable(uiMsg, wParam, lParam))
        return m_iControlID;

    if ((uiMsg == WM_LBUTTONDOWN || uiMsg == WM_LBUTTONDBLCLK) && IsInside(ptMouse.x, ptMouse.y)
        && IsSelectable()) {
        int iExtent = m_nMaxLnCnt;
        int iCount = 0;
        RECT rcHitTest = {0, 0, 0, 0};

        if (m_nPutLnNum + m_nMaxLnCnt
            >= (int)m_ITM.size()) ///리스트의 최대범위밖으로 안나가도록 수정
            iExtent = (int)m_ITM.size() - m_nPutLnNum;

        for (int i = m_nPutLnNum; i < m_nPutLnNum + iExtent; ++i, ++iCount) {
            rcHitTest.left = m_sPosition.x;
            rcHitTest.top = m_sPosition.y + iCount * (m_iLineSpace + m_iCharHeight);
            rcHitTest.right = rcHitTest.left + m_iWidth;
            rcHitTest.bottom = rcHitTest.top + m_iCharHeight;
            if (PtInRect(&rcHitTest, ptMouse)) {
                m_iSelectedItem = i;
                return GetControlID();
            }
        }
        //		m_iSelectedItem = -1;
    }
    return 0;
}

void
CTListBox::Draw() {
    m_bLinkHovered = false;

    if (!IsVision() || m_pFontMgr == NULL || m_bOwnerDraw)
        return;

    int iValue = GetValue(); ///첫번째 찍을 라인
    int iExtent = GetExtent(); ///화면에 보여줄수 있는 최대 라인수

    if (iValue + iExtent >= (int)m_ITM.size()) ///리스트의 최대범위밖으로 안나가도록 수정
        iExtent = (int)m_ITM.size() - iValue;

    m_pFontMgr->SetTransformSprite((float)m_sPosition.x, (float)m_sPosition.y);

    D3DCOLOR dwColor;

    int iPosY = 0;

    for (int i = iValue; i < iValue + iExtent; ++i) {
        if (i >= (int)m_ITM.size() || i < 0)
            break;

        if (m_iSelectedItem == i)
            dwColor = D3DCOLOR_ARGB(255, 255, 128, 0);
        else
            dwColor = m_ITM[i].m_dwColor;

        if (m_ITM[i].m_szTxt) {
            m_ITM[i].m_bDrawn = true;
            if (strlen(m_ITM[i].m_szTxt) > 0) {
                if (m_ITM[i].m_nLinkCount > 0)
                    DrawLineWithLinks(m_ITM[i], iPosY);
                else
                    m_pFontMgr->Draw(m_iFont, true, 0, iPosY, dwColor, m_ITM[i].m_szTxt);
            } else
                m_pFontMgr->Draw(m_iFont, true, 0, iPosY, dwColor, " ");
            iPosY += m_iCharHeight + m_iLineSpace;
        }
    }
}

///링크 라인은 plain / link 세그먼트를 각각의 색으로 나눠 그린다.
///픽셀 오프셋은 append 시점에 미리 계산된 값을 쓴다 (프레임당 폰트 측정 금지).
void
CTListBox::DrawLineWithLinks(const t_list_item& itm, int iPosY) {
    char szBuf[MAX_PATH];
    const int iLen = (int)strlen(itm.m_szTxt);
    int iCursor = 0; ///현재 char 오프셋
    int iPlainX = 0; ///다음 plain 세그먼트가 그려질 픽셀 x

    for (int k = 0; k < itm.m_nLinkCount; ++k) {
        const t_list_link& link = itm.m_Links[k];
        int iBegin = (int)link.m_wBegin;
        int iEnd = (int)link.m_wEnd;
        if (iBegin < iCursor || iEnd > iLen || iBegin >= iEnd)
            continue;

        if (iBegin > iCursor) {
            int iCnt = iBegin - iCursor;
            strncpy(szBuf, itm.m_szTxt + iCursor, iCnt);
            szBuf[iCnt] = '\0';
            m_pFontMgr->Draw(m_iFont, true, iPlainX, iPosY, itm.m_dwColor, szBuf);
        }

        int iCnt = iEnd - iBegin;
        strncpy(szBuf, itm.m_szTxt + iBegin, iCnt);
        szBuf[iCnt] = '\0';
        m_pFontMgr->Draw(m_iFont, true, (int)link.m_xBegin, iPosY, link.m_dwColor, szBuf);

        RECT rcLink = {m_sPosition.x + link.m_xBegin,
            m_sPosition.y + iPosY,
            m_sPosition.x + link.m_xEnd,
            m_sPosition.y + iPosY + m_iCharHeight};
        if (PtInRect(&rcLink, m_ptLastMouse)) {
            m_bLinkHovered = true;
            memcpy(m_HoveredLinkData, link.m_Data, TLB_LINK_DATA_SIZE);
            m_ptHoveredPos = m_ptLastMouse;
        }

        iCursor = iEnd;
        iPlainX = (int)link.m_xEnd;
    }

    if (iCursor < iLen)
        m_pFontMgr->Draw(m_iFont, true, iPlainX, iPosY, itm.m_dwColor, itm.m_szTxt + iCursor);
}

bool
CTListBox::GetHoveredLink(unsigned char* pOutData, POINT& ptOut) {
    if (!m_bLinkHovered)
        return false;
    if (pOutData)
        memcpy(pOutData, m_HoveredLinkData, TLB_LINK_DATA_SIZE);
    ptOut = m_ptHoveredPos;
    return true;
}

/*
void CTListBox::SelectItem()
{
    //텍스트 출력

    POINT sPT = { g_ScenMGR.m_iMosPosX,g_ScenMGR.m_iMosPosY };
    RECT  sRT;

    for(int i=0;i<m_nMaxLnCnt;i++) {
        SetRect(&sRT,m_iSX+3,m_iSY+5+(i*m_nOneLnHight),m_iSX+3+m_iWidth,m_iSY+25+(i*m_nOneLnHight));
        if(PtInRect(&sRT,sPT)) {
            m_nSelectLine = i+m_nPutLnNum;
            return;
        }
    }
}
*/

void
CTListBox::SetText(int index, const char* text, D3DCOLOR dwColor) {
    if (text == NULL)
        return;
    if (m_ITM.empty())
        return;
    if (index < 0 || index >= m_ITM.size())
        return;

    t_list_item itm;
    itm.m_nLinkCount = 0;
    int iLen = (int)strlen(text);
    if (iLen <= m_nMaxPutChar)
        strcpy(itm.m_szTxt, text);
    else
        strncpy(itm.m_szTxt, text, sizeof(itm.m_szTxt));

    itm.m_dwColor = dwColor;
    m_ITM[index] = itm;
}
///한라인에 표시할수 있는 최대문자길이보다 크면 잘라서 추가한다.
void
CTListBox::AppendText(const char* szTxt, D3DCOLOR dwColor, bool bAutoIncValue) {
    if (szTxt == NULL)
        return;

    ///최대 아이템수를 넘으면 제일먼저 들어온 아이템을 버린다.
    if ((m_iMaxSize > 0) && !m_ITM.empty() && (m_iMaxSize <= (int)m_ITM.size()))
        m_ITM.pop_front();

    t_list_item itm;
    itm.m_bDrawn = false;
    itm.m_nLinkCount = 0;
    int iLen = (int)strlen(szTxt);

    CTSplitString szString;
    SIZE szSize = szString.GetSizeText(m_iFont, szTxt);

    //홍근 :
    if (szSize.cx <= m_iWidth)
    // if( iLen <= m_nMaxPutChar )
    {
        strcpy(itm.m_szTxt, szTxt);
        itm.m_dwColor = dwColor;
        m_ITM.push_back(itm);
    } else {
        if (m_nMaxPutChar <= 0) {
            strcpy(itm.m_szTxt, szTxt);
            itm.m_dwColor = dwColor;
            m_ITM.push_back(itm);

        } else {
            CTSplitString TempString(m_iFont,
                (char*)szTxt,
                m_iWidth,
                CTControlMgr::GetInstance()->GetCodePage());
            for (int i = 0; i < TempString.GetLineCount(); ++i) {
                strcpy(itm.m_szTxt, TempString.GetString(i));
                itm.m_dwColor = dwColor;
                m_ITM.push_back(itm);
            }
        }
    }
    if (bAutoIncValue)
        SetValue(GetMaximum());
}

///완성된 한 라인( 소스 스트링의 [iLineBegin,iLineEnd) 구간 )을 링크 오프셋 재계산 후 추가.
///픽셀 extent는 여기서 한번만 측정한다.
void
CTListBox::PushLineWithLinks(const char* szTxt,
    D3DCOLOR dwColor,
    const t_list_link* pLinks,
    int iLinkCount,
    int iLineBegin,
    int iLineEnd) {
    if ((m_iMaxSize > 0) && !m_ITM.empty() && (m_iMaxSize <= (int)m_ITM.size()))
        m_ITM.pop_front();

    t_list_item itm;
    itm.m_bDrawn = false;
    itm.m_nLinkCount = 0;
    itm.m_dwColor = dwColor;

    int iCnt = iLineEnd - iLineBegin;
    if (iCnt < 0)
        iCnt = 0;
    if (iCnt >= (int)sizeof(itm.m_szTxt))
        iCnt = (int)sizeof(itm.m_szTxt) - 1;
    strncpy(itm.m_szTxt, szTxt + iLineBegin, iCnt);
    itm.m_szTxt[iCnt] = '\0';

    char szPrefix[MAX_PATH];
    for (int k = 0; k < iLinkCount && itm.m_nLinkCount < TLB_MAX_LINE_LINKS; ++k) {
        int iBegin = (int)pLinks[k].m_wBegin;
        int iEnd = (int)pLinks[k].m_wEnd;
        if (iBegin < iLineBegin || iEnd > iLineEnd || iBegin >= iEnd)
            continue;

        t_list_link& dst = itm.m_Links[itm.m_nLinkCount];
        dst = pLinks[k];
        dst.m_wBegin = (unsigned short)(iBegin - iLineBegin);
        dst.m_wEnd = (unsigned short)(iEnd - iLineBegin);

        int iPre = (int)dst.m_wBegin;
        SIZE szExtent = {0, 0};
        if (iPre > 0 && m_pFontMgr) {
            strncpy(szPrefix, itm.m_szTxt, iPre);
            szPrefix[iPre] = '\0';
            szExtent = m_pFontMgr->GetFontTextExtent(m_iFont, szPrefix);
        }
        dst.m_xBegin = (short)szExtent.cx;

        int iPreEnd = (int)dst.m_wEnd;
        szExtent.cx = dst.m_xBegin;
        if (m_pFontMgr) {
            strncpy(szPrefix, itm.m_szTxt, iPreEnd);
            szPrefix[iPreEnd] = '\0';
            szExtent = m_pFontMgr->GetFontTextExtent(m_iFont, szPrefix);
        }
        dst.m_xEnd = (short)szExtent.cx;

        ++itm.m_nLinkCount;
    }

    m_ITM.push_back(itm);
}

///링크 세그먼트를 절대 중간에서 자르지 않는 word-wrap 추가.
///측정은 append 시점 한정 (CTSplitString 주석의 "프레임마다 실행 금지" 준수).
void
CTListBox::AppendTextEx(const char* szTxt,
    D3DCOLOR dwColor,
    const t_list_link* pLinks,
    int iLinkCount,
    bool bAutoIncValue) {
    if (szTxt == NULL)
        return;

    if (iLinkCount <= 0 || pLinks == NULL) {
        AppendText(szTxt, dwColor, bAutoIncValue);
        return;
    }

    if (iLinkCount > TLB_MAX_LINE_LINKS)
        iLinkCount = TLB_MAX_LINE_LINKS;

    int iLen = (int)strlen(szTxt);

    SIZE szSize = {0, 0};
    if (m_pFontMgr)
        szSize = m_pFontMgr->GetFontTextExtent(m_iFont, szTxt);

    if (szSize.cx <= m_iWidth || m_nMaxPutChar <= 0 || m_pFontMgr == NULL) {
        PushLineWithLinks(szTxt, dwColor, pLinks, iLinkCount, 0, iLen);
    } else {
        unsigned uiCodePage = CTControlMgr::GetInstance()->GetCodePage();
        char szProbe[MAX_PATH];
        int iLineBegin = 0;
        int iPos = 0;
        int iLastSpace = -1;

        while (iPos < iLen) {
            ///단위: 링크 전체 or MBCS 문자 하나
            int iUnitEnd = -1;
            bool bIsLink = false;
            for (int k = 0; k < iLinkCount; ++k) {
                if ((int)pLinks[k].m_wBegin == iPos && pLinks[k].m_wEnd > pLinks[k].m_wBegin) {
                    iUnitEnd = (int)pLinks[k].m_wEnd;
                    bIsLink = true;
                    break;
                }
            }
            if (!bIsLink) {
                const char* pNext = CharNextExA((WORD)uiCodePage, szTxt + iPos, 0);
                iUnitEnd = (int)(pNext - szTxt);
                if (iUnitEnd <= iPos)
                    iUnitEnd = iPos + 1;
            }
            if (iUnitEnd > iLen)
                iUnitEnd = iLen;

            int iCnt = iUnitEnd - iLineBegin;
            if (iCnt >= (int)sizeof(szProbe))
                iCnt = (int)sizeof(szProbe) - 1;
            strncpy(szProbe, szTxt + iLineBegin, iCnt);
            szProbe[iCnt] = '\0';
            SIZE szProbeSize = m_pFontMgr->GetFontTextExtent(m_iFont, szProbe);

            if (szProbeSize.cx > m_iWidth && iPos > iLineBegin) {
                int iBreak = (iLastSpace > iLineBegin) ? iLastSpace + 1 : iPos;
                PushLineWithLinks(szTxt, dwColor, pLinks, iLinkCount, iLineBegin, iBreak);
                iLineBegin = iBreak;
                iLastSpace = -1;
                iPos = iBreak;
                continue;
            }

            if (!bIsLink && szTxt[iPos] == ' ')
                iLastSpace = iPos;
            iPos = iUnitEnd;
        }

        if (iLineBegin < iLen)
            PushLineWithLinks(szTxt, dwColor, pLinks, iLinkCount, iLineBegin, iLen);
    }

    if (bAutoIncValue)
        SetValue(GetMaximum());
}

void
CTListBox::ClearText() {
    m_ITM.clear();
    SetValue(0);
}
void
CTListBox::DeleteText(int iLineNo) {}

const char*
CTListBox::GetText(int iLineNo) {
    if (iLineNo < 0 || iLineNo >= (short)m_ITM.size())
        return NULL;

    return m_ITM[iLineNo].m_szTxt;
}

t_list_item
CTListBox::GetItem(int iLineNo) {
    t_list_item itm;
    ZeroMemory(&itm, sizeof(t_list_item));
    itm.m_bDrawn = true;
    if (iLineNo < 0 || iLineNo >= (short)m_ITM.size())
        return itm;
    return m_ITM[iLineNo];
}

int
CTListBox::GetValue() {
    return m_nPutLnNum;
}
int
CTListBox::GetExtent() {
    return m_nMaxLnCnt;
}
int
CTListBox::GetMaximum() {
    return (int)m_ITM.size();
}
int
CTListBox::GetMinimum() {
    return 0;
}

void
CTListBox::SetValue(int i) {
    if (GetMaximum() <= m_nMaxLnCnt) {
        m_nPutLnNum = 0;
    } else if (i > GetMaximum() - m_nMaxLnCnt) {
        m_nPutLnNum = GetMaximum() - m_nMaxLnCnt;
    } else {
        m_nPutLnNum = i;
    }

    if (m_nPutLnNum < 0)
        m_nPutLnNum = 0;
}

void
CTListBox::SetExtent(int i) {
    m_nMaxLnCnt = i;
}

void
CTListBox::SetMaximum(int i) {}

void
CTListBox::SetMinimum(int i) {}

RECT
CTListBox::GetWindowRect() {
    RECT rc = {m_sPosition.x, m_sPosition.y, m_sPosition.x + m_iWidth, m_sPosition.y + m_iHeight};
    return rc;
}

bool
CTListBox::IsLastItemDrawn() {
    t_list_item item = GetItem(GetMaximum() - 1);
    return item.m_bDrawn;
}

const char*
CTListBox::GetSelectedItem() {
    return GetText(m_iSelectedItem);
}

void
CTListBox::SetFont(int iFont) {
    m_iFont = iFont;
}