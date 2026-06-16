#ifndef _CTString_
#define _CTString_

/// Inline color markup for tooltip lines. Wrap a category/label substring
/// between TIP_CAT and TIP_END so it renders in the distinct category color
/// while the rest of the line keeps its base color. These are string literals
/// so they concatenate with adjacent printf format literals:
///     CStr::Printf(TIP_CAT "%s" TIP_END ":%d", label, value)
/// Only honored for left-aligned CInfo lines (see CTString::Draw); the bytes
/// are stripped before width measurement in CInfo.
#define TIP_CAT "\x01" /// begin category-colored run
#define TIP_END "\x02" /// end category-colored run (back to base color)

/**
 * 초기에 작성된 string class : CInfo에서 사용중이다
 *
 * @Author		최종진
 * @Date			2005/9/5
 */
class CTString {
public:
    CTString(void);
    ~CTString(void);

    void SetString(const char* pszTxt);
    void SetColor(DWORD dwColor);
    void SetFormat(UINT uFormat);
    void SetFont(HNODE hFont);
    void Draw(RECT& rcDraw);

    std::string m_strText;
    DWORD m_dwColor;
    UINT m_uFormat;
    HNODE m_hFont;
};
#endif