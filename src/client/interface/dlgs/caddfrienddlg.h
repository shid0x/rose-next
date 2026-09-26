#ifndef _AddFriendDlg_
#define _AddFriendDlg_
#include "tgamectrl/tdialog.h"
/**
 * 친구추가시 이름 입력 다이얼로그
 *
 * @Author		최종진
 * @Date			2005/9/12
 */
class CAddFriendDlg: public CTDialog {
public:
    CAddFriendDlg(int iDlgType);
    virtual ~CAddFriendDlg(void);

    virtual unsigned Process(unsigned uiMsg, WPARAM wParam, LPARAM lParam);

    /// The request for a typed name ( shared with UI2's community window ):
    /// false for an empty name or your own, which the dialog ignored; a
    /// friend already listed gets its notice and still counts as handled.
    static bool RequestAddFriend(const char* pszName);

protected:
    enum { IID_BTN_CLOSE = 10, IID_BTN_CONFIRM = 11, IID_EDITBOX = 20 };

protected:
    bool SendReqAddFriend(); /// 서버로 친구 추가 요청
};
#endif