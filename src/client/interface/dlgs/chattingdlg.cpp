
#include "StdAfx.h"

#include "ChattingDLG.h"
#include "CTargetMenu.h"
#include "ExchangeDlg.h"
#include "CPartyDlg.h"
#include "../CToolTipMgr.h"
#include "../CInfo.h"
#include "../CUIMediator.h"
#include "../../GameCommon/Item.h"
#include "../../GameData/CExchange.h"
#include "../../Network/CNetwork.h"
#include "util/string_util.h"
#include "../../GameData/CParty.h"
#include "../../GameData/CDamageMeter.h"
#include "../../Util/Localizing.h"

#include "tgamectrl/teditbox.h"
#include "tgamectrl/tlistbox.h"
#include "tgamectrl/tradiobox.h"
#include "tgamectrl/tscrollbar.h"
#include "tgamectrl/actionevent.h"
#include "tgamectrl/tcontrolmgr.h"

using namespace Rose;

CChatDLG::CChatDLG() {

    m_bChatBlock = false;
    m_dwChatBlockStartTime = 0;
    m_dwChatBlockTime = 0;

    m_bCheckShoutRestrict = true;
    m_dwShoutRestrictTime = 20 * 1000;
    m_dwPrevUseShoutTime = 0;
    // m_dwChatRestrictTime	= 1  * 1000;
    // m_dwPrevSendChatTime	= 0;

    m_iSelectedTab = TAB_ALL;

    ChatFilter filter;
    filter.Filters[0] = 1; /// NORMAL
    filter.Filters[1] = 1; /// SYSTEM
    filter.Filters[2] = 1; /// TRADE
    filter.Filters[3] = 1; /// PARTY
    filter.Filters[4] = 1; /// CLAN
    filter.Filters[5] = 1; /// ALLIED

    m_Filters.push_back(filter); /// All

    filter.Filters[0] = 0;
    filter.Filters[1] = 1;
    filter.Filters[2] = 0;
    filter.Filters[3] = 0;
    filter.Filters[4] = 0;
    filter.Filters[5] = 0;
    m_Filters.push_back(filter); /// Whisper

    filter.Filters[0] = 0;
    filter.Filters[1] = 1;
    filter.Filters[2] = 2;
    filter.Filters[3] = 0;
    filter.Filters[4] = 0;
    filter.Filters[5] = 0;
    m_Filters.push_back(filter); /// Trade

    filter.Filters[0] = 0;
    filter.Filters[1] = 1;
    filter.Filters[2] = 0;
    filter.Filters[3] = 2;
    filter.Filters[4] = 0;
    filter.Filters[5] = 0;
    m_Filters.push_back(filter); /// Party

    filter.Filters[0] = 0;
    filter.Filters[1] = 1;
    filter.Filters[2] = 0;
    filter.Filters[3] = 0;
    filter.Filters[4] = 2;
    filter.Filters[5] = 0;
    m_Filters.push_back(filter); /// Clan

    filter.Filters[0] = 0;
    filter.Filters[1] = 1;
    filter.Filters[2] = 0;
    filter.Filters[3] = 0;
    filter.Filters[4] = 0;
    filter.Filters[5] = 2;
    m_Filters.push_back(filter); /// Allied

    std::wstring wstrTemp;

    /// ASCii
    Util::MulityByte2WideString("!", wstrTemp);
    m_ShoutCommands.push_back(wstrTemp);
    Util::MulityByte2WideString("$", wstrTemp);
    m_TradeCommands.push_back(wstrTemp);
    Util::MulityByte2WideString("@", wstrTemp);
    m_WhisperCommands.push_back(wstrTemp);
    Util::MulityByte2WideString("&", wstrTemp);
    m_ClanCommands.push_back(wstrTemp);
    Util::MulityByte2WideString("#", wstrTemp);
    m_PartyCommands.push_back(wstrTemp);
    Util::MulityByte2WideString("~", wstrTemp);
    m_AlliedCommands.push_back(wstrTemp);
    Util::MulityByte2WideString(CStr::Printf("/%s", STR_HELP), wstrTemp);
    m_HelpCommands.push_back(wstrTemp);
    Util::MulityByte2WideString(" ", wstrTemp);
    m_Spaces.push_back(wstrTemp);

    /// 2바이트 특문/ 일본어 전각시에도 처리가능하도록 추가한다.
    wchar_t wchar[2] = {0, 0};
    wchar[0] = 0xFF01;
    m_ShoutCommands.push_back(wchar);
    wchar[0] = 0xFF04;
    m_TradeCommands.push_back(wchar);
    wchar[0] = 0xFF20;
    m_WhisperCommands.push_back(wchar);
    wchar[0] = 0xFF06;
    m_ClanCommands.push_back(wchar);
    wchar[0] = 0xFF03;
    m_PartyCommands.push_back(wchar);
    wchar[0] = 0xFF5E;
    m_AlliedCommands.push_back(wchar);
    wchar[0] = 0x3000;
    m_Spaces.push_back(wchar);

    wchar[0] = 0xFF0F;
    std::wstring wstrHelp = wchar;
    Util::MulityByte2WideString(STR_HELP, wstrTemp);
    wstrHelp.append(wstrTemp);
    m_HelpCommands.push_back(wstrHelp);

    /// 5초안에 3번만 보낼수 있다
    m_iCheckChatCount = 3; /// 3
    m_dwCheckChatTime = 5 * 1000; ///
}

CChatDLG::~CChatDLG() {}

void
CChatDLG::Update(POINT ptMouse) {
    CTDialog::Update(ptMouse);

    FeedItemLinkHover(ptMouse);

    ///이하 툴팁 표시
    CTDialog* pDlg = CTDialog::GetProcessMouseOverDialog();
    if (pDlg && pDlg != this)
        return;

    WINCTRL_LIST_ITOR iter;
    CWinCtrl* pCtrl;

    for (iter = m_listChild.begin(); iter != m_listChild.end(); ++iter) {
        pCtrl = *iter;
        if (pCtrl->GetControlType() == CTRL_IMAGE)
            continue;

        if (pCtrl->IsInside(ptMouse.x, ptMouse.y)) {
            CToolTipMgr::GetInstance().RegUIInfo(ptMouse.x,
                ptMouse.y,
                GetDialogType(),
                pCtrl->GetControlID());
            break;
        }
    }
}

void
CChatDLG::Draw() {
    CTDialog::Draw();

    ///링크 hover는 리스트박스 Draw중에 판정되므로 그 직후에 툴팁을 등록한다
    ///( CToolTipMgr는 g_itMGR.Update() 끝에서 그리므로 같은 프레임에 표시됨 )
    DrawHoveredItemLinkTooltip();
}

namespace {
const int s_ChatListBoxIDs[] = {
    CChatDLG::IID_LISTBOX_ALL,
    CChatDLG::IID_LISTBOX_WHISPER,
    CChatDLG::IID_LISTBOX_TRADE,
    CChatDLG::IID_LISTBOX_PARTY,
    CChatDLG::IID_LISTBOX_CLAN,
    CChatDLG::IID_LISTBOX_ALLIED,
    CChatDLG::IDD_LISTBOX_TOP,
};
const int s_ChatListBoxCount = sizeof(s_ChatListBoxIDs) / sizeof(s_ChatListBoxIDs[0]);

///토큰을 파싱해서 링크 세그먼트가 있으면 AppendTextEx로, 없으면 기존 경로로 추가
void
AppendChatLineParsed(CTListBox* pListBox, const char* pszMsg, DWORD dwColor, bool bAutoInc) {
    char szDisplay[512];
    ChatItemLinkRange sRanges[CHAT_ITEM_LINK_MAX_PER_MSG];
    int iLinks = ChatItemLink_Decode(pszMsg,
        szDisplay,
        sizeof(szDisplay),
        sRanges,
        CHAT_ITEM_LINK_MAX_PER_MSG);

    if (iLinks <= 0) {
        pListBox->AppendText(pszMsg, dwColor, bAutoInc);
        return;
    }

    t_list_link sLinks[CHAT_ITEM_LINK_MAX_PER_MSG];
    for (int i = 0; i < iLinks; ++i) {
        ZeroMemory(&sLinks[i], sizeof(t_list_link));
        sLinks[i].m_wBegin = (unsigned short)sRanges[i].iBegin;
        sLinks[i].m_wEnd = (unsigned short)sRanges[i].iEnd;
        sLinks[i].m_dwColor = sRanges[i].dwColor;
        memcpy(sLinks[i].m_Data, sRanges[i].Item, sizeof(sRanges[i].Item));
    }
    pListBox->AppendTextEx(szDisplay, dwColor, sLinks, iLinks, bAutoInc);
}
} // namespace

void
CChatDLG::FeedItemLinkHover(POINT ptMouse) {
    for (int i = 0; i < s_ChatListBoxCount; ++i) {
        CWinCtrl* pCtrl = Find(s_ChatListBoxIDs[i]);
        if (pCtrl && pCtrl->GetControlType() == CTRL_LISTBOX)
            ((CTListBox*)pCtrl)->SetHoverMousePos(ptMouse);
    }
}

void
CChatDLG::DrawHoveredItemLinkTooltip() {
    for (int i = 0; i < s_ChatListBoxCount; ++i) {
        CWinCtrl* pCtrl = Find(s_ChatListBoxIDs[i]);
        if (pCtrl == NULL || pCtrl->GetControlType() != CTRL_LISTBOX)
            continue;

        unsigned char byData[8];
        POINT ptHover;
        if (!((CTListBox*)pCtrl)->GetHoveredLink(byData, ptHover))
            continue;

        tagITEM sItem;
        if (!ChatItemLink_ItemFromBytes(byData, sItem))
            return;

        CItem TipItem(&sItem);
        CInfo Info;
        DWORD dwInfoType = 0;
        if (GetAsyncKeyState(VK_RBUTTON) < 0)
            dwInfoType |= INFO_STATUS_DETAIL;
        TipItem.GetToolTip(Info, 0, dwInfoType);

        POINT ptTip = ptHover;
        ptTip.x += 12;
        ptTip.y += 12;
        Info.SetPosition(ptTip);
        CToolTipMgr::GetInstance().RegistInfo(Info);
        return;
    }
}

bool
CChatDLG::TryOpenHoveredItemLinkPreview() {
    for (int i = 0; i < s_ChatListBoxCount; ++i) {
        CWinCtrl* pCtrl = Find(s_ChatListBoxIDs[i]);
        if (pCtrl == NULL || pCtrl->GetControlType() != CTRL_LISTBOX)
            continue;

        unsigned char byData[8];
        POINT ptHover;
        if (!((CTListBox*)pCtrl)->GetHoveredLink(byData, ptHover))
            continue;

        tagITEM sItem;
        if (!ChatItemLink_ItemFromBytes(byData, sItem))
            return false;

        ///장비 아이템만 열린다 — 소모품 링크 등은 소비하지 않고 기존 동작 유지
        return g_UIMed.OpenItemPreview(sItem);
    }
    return false;
}

bool
CChatDLG::AddItemLinkToInput(tagITEM& sItem) {
    std::string strDisplay = ChatItemLink_DisplayName(sItem);
    if (strDisplay.empty())
        return false;

    CWinCtrl* pCtrl = Find(IID_EDITBOX);
    if (pCtrl == NULL || pCtrl->GetControlType() != CTRL_EDITBOX)
        return false;
    CTEditBox* pEditBox = (CTEditBox*)pCtrl;

    ///입력창에서 지워진 링크는 버린다 ( cap이 유령 엔트리로 차지 않도록 )
    PurgeStalePendingItemLinks(pEditBox->get_text());

    if ((int)m_PendingItemLinks.size() >= CHAT_ITEM_LINK_MAX_PER_MSG) {
        g_itMGR.AppendChatMsg("You can only link 3 items per message.",
            IT_MGR::CHAT_TYPE_SYSTEM);
        return false;
    }

    ///AppendText는 입력 제한 초과시 조용히 거부하므로 실제로 붙었는지 확인한다
    const char* szBefore = pEditBox->get_text();
    int iLenBefore = szBefore ? (int)strlen(szBefore) : 0;
    pEditBox->AppendText((char*)strDisplay.c_str());
    const char* szAfter = pEditBox->get_text();
    int iLenAfter = szAfter ? (int)strlen(szAfter) : 0;
    if (iLenAfter < iLenBefore + (int)strDisplay.size())
        return false;
    pEditBox->AppendText(" ");

    PendingItemLink sLink;
    sLink.strDisplay = strDisplay;
    sLink.strToken = ChatItemLink_Encode(sItem);
    m_PendingItemLinks.push_back(sLink);

    pEditBox->SetFocus(true);
    return true;
}

void
CChatDLG::PurgeStalePendingItemLinks(const char* szCurrentInput) {
    std::vector<PendingItemLink>::iterator iter = m_PendingItemLinks.begin();
    while (iter != m_PendingItemLinks.end()) {
        if (szCurrentInput == NULL || strstr(szCurrentInput, iter->strDisplay.c_str()) == NULL)
            iter = m_PendingItemLinks.erase(iter);
        else
            ++iter;
    }
}

void
CChatDLG::SubstitutePendingItemLinks(std::string& stMsg) {
    size_t nFrom = 0;
    for (size_t i = 0; i < m_PendingItemLinks.size(); ++i) {
        const PendingItemLink& sLink = m_PendingItemLinks[i];
        size_t nPos = stMsg.find(sLink.strDisplay, nFrom);
        if (nPos == std::string::npos)
            nPos = stMsg.find(sLink.strDisplay); ///사용자가 순서를 바꿔 편집했을 때
        if (nPos == std::string::npos)
            continue; ///이름이 편집돼 사라짐 → 평문으로 전송 (링크 포기)
        stMsg.replace(nPos, sLink.strDisplay.size(), sLink.strToken);
        nFrom = nPos + sLink.strToken.size();
    }
}

unsigned int
CChatDLG::Process(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    // if( wParam == VK_RETURN && uiMsg == WM_KEYDOWN )
    //{
    //	CWinCtrl* pCtrl = Find( IID_EDITBOX );
    //	if( pCtrl == NULL )
    //		return 0;

    //	if( pCtrl->GetControlType() != CTRL_EDITBOX )
    //		return 0;

    //	CTEditBox* pEditBox = (CTEditBox*)pCtrl;
    //	if( pEditBox->IsFocus() )
    //	{
    //		SendChatMsg(pEditBox->get_text());

    //		switch( m_iSelectedTab )
    //		{
    //		case TAB_PARTY:
    //			if( CWinCtrl* pCtrl = Find( IID_EDITBOX ) )
    //			{
    //				CTEditBox* pBox = (CTEditBox*)pCtrl;

    //				if( pBox->get_text() == NULL )
    //					pBox->AppendText("#");
    //				else if( strlen(pBox->get_text()) < 1 )
    //					pBox->AppendText("#");
    //			}
    //			break;
    //		case TAB_CLAN:
    //			if( CWinCtrl* pCtrl = Find( IID_EDITBOX ) )
    //			{
    //				CTEditBox* pBox = (CTEditBox*)pCtrl;
    //				if( pBox->get_text() == NULL )
    //					pBox->AppendText("&");
    //				else if( strlen(pBox->get_text()) < 1 )
    //					pBox->AppendText("&");
    //			}
    //			break;
    //		case TAB_ALLIED:
    //			{
    //				if( CWinCtrl* pCtrl = Find( IID_EDITBOX ) )
    //				{
    //					CTEditBox* pBox = (CTEditBox*)pCtrl;
    //					if( pBox->get_text() == NULL )
    //						pBox->AppendText("~");
    //					else if( strlen(pBox->get_text()) < 1 )
    //						pBox->AppendText("~");
    //				}
    //			}
    //			break;
    //		default:
    //			break;
    //		}
    //		/////내가 채팅 메세지를 보내면 항상 리스트는 맨밑으로 이동
    //		//if( int iActiveListBox = GetActiveListBox() )
    //		//{
    //		//	CWinCtrl* pCtrl = Find( iActiveListBox );
    //		//	if( pCtrl )
    //		//	{
    //		//		CTListBox* pListBox = (CTListBox*)pCtrl;
    //		//		if( int iActiveScrollBar = GetActiveScrollBar() )
    //		//		{
    //		//			pCtrl = Find( iActiveScrollBar );
    //		//			if( pCtrl && pCtrl->GetControlType() == CTRL_SCROLLBAR )
    //		//			{
    //		//				CTScrollBar* pScrollBar = (CTScrollBar*)pCtrl;
    //		//				pScrollBar->SetValue( pListBox->GetMaximum() );
    //		//			}
    //		//		}
    //		//	}
    //		//}
    //	}
    //	else
    //	{
    //		pEditBox->SetFocus(true);
    //	}
    //	return uiMsg;
    //}

    ///ALT+click 된 아이템 링크: 장비 미리보기 창 열기( 링크 위가 아니면 통과 )
    if (uiMsg == WM_LBUTTONDOWN && GetAsyncKeyState(VK_MENU) < 0) {
        if (TryOpenHoveredItemLinkPreview())
            return uiMsg;
    }

    unsigned iProcID = 0;
    if (iProcID = CTDialog::Process(uiMsg, wParam, lParam)) {
        switch (uiMsg) {
            case WM_MOUSEWHEEL:
                return uiMsg;
                break;

            case WM_LBUTTONUP: {
                if (On_LButtonUP(iProcID))
                    return uiMsg;
            } break;
            case WM_LBUTTONDOWN: {
                OnLButtonDown(iProcID, wParam, lParam);
                if (iProcID == 1) /// Dialog를 Click했다면  이동가능하도록
                    return 0;
            }
        }
        return uiMsg;
    }

    switch (uiMsg) {
        case WM_KEYDOWN:

            switch (wParam) {
                case VK_SPACE: {
                    // 20050902 홍근 2인승 카트 : 스페이스바로 내가 보조석에 탔을 때 내리기.
                    if (CTControlMgr::GetInstance()->GetKeyboardInputType()
                            == CTControlMgr::INPUTTYPE_NORMAL
                        && NULL == CTEditBox::s_pFocusEdit && g_pAVATAR->IsRideUser()) {
                        g_pNet->Send_cli_TOGGLE(TOGGLE_TYPE_DRIVE);
                    }
                } break;
            }
    }

    return 0;
}

bool
CChatDLG::On_LButtonUP(unsigned iProcID) {
    switch (iProcID) {
        case IID_BTN_FILTER:
            g_itMGR.OpenDialog(DLG_TYPE_CHATFILTER);
            break;
        case IDD_BTN_SCROLLDOWN:
            // g_itMGR.CloseDialog( DLG_TYPE_CHAT );
            ActiveListBoxMoveEnd();
            break;
        default:
            break;
    }

    return true;
}

void
CChatDLG::Show() {
    CTDialog::Show();

    if (it_GetKeyboardInputType() == CTControlMgr::INPUTTYPE_AUTOENTER) {
        CWinCtrl* pCtrl = Find(IID_EDITBOX);
        if (pCtrl && pCtrl->GetControlType() == CTRL_EDITBOX)
            pCtrl->SetFocus(true);
    }
    TabSelected(TAB_ALL);
}

void
CChatDLG::SendChatMsg(char* szMsg) {
    if (szMsg == NULL)
        return;

    if (strlen(szMsg) < 1)
        return;

    ActiveListBoxMoveEnd();

    if (IsChatBlock()) {
        g_itMGR.AppendChatMsg(STR_STATE_BLOCK_CHAT, IT_MGR::CHAT_TYPE_SYSTEM);
        return;
    }
    string stMsg = szMsg;

    /// Local client commands — handled here, never sent to the server.
    /// "/dps" toggles the damage meter panel, "/dps reset" clears its data.
    if (stMsg == "/dps" || stMsg == "/dps reset") {
        if (stMsg == "/dps reset") {
            CDamageMeter::GetInstance().Reset();
            g_itMGR.AppendChatMsg("Damage meter reset.", IT_MGR::CHAT_TYPE_SYSTEM);
        } else {
            g_UIMed.ToggleDamageMeter();
        }

        CWinCtrl* pEditCtrl = Find(IID_EDITBOX);
        if (pEditCtrl != NULL && pEditCtrl->GetControlType() == CTRL_EDITBOX)
            ((CTEditBox*)pEditCtrl)->clear_text();
        return;
    }

    ///아이템 링크: "[Name]" → wire token 치환 ( GM 명령어는 제외 )
    if (stMsg[0] != '/')
        SubstitutePendingItemLinks(stMsg);

    ///서버는 129 byte 초과 채팅을 IS_HACKING 처리한다 — 넘길 바에 보내지 않는다.
    ///여기서 return하면 입력창/pending 링크가 유지되어 사용자가 줄여서 재전송할 수 있다.
    if ((int)stMsg.size() > CHAT_MSG_WIRE_MAX) {
        g_itMGR.AppendChatMsg("Message too long to send.", IT_MGR::CHAT_TYPE_SYSTEM);
        return;
    }

    m_PendingItemLinks.clear();
    m_strLastSendMsg = stMsg;

    string stTargetID;
    string stRealMsg;

    int iChatType = ChatParser(stMsg, stRealMsg, stTargetID);

    ///위치에 주의할껏 szMsg의 포인터가 파괴될수 있다.
    CWinCtrl* pCtrl = Find(IID_EDITBOX);
    if (pCtrl == NULL)
        return;
    if (pCtrl->GetControlType() != CTRL_EDITBOX)
        return;

    CTEditBox* pEditBox = (CTEditBox*)pCtrl;
    pEditBox->clear_text();

    DWORD dwCurrentTime = g_GameDATA.GetTime();

    if (m_bCheckShoutRestrict) //&& ( dwCurrentTime - m_dwPrevSendChatTime) < m_dwChatRestrictTime )
    {
        if (m_PrevSendMessageTimes.size() >= (unsigned int)m_iCheckChatCount) {
            DWORD prev_time = m_PrevSendMessageTimes.front(); //
            if (dwCurrentTime - prev_time < m_dwCheckChatTime) {
                g_itMGR.AppendChatMsg(STR_CHAT_RESTRICT, IT_MGR::CHAT_TYPE_SYSTEM);
            }
            m_PrevSendMessageTimes.pop();
        }
        m_PrevSendMessageTimes.push(dwCurrentTime);
    }

    // m_dwPrevSendChatTime = dwCurrentTime;

    switch (iChatType) {
        case CHAT_ALLIED:
            if (g_pAVATAR
                && g_pAVATAR->Get_TeamNO() != 2) /// Defalt Team No일경우에는 동맹쳇을 보내지 않는다
                g_pNet->Send_cli_ALLIED_SHOUT((char*)stRealMsg.c_str());
            break;
        case CHAT_OPEN_HELP:
            g_itMGR.OpenDialog(DLG_TYPE_HELP);
            break;
        case CHAT_CLAN: {
            g_pNet->Send_cli_CLAN_CHAT((char*)stRealMsg.c_str());

            pEditBox = (CTEditBox*)pCtrl;
            std::string stTemp = "&";
            stTemp.append(stTargetID);
            pEditBox->AppendText((char*)stTemp.c_str());
            pEditBox->AppendText(" ");
        } break;
        case CHAT_PARTY: {
            g_pNet->Send_cli_PARTY_CHAT((char*)stRealMsg.c_str());

            pEditBox = (CTEditBox*)pCtrl;
            std::string stTemp = "#";
            stTemp.append(stTargetID);
            pEditBox->AppendText((char*)stTemp.c_str());
            pEditBox->AppendText(" ");
        } break;
        case CHAT_NORMAL:
            g_pNet->Send_cli_CHAT((char*)stRealMsg.c_str());
            break;
        case CHAT_SHOUT:
            if (m_bCheckShoutRestrict
                && (dwCurrentTime - m_dwPrevUseShoutTime) < m_dwShoutRestrictTime) {
                g_itMGR.AppendChatMsg(
                    CStr::Printf(F_STR_RESTRICT_SHOUT, m_dwShoutRestrictTime / 1000),
                    IT_MGR::CHAT_TYPE_SYSTEM);
                break;
            }
            g_pNet->Send_cli_SHOUT((char*)stRealMsg.c_str());
            m_dwPrevUseShoutTime = g_GameDATA.GetTime();
            break;
        case CHAT_WHISPER: {
            ///서버에 날리고
            g_pNet->Send_cli_WHISPER((char*)stTargetID.c_str(), (char*)stRealMsg.c_str());

            ///자신의 채팅창에 메세지 뿌리고
            string Temp = g_pAVATAR->Get_NAME();
            Temp.append(">");
            Temp.append(stMsg);
            g_itMGR.AppendChatMsg(Temp.c_str(), IT_MGR::CHAT_TYPE_WHISPER);
            ///에디트박스에 @ID를 추가한다.
            pEditBox = (CTEditBox*)pCtrl;
            std::string stTemp = "@";
            stTemp.append(stTargetID);
            pEditBox->AppendText((char*)stTemp.c_str());
            pEditBox->AppendText(" ");

            break;
        }
        case CHAT_EXCHANGE: {
            CObjCHAR* pObjChar = g_pObjMGR->Get_ClientCharOBJByName(stTargetID.c_str());
            if (pObjChar) {
                if (CExchange::GetInstance().SendCliTradeReq(
                        g_pObjMGR->Get_ServerObjectIndex(pObjChar->Get_INDEX()))) {
                    ///자신의 채팅창에 메세지 뿌리고
                    string Temp = stTargetID;
                    Temp.append(STR_REQ_TRADE);
                    g_itMGR.AppendChatMsg(Temp.c_str(), IT_MGR::CHAT_TYPE_SYSTEM);
                }
            }
            break;
        }
        default:
            g_pNet->Send_cli_CHAT((char*)stRealMsg.c_str());
            break;
    }
}

int
CChatDLG::ChatParser(string stMsg, string& stRealMsg, string& stTargetID) {
    const char* pszMsg = stMsg.c_str();

    std::wstring wstrMsg;
    Util::MulityByte2WideString(pszMsg, wstrMsg);

    std::vector<std::wstring>::iterator iter;
    std::wstring::size_type index;

    for (iter = m_HelpCommands.begin(); iter != m_HelpCommands.end(); ++iter) {
        index = wstrMsg.find(*iter);
        if (index == 0)
            return CHAT_OPEN_HELP;
    }

    if (wstrMsg.length() < 2) {
        stRealMsg = stMsg;
        return CHAT_NORMAL;
    }

    for (iter = m_ShoutCommands.begin(); iter != m_ShoutCommands.end(); ++iter) {
        index = wstrMsg.find(*iter);
        if (index == 0) {
            std::wstring wstrRealMsg = wstrMsg.substr(1, wstrMsg.size() - 1);
            Util::Wide2MultiByteString(wstrRealMsg, stRealMsg);
            return CHAT_SHOUT;
        }
    }

    for (iter = m_AlliedCommands.begin(); iter != m_AlliedCommands.end(); ++iter) {
        index = wstrMsg.find(*iter);
        if (index == 0) {
            std::wstring wstrRealMsg = wstrMsg.substr(1, wstrMsg.size() - 1);
            Util::Wide2MultiByteString(wstrRealMsg, stRealMsg);
            return CHAT_ALLIED;
        }
    }

    // for( iter = m_TradeCommands.begin(); iter != m_TradeCommands.end(); ++iter )
    //{
    //	index = wstrMsg.find( *iter );
    //	if( index == 0 )
    //	{
    //		std::wstring wstrRealMsg = wstrMsg.substr( 1, wstrMsg.size() - 1 );
    //		Util.Wide2MultyByteString( wstrRealMsg, stRealMsg );
    //		return CHAT_TRADE;
    //	}
    //}

    for (iter = m_ClanCommands.begin(); iter != m_ClanCommands.end(); ++iter) {
        index = wstrMsg.find(*iter);
        if (index == 0) {
            std::wstring wstrRealMsg = wstrMsg.substr(1, wstrMsg.size() - 1);
            Util::Wide2MultiByteString(wstrRealMsg, stRealMsg);
            return CHAT_CLAN;
        }
    }

    if (CParty::GetInstance().HasParty()) {
        for (iter = m_PartyCommands.begin(); iter != m_PartyCommands.end(); ++iter) {
            index = wstrMsg.find(*iter);
            if (index == 0) {
                std::wstring wstrRealMsg = wstrMsg.substr(1, wstrMsg.size() - 1);
                Util::Wide2MultiByteString(wstrRealMsg, stRealMsg);
                return CHAT_PARTY;
            }
        }
    }

    for (iter = m_WhisperCommands.begin(); iter != m_WhisperCommands.end(); ++iter) {
        index = wstrMsg.find(*iter);
        if (index == 0) {

            for (iter = m_Spaces.begin(); iter != m_Spaces.end(); ++iter) {
                index = wstrMsg.find(*iter);
                if (index != std::wstring::npos) {
                    if (index >= 2 && index < wstrMsg.length()) {
                        std::wstring wstrTargetID = wstrMsg.substr(1, index - 1);
                        Util::Wide2MultiByteString(wstrTargetID, stTargetID);

                        std::wstring wstrRealMsg = wstrMsg.substr(index, wstrMsg.length() - index);
                        Util::Wide2MultiByteString(wstrRealMsg, stRealMsg);
                        return CHAT_WHISPER;
                    } else {
                        break;
                    }
                }
            }
            break;
        }
    }

    stRealMsg = stMsg;
    return CHAT_NORMAL;
}

void
CChatDLG::SetChatBlock(DWORD BlockTime) {
    m_bChatBlock = true;
    m_dwChatBlockStartTime = g_GameDATA.GetGameTime();
    m_dwChatBlockTime = BlockTime;
}

void
CChatDLG::SetChatUnBlock() {
    m_bChatBlock = false;
    m_dwChatBlockStartTime = 0;
    m_dwChatBlockTime = 0;
}

bool
CChatDLG::IsChatBlock() {
    if (m_bChatBlock && timeGetTime() - m_dwChatBlockStartTime >= m_dwChatBlockTime)
        SetChatUnBlock();

    return m_bChatBlock;
}

/// 상황에 따라 List와 ScrollBar의 Scroll을 구분한다.
void
CChatDLG::AppendMsg(const char* pszMsg, DWORD color, int iType) {
    AppendMsg2All(pszMsg, color, iType);
    AppendMsg2Whisper(pszMsg, color, iType);
    AppendMsg2Trade(pszMsg, color, iType);
    AppendMsg2Party(pszMsg, color, iType);
    AppendMsg2Clan(pszMsg, color, iType);
    AppendMsg2Allied(pszMsg, color, iType);
}

void
CChatDLG::AppendMsg2(const char* pszMsg, DWORD color, int iType) {
    AppendMsg2sys(pszMsg, color, iType);
}

void
CChatDLG::SetEnableShoutRestrict() {
    m_bCheckShoutRestrict = true;
}

void
CChatDLG::SetDisableShoutRestrict() {
    m_bCheckShoutRestrict = false;
}

void
CChatDLG::SendChatMsgRepeat() {
    if (!m_strLastSendMsg.empty())
        SendChatMsg((char*)m_strLastSendMsg.c_str());
}

void
CChatDLG::TabSelected(int iTab) {
    m_iSelectedTab = iTab;
    switch (iTab) {
        case TAB_ALL: {
            ShowChild(IID_LISTBOX_ALL);
            ShowChild(IID_SCROLLBAR_ALL);
            HideChild(IID_LISTBOX_WHISPER);
            HideChild(IID_SCROLLBAR_WHISPER);
            HideChild(IID_LISTBOX_TRADE);
            HideChild(IID_SCROLLBAR_TRADE);
            HideChild(IID_LISTBOX_PARTY);
            HideChild(IID_SCROLLBAR_PARTY);
            HideChild(IID_LISTBOX_CLAN);
            HideChild(IID_SCROLLBAR_CLAN);
            HideChild(IID_LISTBOX_ALLIED);
            HideChild(IID_SCROLLBAR_ALLIED);
            CWinCtrl* pCtrl = NULL;
            if (pCtrl = Find(IID_LISTBOX_ALL)) {
                CTListBox* pListBox = (CTListBox*)pCtrl;
                pListBox->SetValue(pListBox->GetMaximum());
            }
        } break;
        case TAB_WHISPER: {
            ShowChild(IID_LISTBOX_WHISPER);
            ShowChild(IID_SCROLLBAR_WHISPER);
            HideChild(IID_LISTBOX_ALL);
            HideChild(IID_SCROLLBAR_ALL);
            HideChild(IID_LISTBOX_TRADE);
            HideChild(IID_SCROLLBAR_TRADE);
            HideChild(IID_LISTBOX_PARTY);
            HideChild(IID_SCROLLBAR_PARTY);
            HideChild(IID_LISTBOX_CLAN);
            HideChild(IID_SCROLLBAR_CLAN);
            HideChild(IID_LISTBOX_ALLIED);
            HideChild(IID_SCROLLBAR_ALLIED);
            CWinCtrl* pCtrl = NULL;
            if (pCtrl = Find(IID_LISTBOX_WHISPER)) {
                CTListBox* pListBox = (CTListBox*)pCtrl;
                pListBox->SetValue(pListBox->GetMaximum());
            }
        } break;
        case TAB_TRADE: {
            ShowChild(IID_LISTBOX_TRADE);
            ShowChild(IID_SCROLLBAR_TRADE);
            HideChild(IID_LISTBOX_ALL);
            HideChild(IID_SCROLLBAR_ALL);
            HideChild(IID_LISTBOX_WHISPER);
            HideChild(IID_SCROLLBAR_WHISPER);
            HideChild(IID_LISTBOX_PARTY);
            HideChild(IID_SCROLLBAR_PARTY);
            HideChild(IID_LISTBOX_CLAN);
            HideChild(IID_SCROLLBAR_CLAN);
            HideChild(IID_LISTBOX_ALLIED);
            HideChild(IID_SCROLLBAR_ALLIED);
            CWinCtrl* pCtrl = NULL;
            if (pCtrl = Find(IID_LISTBOX_TRADE)) {
                CTListBox* pListBox = (CTListBox*)pCtrl;
                pListBox->SetValue(pListBox->GetMaximum());
            }
        } break;
        case TAB_PARTY: {
            ShowChild(IID_LISTBOX_PARTY);
            ShowChild(IID_SCROLLBAR_PARTY);
            HideChild(IID_LISTBOX_ALL);
            HideChild(IID_SCROLLBAR_ALL);
            HideChild(IID_LISTBOX_WHISPER);
            HideChild(IID_SCROLLBAR_WHISPER);
            HideChild(IID_LISTBOX_TRADE);
            HideChild(IID_SCROLLBAR_TRADE);
            HideChild(IID_LISTBOX_CLAN);
            HideChild(IID_SCROLLBAR_CLAN);
            HideChild(IID_LISTBOX_ALLIED);
            HideChild(IID_SCROLLBAR_ALLIED);
            CWinCtrl* pCtrl = NULL;
            if (pCtrl = Find(IID_LISTBOX_PARTY)) {
                CTListBox* pListBox = (CTListBox*)pCtrl;
                pListBox->SetValue(pListBox->GetMaximum());
            }
        } break;
        case TAB_CLAN: {
            ShowChild(IID_LISTBOX_CLAN);
            ShowChild(IID_SCROLLBAR_CLAN);

            HideChild(IID_LISTBOX_ALL);
            HideChild(IID_SCROLLBAR_ALL);
            HideChild(IID_LISTBOX_WHISPER);
            HideChild(IID_SCROLLBAR_WHISPER);
            HideChild(IID_LISTBOX_TRADE);
            HideChild(IID_SCROLLBAR_TRADE);
            HideChild(IID_LISTBOX_PARTY);
            HideChild(IID_SCROLLBAR_PARTY);
            HideChild(IID_LISTBOX_ALLIED);
            HideChild(IID_SCROLLBAR_ALLIED);
            CWinCtrl* pCtrl = NULL;
            if (pCtrl = Find(IID_LISTBOX_CLAN)) {
                CTListBox* pListBox = (CTListBox*)pCtrl;
                pListBox->SetValue(pListBox->GetMaximum());
            }
        } break;
        case TAB_ALLIED: {
            HideChild(IID_LISTBOX_CLAN);
            HideChild(IID_SCROLLBAR_CLAN);
            HideChild(IID_LISTBOX_ALL);
            HideChild(IID_SCROLLBAR_ALL);
            HideChild(IID_LISTBOX_WHISPER);
            HideChild(IID_SCROLLBAR_WHISPER);
            HideChild(IID_LISTBOX_TRADE);
            HideChild(IID_SCROLLBAR_TRADE);
            HideChild(IID_LISTBOX_PARTY);
            HideChild(IID_SCROLLBAR_PARTY);
            ShowChild(IID_LISTBOX_ALLIED);
            ShowChild(IID_SCROLLBAR_ALLIED);
            CWinCtrl* pCtrl = NULL;
            if (pCtrl = Find(IID_LISTBOX_ALLIED)) {
                CTListBox* pListBox = (CTListBox*)pCtrl;
                pListBox->SetValue(pListBox->GetMaximum());
            }
        } break;
        default:
            break;
    }
}

int
CChatDLG::GetActiveListBox() {
    switch (m_iSelectedTab) {
        case TAB_ALL:
            return IID_LISTBOX_ALL;
        case TAB_WHISPER:
            return IID_LISTBOX_WHISPER;
        case TAB_TRADE:
            return IID_LISTBOX_TRADE;
        case TAB_PARTY:
            return IID_LISTBOX_PARTY;
        case TAB_CLAN:
            return IID_LISTBOX_CLAN;
        case TAB_ALLIED:
            return IID_LISTBOX_ALLIED;
        default:
            break;
    }
    return 0;
}

int
CChatDLG::GetActiveScrollBar() {
    switch (m_iSelectedTab) {
        case TAB_ALL:
            return IID_SCROLLBAR_ALL;
        case TAB_WHISPER:
            return IID_SCROLLBAR_WHISPER;
        case TAB_TRADE:
            return IID_SCROLLBAR_TRADE;
        case TAB_PARTY:
            return IID_SCROLLBAR_PARTY;
        case TAB_CLAN:
            return IID_SCROLLBAR_CLAN;
        case TAB_ALLIED:
            return IID_SCROLLBAR_ALLIED;
        default:
            break;
    }
    return 0;
}
// Dagnarus
void
CChatDLG::AppendMsg2System(const char* pszMsg, DWORD dwColor) {
    AppendMsg2ListBox(IDD_LISTBOX_TOP, IDD_SCROLLBAR_TOP, pszMsg, dwColor);
}

void
CChatDLG::AppendMsg2sys(const char* pszMsg, DWORD dwColor, int iFilterType) {
    AppendMsg2ListBox(IDD_LISTBOX_TOP, IDD_SCROLLBAR_TOP, pszMsg, dwColor);
}

void
CChatDLG::AppendMsg2All(const char* pszMsg, DWORD dwColor, int iFilterType) {
    if (iFilterType == FILTER_WHISPER) ///무조건 넣는다.
    {
        AppendMsg2ListBox(IID_LISTBOX_ALL, IID_SCROLLBAR_ALL, pszMsg, dwColor);
    } else ///필터 타입에 따라 넣거나안넣는다.
    {
        if (m_Filters[0].Filters[iFilterType] > 0)
            AppendMsg2ListBox(IID_LISTBOX_ALL, IID_SCROLLBAR_ALL, pszMsg, dwColor);
    }
}

void
CChatDLG::AppendMsg2Whisper(const char* pszMsg, DWORD dwColor, int iFilterType) {
    if (iFilterType == FILTER_WHISPER) ///무조건 넣는다.
    {
        AppendMsg2ListBox(IID_LISTBOX_WHISPER, IID_SCROLLBAR_WHISPER, pszMsg, dwColor);
    } else ///필터 타입에 따라 넣거나안넣는다.
    {
        if (m_Filters[1].Filters[iFilterType] > 0)
            AppendMsg2ListBox(IID_LISTBOX_WHISPER, IID_SCROLLBAR_WHISPER, pszMsg, dwColor);
    }
}

void
CChatDLG::AppendMsg2Trade(const char* pszMsg, DWORD dwColor, int iFilterType) {
    if (iFilterType == FILTER_WHISPER) ///무조건 넣는다.
    {
        AppendMsg2ListBox(IID_LISTBOX_TRADE, IID_SCROLLBAR_TRADE, pszMsg, dwColor);
    } else ///필터 타입에 따라 넣거나안넣는다.
    {
        if (m_Filters[2].Filters[iFilterType] > 0)
            AppendMsg2ListBox(IID_LISTBOX_TRADE, IID_SCROLLBAR_TRADE, pszMsg, dwColor);
    }
}

void
CChatDLG::AppendMsg2Party(const char* pszMsg, DWORD dwColor, int iFilterType) {
    if (iFilterType == FILTER_WHISPER) ///무조건 넣는다.
    {
        AppendMsg2ListBox(IID_LISTBOX_PARTY, IID_SCROLLBAR_PARTY, pszMsg, dwColor);
    } else ///필터 타입에 따라 넣거나안넣는다.
    {
        if (m_Filters[3].Filters[iFilterType] > 0)
            AppendMsg2ListBox(IID_LISTBOX_PARTY, IID_SCROLLBAR_PARTY, pszMsg, dwColor);
    }
}

void
CChatDLG::AppendMsg2Clan(const char* pszMsg, DWORD dwColor, int iFilterType) {
    if (iFilterType == FILTER_WHISPER) ///무조건 넣는다.
    {
        AppendMsg2ListBox(IID_LISTBOX_CLAN, IID_SCROLLBAR_CLAN, pszMsg, dwColor);
    } else ///필터 타입에 따라 넣거나안넣는다.
    {
        if (m_Filters[4].Filters[iFilterType] > 0)
            AppendMsg2ListBox(IID_LISTBOX_CLAN, IID_SCROLLBAR_CLAN, pszMsg, dwColor);
    }
}

void
CChatDLG::AppendMsg2Allied(const char* pszMsg, DWORD dwColor, int iFilterType) {
    if (iFilterType == FILTER_WHISPER) ///무조건 넣는다.
    {
        AppendMsg2ListBox(IID_LISTBOX_ALLIED, IID_SCROLLBAR_ALLIED, pszMsg, dwColor);
    } else ///필터 타입에 따라 넣거나안넣는다.
    {
        if (m_Filters[5].Filters[iFilterType] > 0)
            AppendMsg2ListBox(IID_LISTBOX_ALLIED, IID_SCROLLBAR_ALLIED, pszMsg, dwColor);
    }
}

void
CChatDLG::AppendMsg2ListBox(int iListBoxID, int iScrollBarID, const char* pszMsg, DWORD dwColor) {
    CWinCtrl* pCtrl = Find(iListBoxID);
    assert(pCtrl);
    if (pCtrl && pCtrl->GetControlType() == CTRL_LISTBOX) {
        CTListBox* pListBox = (CTListBox*)pCtrl;

        if (pListBox->GetValue() >= pListBox->GetMaximum() - pListBox->GetExtent()) {
            AppendChatLineParsed(pListBox, pszMsg, dwColor, true);
            pListBox->SetValue(pListBox->GetMaximum());

            pCtrl = Find(iScrollBarID);
            assert(pCtrl);
            if (pCtrl && pCtrl->GetControlType() == CTRL_SCROLLBAR) {
                CTScrollBar* pScrollBar = (CTScrollBar*)pCtrl;
                pScrollBar->SetValue(pListBox->GetMaximum());
            }
        } else {
            AppendChatLineParsed(pListBox, pszMsg, dwColor, false);
        }
    }
}

void
CChatDLG::OnLButtonDown(unsigned uiProcID, WPARAM wParam, LPARAM lParam) {
    switch (uiProcID) {
        case IID_BTN_ALL:
            TabSelected(TAB_ALL);
            if (CWinCtrl* pCtrl = Find(IID_EDITBOX)) {
                CTEditBox* pBox = (CTEditBox*)pCtrl;
                pBox->clear_text();
            }
            ActiveListBoxMoveEnd();
            break;
        case IID_BTN_WHISPER:
            TabSelected(TAB_WHISPER);
            if (CWinCtrl* pCtrl = Find(IID_EDITBOX)) {
                CTEditBox* pBox = (CTEditBox*)pCtrl;
                pBox->clear_text();
                pBox->AppendText("@");
            }
            ActiveListBoxMoveEnd();
            break;
        case IID_BTN_TRADE:
            TabSelected(TAB_TRADE);
            if (CWinCtrl* pCtrl = Find(IID_EDITBOX)) {
                CTEditBox* pBox = (CTEditBox*)pCtrl;
                pBox->clear_text();
                pBox->AppendText("$");
            }
            ActiveListBoxMoveEnd();
            break;
        case IID_BTN_PARTY:
            TabSelected(TAB_PARTY);
            if (CWinCtrl* pCtrl = Find(IID_EDITBOX)) {
                CTEditBox* pBox = (CTEditBox*)pCtrl;
                pBox->clear_text();
                pBox->AppendText("#");
            }
            ActiveListBoxMoveEnd();
            break;
        case IID_BTN_CLAN:
            TabSelected(TAB_CLAN);
            if (CWinCtrl* pCtrl = Find(IID_EDITBOX)) {
                CTEditBox* pBox = (CTEditBox*)pCtrl;
                pBox->clear_text();
                pBox->AppendText("&");
            }
            ActiveListBoxMoveEnd();
            break;
        case IID_BTN_ALLIED: {
            TabSelected(TAB_ALLIED);
            if (CWinCtrl* pCtrl = Find(IID_EDITBOX)) {
                CTEditBox* pBox = (CTEditBox*)pCtrl;
                pBox->clear_text();
                pBox->AppendText("~");
            }
        }
            ActiveListBoxMoveEnd();
            break;
        default:
            break;
    }

    if (uiProcID != IID_BTN_FILTER)
        g_itMGR.CloseDialog(DLG_TYPE_CHATFILTER);
}

bool
CChatDLG::Create(const char* IDD) {
    if (CTDialog::Create(IDD)) {
        CWinCtrl* pCtrl = Find(IID_RADIOBOX);
        if (pCtrl && pCtrl->GetControlType() == CTRL_RADIOBOX) {
            CTRadioBox* pBox = (CTRadioBox*)pCtrl;
            pBox->SetPressedButton(IID_BTN_ALL);
        }

        pCtrl = Find(IID_EDITBOX);
        assert(pCtrl);
        if (pCtrl)
            pCtrl->AddActionListener(this);

        return true;
    }
    return false;
}

ChatFilter&
CChatDLG::GetCurrentTabFilter() {
    return m_Filters[m_iSelectedTab - 1];
}

void
CChatDLG::SetCurrentTabFilter(ChatFilter& filter) {
    m_Filters[m_iSelectedTab - 1] = filter;
}

unsigned
CChatDLG::ActionPerformed(CActionEvent* e) {
    assert(e);
    if (e == NULL)
        return 0;

    switch (e->GetID()) {
        case WM_KEYDOWN: {
            if (e->GetWParam() == VK_RETURN) {
                CWinCtrl* pCtrl = e->GetSource();
                assert(pCtrl);
                if (pCtrl && pCtrl->GetControlType() == CTRL_EDITBOX) {
                    CTEditBox* pEditBox = (CTEditBox*)pCtrl;
                    if (CTEditBox::s_pFocusEdit && CTEditBox::s_pFocusEdit == pEditBox
                        && pEditBox->get_text()) {
                        SendChatMsg(pEditBox->get_text());

                        switch (m_iSelectedTab) {
                            case TAB_WHISPER:
                                if (pEditBox->get_text() == NULL)
                                    pEditBox->AppendText("@");
                                else if (strlen(pEditBox->get_text()) < 1)
                                    pEditBox->AppendText("@");
                                break;
                            case TAB_TRADE:
                                if (pEditBox->get_text() == NULL)
                                    pEditBox->AppendText("$");
                                else if (strlen(pEditBox->get_text()) < 1)
                                    pEditBox->AppendText("$");
                                break;
                            case TAB_PARTY:
                                if (pEditBox->get_text() == NULL)
                                    pEditBox->AppendText("#");
                                else if (strlen(pEditBox->get_text()) < 1)
                                    pEditBox->AppendText("#");
                                break;
                            case TAB_CLAN:
                                if (pEditBox->get_text() == NULL)
                                    pEditBox->AppendText("&");
                                else if (strlen(pEditBox->get_text()) < 1)
                                    pEditBox->AppendText("&");
                                break;
                            case TAB_ALLIED:
                                if (pEditBox->get_text() == NULL)
                                    pEditBox->AppendText("~");
                                else if (strlen(pEditBox->get_text()) < 1)
                                    pEditBox->AppendText("~");
                                break;
                            default:
                                break;
                        }
                    }
                    // else
                    //{
                    //	pEditBox->SetFocus( true );
                    //}
                    return pEditBox->GetControlID();
                }
            }
        } break;
        default:
            break;
    }
    return 0;
}

//*-------------------------------------------------------------------------------------*/
/// @brief 현재 활성화된(선택된탭) 리스트박스의 item count를 리턴한다.
//*-------------------------------------------------------------------------------------*/
int
CChatDLG::GetLineCount() {
    int listbox_id = GetActiveListBox();
    if (CWinCtrl* pCtrl = Find(listbox_id)) {
        CTListBox* pListBox = (CTListBox*)pCtrl;
        return pListBox->GetMaximum();
    }
    return 0;
}

//*-------------------------------------------------------------------------------------*/
/// @param index : 0-based
/// @brief 현재 활성화된(선택된탭) 리스트박스의 index의 string을 리턴한다.
//*-------------------------------------------------------------------------------------*/
const char*
CChatDLG::GetLineString(int index) {
    int listbox_id = GetActiveListBox();
    if (CWinCtrl* pCtrl = Find(listbox_id)) {
        CTListBox* pListBox = (CTListBox*)pCtrl;
        return pListBox->GetText(index);
    }
    return NULL;
}
//*-------------------------------------------------------------------------------------*/
/// @brief 현재 활성화된(선택된탭) 리스트박스를 맨밑으로 이동시킨다
//*-------------------------------------------------------------------------------------*/
void
CChatDLG::ActiveListBoxMoveEnd() {
    int listbox_id = GetActiveListBox();
    int scrollbox_id = GetActiveScrollBar();

    CWinCtrl* pCtrl = Find(listbox_id);
    if (pCtrl && pCtrl->GetControlType() == CTRL_LISTBOX) {
        CTListBox* pListBox = (CTListBox*)pCtrl;
        pListBox->SetValue(pListBox->GetMaximum());

        pCtrl = Find(scrollbox_id);
        if (pCtrl && pCtrl->GetControlType() == CTRL_SCROLLBAR) {
            CTScrollBar* pScrollBar = (CTScrollBar*)pCtrl;
            pScrollBar->SetValue(pListBox->GetMaximum());
        }
    }
}