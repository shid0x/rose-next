#include "StdAfx.h"

#include ".\tstatic.h"
#include "TControlMgr.h"
#include "TImage.h"
CTStatic::CTStatic(void) {
    m_pImage = NULL;
    m_pFontImpl = CTControlMgr::GetInstance()->GetFontMgr();
    m_dwColor = D3DCOLOR_ARGB(255, 255, 255, 255);
    m_iFont = 0;
    m_iClipWidth = 0;
    ///셀 렌더러는 Init()을 거치지 않고 직접 생성하므로 여기서 타입을 확정한다
    m_dwCtrlType = CTRL_STATIC;
}

CTStatic::~CTStatic(void) {
    if (m_pImage) {
        delete m_pImage;
        m_pImage = NULL;
    }
}

bool
CTStatic::Create(int iScrX, int iScrY, int iWidth, int iHeight) {
    CTStatic::CTStatic();
    Init(CTRL_STATIC, iScrX, iScrY, iWidth, iHeight);
    return true;
}

unsigned int
CTStatic::Process(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    return 0;
}

void
CTStatic::Update(POINT ptMouse) {}

void
CTStatic::Draw() {
    if (!IsVision())
        return;

    int iTextX = m_sPosition.x;
    if (m_pImage) {
        m_pImage->Draw(m_sPosition);
        iTextX += m_pImage->GetWidth();
    }

    ///클립 너비가 지정되어 있고 텍스트가 그보다 길면 말줄임표( .. )를 붙여 잘라낸다.
    ///엔진 폰트는 글자를 텍스쳐에 캐싱하므로 DT_END_ELLIPSIS는 통하지 않는다.
    ///그래서 CSinglelineString과 동일하게 문자열 자체를 미리 잘라 점좌표로 그린다
    std::string strClipped;
    const char* pszDraw = m_strText.c_str();
    if (m_iClipWidth > 0) {
        int iAvail = m_iClipWidth - (iTextX - m_sPosition.x);
        if (iAvail > 0 && m_pFontImpl->GetFontTextExtent(GetFont(), pszDraw).cx > iAvail) {
            const char* pszEllipsis = "..";
            int iEllipsisW = m_pFontImpl->GetFontTextExtent(GetFont(), pszEllipsis).cx;
            WORD wCodePage = (WORD)CTControlMgr::GetInstance()->GetCodePage();

            std::string strFit;
            const char* pszCur = pszDraw;
            while (*pszCur) {
                const char* pszNext = CharNextExA(wCodePage, pszCur, 0);
                std::string strCand(strFit);
                strCand.append(pszCur, pszNext);
                if (m_pFontImpl->GetFontTextExtent(GetFont(), strCand.c_str()).cx + iEllipsisW
                    > iAvail)
                    break;
                strFit.swap(strCand);
                pszCur = pszNext;
            }
            strClipped = strFit + pszEllipsis;
            pszDraw = strClipped.c_str();
        }
    }

    m_pFontImpl->Draw(GetFont(), true, iTextX, m_sPosition.y, m_dwColor, pszDraw);
}

void
CTStatic::SetString(const char* szString) {
    if (szString == NULL)
        return;
    int iSize = (int)strlen(szString) + 1;
    char* szTemp = new char[iSize];
    strncpy(szTemp, szString, iSize);
    m_strText = szTemp;
    delete[] szTemp;
}
const char*
CTStatic::toString() {
    return m_strText.c_str();
}

void
CTStatic::SetImage(CTImage* pImage) {
    m_pImage = pImage;
}
void
CTStatic::SetColor(DWORD dwColor) {
    m_dwColor = dwColor;
}

void
CTStatic::SetFont(int iFont) {
    m_iFont = iFont;
}

int
CTStatic::GetFont() {
    return m_iFont;
}

std::vector<CWinCtrl*>
CTStatic::GetChildren() {
    if (m_pImage) {
        return {m_pImage};
    }

    return {};
}