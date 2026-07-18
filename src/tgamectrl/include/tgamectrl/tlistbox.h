#ifndef _TLISTBOX_
#define _TLISTBOX_
#include "winctrl.h"
#include "IScrollModel.h"
#include <deque>

using namespace std;

#define TLB_MAX_LINE_LINKS 4
#define TLB_LINK_DATA_SIZE 8

/// Inline "link" segment inside a list line: a char range drawn in its own
/// color, hoverable, carrying an opaque payload for the owning dialog.
typedef struct {
    unsigned short m_wBegin; ///char offset into m_szTxt (inclusive)
    unsigned short m_wEnd; ///char offset into m_szTxt (exclusive)
    short m_xBegin; ///pixel x extent, precomputed at append time
    short m_xEnd;
    D3DCOLOR m_dwColor;
    unsigned char m_Data[TLB_LINK_DATA_SIZE]; ///opaque payload (owner-defined)
} t_list_link;

typedef struct {
    char m_szTxt[MAX_PATH];
    D3DCOLOR m_dwColor;
    bool m_bDrawn;
    unsigned char m_nLinkCount;
    t_list_link m_Links[TLB_MAX_LINE_LINKS];
} t_list_item;

typedef deque<t_list_item> li_item_vec;
typedef deque<t_list_item>::iterator li_item_vec_itor;
typedef deque<t_list_item>::const_iterator li_item_vec_const_itor;

class CTScrollBar;
class ITFont;

/**
 * ListBox Class : t_list_item을 item으로 가질수 있는 ListBox
 *	- ScrollBar를 붙일수 있도록 IScrollModel을 Implement한다.
 *
 * @Author	최종진
 *
 * @Date		2003/11/26
 */
class TGAMECTRL_API CTListBox: public CWinCtrl, public IScrollModel {
public:
    CTListBox(void);
    virtual ~CTListBox(void);

    bool Create(int iScrX,
        int iScrY,
        int iWidth,
        int iHeight,
        int iExtent,
        int iCharWidth,
        int iCharHeight);
    virtual unsigned int Process(UINT uiMsg, WPARAM wParam, LPARAM lParam);
    virtual void Update(POINT ptMouse);
    virtual void Draw();

    /// IScrollModel 구현부
    virtual int GetValue();
    virtual int GetExtent();
    virtual int GetMaximum();
    virtual int GetMinimum();

    virtual void SetValue(int i);
    virtual void SetExtent(int i);
    virtual void SetMaximum(int i);
    virtual void SetMinimum(int i);

    virtual RECT GetWindowRect();
    virtual bool IsLastItemDrawn();
    ///텍스트 추가 삭제
    void AppendText(const char* szTxt, D3DCOLOR dwColor, bool bAutoIncValue = true);
    ///링크 세그먼트를 포함한 라인 추가 (links must be sorted, non-overlapping)
    void AppendTextEx(const char* szTxt,
        D3DCOLOR dwColor,
        const t_list_link* pLinks,
        int iLinkCount,
        bool bAutoIncValue = true);
    ///마우스가 링크 위에 있으면 payload/좌표를 돌려준다 (updated during Draw)
    bool GetHoveredLink(unsigned char* pOutData, POINT& ptOut);
    ///hover 판정용 마우스 좌표 갱신 (Update 경로에서 매 프레임 호출해도 됨)
    void SetHoverMousePos(POINT pt) { m_ptLastMouse = pt; }
    void SetText(int index, const char* text, D3DCOLOR dwColor);
    void DeleteText(int iLineNo);
    const char* GetText(int iLineNo);
    t_list_item GetItem(int iLineNo);
    void ClearText(); ///모든 Item들을 지운다.
    ///
    const li_item_vec& GetList() { return m_ITM; }

    ///아이템 선택
    bool IsSelectable() { return m_bSelectable; }
    void SetSelectable(bool b) { m_bSelectable = b; }
    short GetSelectedItemID() { return m_iSelectedItem; }
    const char* GetSelectedItem();
    void SetSelectedItem(short i) { m_iSelectedItem = i; }
    ///라인스페이스 ( 행간 )
    void SetLineSpace(short i) { m_iLineSpace = i; }
    short GetLineSpace() { return m_iLineSpace; }
    ///
    void SetMaxSize(int i) { m_iMaxSize = i; }
    int GetMaxSize() { return m_iMaxSize; }
    ///폰트 크기
    void SetCharHeight(short i) { m_iCharHeight = i; }
    short GetCharHeight() { return m_iCharHeight; }

    void SetCharWidth(short i);
    short GetCharWidth() { return m_iCharWidth; }

    void SetFont(int iFont);

protected:
    ///리스트가 선택가능한 타입일경우 메세지 처리하는 method
    bool ProcessSelectable(UINT uiMsg, WPARAM wParam, LPARAM lParam);
    ///한 라인을 링크 세그먼트별로 나눠 그린다. 링크 hover도 여기서 판정.
    void DrawLineWithLinks(const t_list_item& itm, int iPosY);
    ///라인 완성 helper: 링크 오프셋 재계산 후 push_back
    void PushLineWithLinks(const char* szTxt,
        D3DCOLOR dwColor,
        const t_list_link* pLinks,
        int iLinkCount,
        int iLineBegin,
        int iLineEnd);

protected:
    li_item_vec m_ITM; ///리스트아이템
    short m_nPutLnNum; ///현재 찍을 첫번째 라인번호
    short m_nMaxLnCnt; ///최대 찍을수 있는 라인수
    short m_nMaxPutChar; ///한라인에찍을수있는 최대문자수
    short m_iSelectedItem; ///선택된 아이템
    bool m_bSelectable; ///아이템이 선택가능한가?
    short m_iLineSpace; ///행간
    short m_iCharHeight; ///폰트 높이
    short m_iCharWidth; ///폰트 넓이
    int m_iMaxSize; ///리스트에 추가될수 있는 아이템의 최대 갯수
    ITFont* m_pFontMgr;
    int m_iFont;
    ///링크 hover 상태 (Draw에서 갱신)
    POINT m_ptLastMouse;
    bool m_bLinkHovered;
    unsigned char m_HoveredLinkData[TLB_LINK_DATA_SIZE];
    POINT m_ptHoveredPos;
};
#endif ///_TLISTBOX_