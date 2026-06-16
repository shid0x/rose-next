#include "stdafx.h"

#include ".\ctstring.h"

namespace {
/// Distinct color for tooltip category labels (a muted dark blue-ish).
const DWORD kToolTipCategoryColor = D3DCOLOR_ARGB(255, 95, 150, 225);
const char kTipCatBegin = TIP_CAT[0];
const char kTipCatEnd = TIP_END[0];
} // namespace

CTString::CTString(void) {}

CTString::~CTString(void) {}
void
CTString::SetString(const char* pszTxt) {
    m_strText = pszTxt;
}
void
CTString::SetColor(DWORD dwColor) {
    m_dwColor = dwColor;
}
void
CTString::SetFormat(UINT uFormat) {
    m_uFormat = uFormat;
}

void
CTString::SetFont(HNODE hFont) {
    m_hFont = hFont;
}

void
CTString::Draw(RECT& rcDraw) {
    /// Fast path: no inline category markup.
    if (m_strText.find(kTipCatBegin) == std::string::npos
        && m_strText.find(kTipCatEnd) == std::string::npos) {
        drawFont(m_hFont, true, &rcDraw, m_dwColor, m_uFormat, m_strText.c_str());
        return;
    }

    /// Inline category coloring lays runs out left-to-right, so it only makes
    /// sense for left-aligned text. For centered/right-aligned lines, strip the
    /// markup and draw the whole line in its base color.
    if ((m_uFormat & (DT_CENTER | DT_RIGHT)) != 0) {
        std::string strStripped;
        strStripped.reserve(m_strText.size());
        for (size_t i = 0; i < m_strText.size(); ++i) {
            char c = m_strText[i];
            if (c != kTipCatBegin && c != kTipCatEnd)
                strStripped.push_back(c);
        }
        drawFont(m_hFont, true, &rcDraw, m_dwColor, m_uFormat, strStripped.c_str());
        return;
    }

    /// Left-aligned: draw each colored run in sequence, advancing X by the
    /// measured width of the run just drawn. A toggle byte flushes the current
    /// run and switches the active color (category vs. the line's base color).
    RECT rcRun = rcDraw;
    DWORD dwColor = m_dwColor;
    std::string strRun;

    for (size_t i = 0; i <= m_strText.size(); ++i) {
        char c = (i < m_strText.size()) ? m_strText[i] : '\0';

        if (c == kTipCatBegin || c == kTipCatEnd || c == '\0') {
            if (!strRun.empty()) {
                drawFont(m_hFont, true, &rcRun, dwColor, m_uFormat, strRun.c_str());
                rcRun.left += getFontTextExtent(m_hFont, strRun.c_str()).cx;
                strRun.clear();
            }
            if (c == kTipCatBegin)
                dwColor = kToolTipCategoryColor;
            else if (c == kTipCatEnd)
                dwColor = m_dwColor;
        } else {
            strRun.push_back(c);
        }
    }
}
