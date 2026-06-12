#include "stdafx.h"

#include ".\cinfo.h"
#include "IO_ImageRes.h"
#include "CTDrawImpl.h"
#include "tgamectrl/resourcemgr.h"
#include "../util/Localizing.h"

namespace {
///한 줄의 최대 픽셀 너비. 더 길면 단어 단위로 줄바꿈한다
const int kMaxToolTipLineWidth = 480;
///텍스트 줄 높이
const int kTextRowHeight = 17;
///빈 줄( 구분용 간격 ) 높이
const int kSeparatorRowHeight = 8;
///설명 줄바꿈 최소 너비
const int kMinWrapWidth = 140;

bool
IsBlankText(const char* pszTxt) {
    for (const char* p = pszTxt; *p; ++p) {
        if (*p != ' ' && *p != '\t')
            return false;
    }
    return true;
}

///명시적 개행을 지원하고, 단어(공백) 단위로 픽셀 너비에 맞춰 자른다.
///공백 없이 너무 긴 어절( CJK 등 )은 글자 단위( 코드페이지 인식 )로 자른다
std::vector<std::string>
WrapTextToWidth(const char* pszText, HNODE hFont, int iMaxWidthPx, WORD wCodePage) {
    std::vector<std::string> lines;
    if (pszText == NULL)
        return lines;

    if (iMaxWidthPx < 1)
        iMaxWidthPx = 1;

    std::string strLine;
    const char* pszCur = pszText;

    while (*pszCur) {
        if (*pszCur == ' ' || *pszCur == '\t') {
            ++pszCur;
            continue;
        }

        if (*pszCur == '\r' || *pszCur == '\n') {
            lines.push_back(strLine);
            strLine.clear();
            if (pszCur[0] == '\r' && pszCur[1] == '\n')
                ++pszCur;
            ++pszCur;
            continue;
        }

        const char* pszWordEnd = pszCur;
        while (*pszWordEnd && *pszWordEnd != ' ' && *pszWordEnd != '\t' && *pszWordEnd != '\r'
            && *pszWordEnd != '\n')
            pszWordEnd = CharNextExA(wCodePage, pszWordEnd, 0);

        std::string strWord(pszCur, pszWordEnd);
        pszCur = pszWordEnd;

        if (!strLine.empty()) {
            std::string strCandidate = strLine + " " + strWord;
            if (getFontTextExtent(hFont, strCandidate.c_str()).cx <= iMaxWidthPx) {
                strLine.swap(strCandidate);
                continue;
            }
            lines.push_back(strLine);
            strLine.clear();
        }

        ///단어 하나가 한 줄보다 길면 들어가는 만큼씩 잘라서 추가
        while (!strWord.empty() && getFontTextExtent(hFont, strWord.c_str()).cx > iMaxWidthPx) {
            const char* pszNext = CharNextExA(wCodePage, strWord.c_str(), 0);
            std::string strFit(strWord.c_str(), pszNext); ///최소 한 글자는 가져간다

            while (*pszNext) {
                const char* pszAfter = CharNextExA(wCodePage, pszNext, 0);
                std::string strMore(strWord.c_str(), pszAfter);
                if (getFontTextExtent(hFont, strMore.c_str()).cx > iMaxWidthPx)
                    break;
                strFit.swap(strMore);
                pszNext = pszAfter;
            }

            lines.push_back(strFit);
            strWord.erase(0, strFit.size());
        }
        strLine = strWord;
    }

    if (!strLine.empty())
        lines.push_back(strLine);

    return lines;
}
} // namespace

CInfo::CInfo(void) {
    Clear();
}

CInfo::~CInfo(void) {}
void
CInfo::Draw() {
    Finalize();

    RECT rcDraw = {2, 2, m_iWidth, kTextRowHeight};

    std::vector<stLine>::iterator iter;

    int iPosX = m_ptPosition.x;
    int iPosY = m_ptPosition.y;

    for (iter = m_listString.begin(); iter != m_listString.end(); ++iter) {
        DrawLineImage(iPosX, iPosY, m_iWidth, iter->nHeight);

        ///배경은 스케일된 트랜스폼을 남기므로, 글자는 평행이동만 적용해 그린다
        D3DXMATRIX mat;
        D3DXMatrixTranslation(&mat, (float)iPosX, (float)iPosY, 0);
        ::setTransformSprite(mat);

        iter->String.Draw(rcDraw);
        iPosY += iter->nHeight;
    }
}

bool
CInfo::IsEmpty() {
    return m_listString.empty();
}

void
CInfo::DrawLineImage(int x, int y, int iWidth, int iHeight) {
    m_iLineImageNo = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "ID_BLACK_PANEL");
    ///소스 영역을 넓히는 Draw는 텍스쳐 폭(512)에서 잘리므로 지오메트리 스케일로 그린다
    g_DrawImpl.DrawFit(x,
        y,
        IMAGE_RES_UI,
        m_iLineImageNo,
        iWidth,
        iHeight,
        D3DCOLOR_ARGB(180, 255, 255, 255));
}

void
CInfo::Clear() {
    m_iLineImageNo = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "ID_BLACK_PANEL");
    m_listString.clear();
    //	m_strTitle.clear();

    m_iHeight = 0;

    m_uMaxSizeString = 0;
    m_iWidth = 0;
    m_bWrapPending = false;

    m_ptPosition.x = 0;
    m_ptPosition.y = 0;
}

void
CInfo::SetPosition(POINT pt) {
    Finalize();
    m_ptPosition = pt;
    AdjustPosition();
}
int
CInfo::GetWidth() {
    Finalize();
    return m_iWidth;
}
int
CInfo::GetHeight() {
    Finalize();
    return m_iHeight;
}
int
CInfo::GetMaxSizeString() {
    return m_uMaxSizeString;
}
void
CInfo::AddString(CTString& TString) {
    AddString(TString.m_strText.c_str(), TString.m_dwColor, TString.m_uFormat);
}

void
CInfo::AddString(const char* pszTxt, DWORD color, HNODE hFont, UINT uFormat) {
    if (pszTxt == NULL) {
        Clear();
        return;
    }

    if (getFontTextExtent(hFont, pszTxt).cx + 5 <= kMaxToolTipLineWidth) {
        AppendLine(pszTxt, color, hFont, uFormat);
        return;
    }

    ///최대 너비를 넘는 줄은 단어 단위로 줄바꿈한다
    std::vector<std::string> lines = WrapTextToWidth(pszTxt,
        hFont,
        kMaxToolTipLineWidth - 5,
        (WORD)CLocalizing::GetSingleton().GetCurrentCodePageNO());

    for (size_t i = 0; i < lines.size(); ++i)
        AppendLine(lines[i].c_str(), color, hFont, uFormat);
}

void
CInfo::AddWrappedString(const char* pszTxt, DWORD color, HNODE hFont) {
    if (pszTxt == NULL || pszTxt[0] == '\0')
        return;

    ///설명 텍스트. 모든 줄이 추가된 뒤 Finalize에서 박스 최종 너비에 맞춰 줄바꿈된다
    stLine Line;
    Line.bWrapToWidth = true;
    Line.nHeight = 0;
    Line.String.SetColor(color);
    Line.String.SetString(pszTxt);
    Line.String.SetFormat(DT_LEFT);
    Line.String.SetFont(hFont);
    m_listString.push_back(Line);
    m_bWrapPending = true;
}

void
CInfo::AppendLine(const char* pszTxt, DWORD color, HNODE hFont, UINT uFormat) {
    stLine Line;
    Line.bWrapToWidth = false;

    if (IsBlankText(pszTxt)) {
        ///빈 줄은 전체 한 줄 대신 좁은 구분 간격으로 표시한다
        Line.nHeight = kSeparatorRowHeight;
    } else {
        Line.nHeight = kTextRowHeight;

        SIZE sizeString = getFontTextExtent(hFont, pszTxt);

        if (m_iWidth < sizeString.cx + 5)
            m_iWidth = sizeString.cx + 5;

        if (strlen(pszTxt) > m_uMaxSizeString)
            m_uMaxSizeString = strlen(pszTxt);
    }

    m_iHeight += Line.nHeight;

    Line.String.SetColor(color);
    Line.String.SetString(pszTxt);
    Line.String.SetFormat(uFormat);
    Line.String.SetFont(hFont);
    m_listString.push_back(Line);
    AdjustPosition();
}

void
CInfo::Finalize() {
    if (!m_bWrapPending)
        return;
    m_bWrapPending = false;

    ///확정된 박스 너비에 맞춰 보류된 설명들을 줄바꿈한다
    int iWrapWidth = m_iWidth - 5;
    if (iWrapWidth < kMinWrapWidth)
        iWrapWidth = kMinWrapWidth;
    if (iWrapWidth > kMaxToolTipLineWidth - 5)
        iWrapWidth = kMaxToolTipLineWidth - 5;

    WORD wCodePage = (WORD)CLocalizing::GetSingleton().GetCurrentCodePageNO();

    std::vector<stLine> Resolved;
    Resolved.reserve(m_listString.size() + 8);

    m_iHeight = 0;
    for (size_t i = 0; i < m_listString.size(); ++i) {
        stLine& Line = m_listString[i];

        if (!Line.bWrapToWidth) {
            m_iHeight += Line.nHeight;
            Resolved.push_back(Line);
            continue;
        }

        std::vector<std::string> lines = WrapTextToWidth(Line.String.m_strText.c_str(),
            Line.String.m_hFont,
            iWrapWidth,
            wCodePage);

        for (size_t n = 0; n < lines.size(); ++n) {
            stLine Seg = Line;
            Seg.bWrapToWidth = false;
            Seg.String.SetString(lines[n].c_str());

            if (IsBlankText(lines[n].c_str())) {
                Seg.nHeight = kSeparatorRowHeight;
            } else {
                Seg.nHeight = kTextRowHeight;

                SIZE sizeString = getFontTextExtent(Seg.String.m_hFont, lines[n].c_str());
                if (m_iWidth < sizeString.cx + 5)
                    m_iWidth = sizeString.cx + 5;

                if (lines[n].size() > m_uMaxSizeString)
                    m_uMaxSizeString = lines[n].size();
            }

            m_iHeight += Seg.nHeight;
            Resolved.push_back(Seg);
        }
    }

    m_listString.swap(Resolved);
    AdjustPosition();
}

void
CInfo::AdjustPosition() {
    if (m_ptPosition.x < 0)
        m_ptPosition.x = 0;
    if (m_ptPosition.y < 0)
        m_ptPosition.y = 0;

    if (m_ptPosition.x + m_iWidth > g_pCApp->GetWIDTH())
        m_ptPosition.x = g_pCApp->GetWIDTH() - m_iWidth;

    if (m_ptPosition.y + m_iHeight > g_pCApp->GetHEIGHT())
        m_ptPosition.y = g_pCApp->GetHEIGHT() - m_iHeight;

    // if( !m_strTitle.empty() )
    //	SetTitle( (char*)m_strTitle.c_str() );
}

// void CInfo::SetTitle( char* pszTitle  )
//{
//	assert( pszTitle );
//	if( pszTitle )
//	{
//		m_strTitle = pszTitle;
//		m_Title.SetDefaultColor( g_dwYELLOW );
//		m_Title.SetDefaultFont( FONT_NORMAL_BOLD );
//		int iStringWidthOnelinePrint = m_Title.SetString( pszTitle, m_iWidth );
//		if( m_iWidth < iStringWidthOnelinePrint )
//			m_iWidth = iStringWidthOnelinePrint;
//	}
//}