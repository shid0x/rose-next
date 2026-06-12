#include "StdAfx.h"

#include "it_mgr.h"
#include "..\\Object.h"
#include "CDragNDropMgr.h"
#include "CHelpMgr.h"
#include "CTDrawImpl.h"
#include "CToolTipMgr.h"
#include "IO_ImageRes.h"
#include "InterfaceType.h"

#include "itstate/CITStateAppraisal.h"
#include "itstate/CITStateDead.h"
#include "itstate/CITStateNormal.h"
#include "itstate/CITStateNpcDialog.h"
#include "itstate/CITStateRepair.h"
#include "itstate/CITStateRideCart.h"
#include "itstate/CITStateWaitDisconnect.h"

#include "Icon/CIconDialog.h"

#include "Command/CTCmdHotExec.h"
#include "Command/CTCmdNumberInput.h"
#include "Command/CTCmdOpenDlg.h"
#include "Command/UICommand.h"
#include "cursor/ccursor.h"

#include "Dlgs/CAddFriendDlg.h"
#include "Dlgs/CBankWindowDlg.h"
#include "Dlgs/CChatFilterDlg.h"
#include "Dlgs/CChatRoomDlg.h"
#include "Dlgs/CClanDlg.h"
#include "Dlgs/CCommDlg.h"
#include "Dlgs/CDialogDlg.h"
#include "Dlgs/CMemoDlg.h"
#include "Dlgs/CMemoViewDlg.h"
#include "Dlgs/CMsgBoxSpecial.h"
#include "Dlgs/CPrivateChatDlg.h"
#include "Dlgs/CUpgradeDlg.h"
#include "Dlgs/ClanOrganizeDlg.h"
#include "Dlgs/ClanRegistNotice.h"
#include "Dlgs/EventDialog.h"
#include "Dlgs/InputNameDlg.h"
#include "Dlgs/NotifyButtonDlg.h"
#include "Dlgs/NotifyDlg.h"
#include "Dlgs/PartyOptionDlg.h"
#include "Dlgs/SelectEventDlg.h"
#include "Dlgs/Skilltreedlg.h"

#include "Dlgs/AvatarInfoDlg.h"
#include "Dlgs/CAvatarStoreDlg.h"
#include "Dlgs/CBankDlg.h"
#include "Dlgs/CGoodsDlg.h"
#include "Dlgs/CItemDlg.h"
#include "Dlgs/CMenuDlg.h"
#include "Dlgs/CMinimapDLG.h"
#include "Dlgs/CNumberInputDlg.h"
#include "Dlgs/COptionDlg.h"
#include "Dlgs/CPartyDlg.h"
#include "Dlgs/CPartyMember.h"
#include "Dlgs/CPatDlg.h"
#include "Dlgs/CPrivateChatDlg.h"
#include "Dlgs/CPrivateStoreDlg.h"
#include "Dlgs/CQuestDlg.h"
#include "Dlgs/CRestartDLG.h"
#include "Dlgs/CSeparateDlg.h"
#include "Dlgs/CSkillDLG.h"
#include "Dlgs/CStoreDLG.h"
#include "Dlgs/CSystemDLG.h"
#include "Dlgs/CSystemMsgDlg.h"
#include "Dlgs/CTargetMenu.h"
#include "Dlgs/CharacterDLG.h"
#include "Dlgs/ChattingDLG.h"
#include "Dlgs/DealDLG.h"
#include "Dlgs/ExchangeDLG.h"
#include "Dlgs/HelpDlg.h"
#include "Dlgs/MakeDLG.h"
#include "Dlgs/QuickToolBAR.h"
#include "Dlgs/TargetObjectDLG.h"

#include "dlgs/console_dialog.h"

#include "cursor/CCursor.h"

#include "../GameData/CClan.h"
#include "../GameData/CExchange.h"
#include "../GameData/CParty.h"
#include "../GameData/CSeparate.h"
#include "../GameData/CUpgrade.h"
#include "../JCommandState.h"
#include "../Network/CNetwork.h"
#include "../System/CGame.h"
#include "../gamedata/cdealdata.h"
#include "../gamedata/cstore.h"
#include "../util/CCheat.h"

#include "../IO_Event.h"
#include "tgamectrl/TGameCtrl.h"
#include "tgamectrl/resourcemgr.h"
#include "tgamectrl/tbutton.h"
#include "tgamectrl/tcaption.h"
#include "tgamectrl/tcommand.h"
#include "tgamectrl/tcontrolmgr.h"
#include "tgamectrl/tdialog.h"
#include "tgamectrl/teditbox.h"
#include "tgamectrl/tlistbox.h"
#include "tgamectrl/tscrollbar.h"
#include "tgamectrl/timage.h"
#include "tgamectrl/winctrl.h"

#include "../GameCommon/ReloadProcess.h"
#include "../SFX/ISFX.h"
#include "../SFX/SFXFont.h"
#include "../SFX/SFXManager.h"

#include "io_quest.h"
#include "controls/EffectString.h"
#include <algorithm>

const D3DCOLOR c_dwChatColorAll = D3DCOLOR_ARGB(255, 255, 255, 255);
const D3DCOLOR c_dwChatColorShout = D3DCOLOR_ARGB(255, 189, 250, 255);
const D3DCOLOR c_dwChatColorParty = D3DCOLOR_ARGB(255, 255, 237, 140);
const D3DCOLOR c_dwChatColorWhisper = D3DCOLOR_ARGB(255, 201, 255, 144);
const D3DCOLOR c_dwChatColorNotice = D3DCOLOR_ARGB(255, 255, 188, 172);
const D3DCOLOR c_dwChatColorSystem = D3DCOLOR_ARGB(255, 255, 224, 229);
const D3DCOLOR c_dwChatColorError = D3DCOLOR_ARGB(255, 255, 0, 0);
const D3DCOLOR c_dwChatColorQuest = D3DCOLOR_ARGB(255, 151, 221, 241);
const D3DCOLOR c_dwChatColorClan = D3DCOLOR_ARGB(255, 255, 228, 122);
const D3DCOLOR c_dwChatColorQuestReward = D3DCOLOR_ARGB(255, 224, 162, 209);

IT_MGR g_itMGR;
STBDATA g_TblHELP;

// A lookup table of dialog types to their XML file names
static const std::array<char*, DLG_TYPE_MAX> DIALOG_FILENAME = {
    "", // DLG_TYPE_NONE
    "dlgmenu", // DLG_TYPE_MENU
    "DlgChat", // DLG_TYPE_CHAT
    "DlgDialog", // DLG_TYPE_DIALOG
    "DlgMake", // DLG_TYPE_MAKE
    "DlgComm", // DLG_TYPE_COMMUNITY
    "DlgStore", // DLG_TYPE_STORE
    "DlgDeal", // DLG_TYPE_DEAL
    "DlgQuickBar", // DLG_TYPE_QUICKBAR
    "DlgAvata", // DLG_TYPE_CHAR
    "DlgMinimap", // DLG_TYPE_MINIMAP
    "dlgpartyoption", // DLG_TYPE_PARTYOPTION
    "DlgRestart", // DLG_TYPE_RESTART
    "DlgSystem", // DLG_TYPE_SYSTEM
    "DlgQuest", // DLG_TYPE_QUEST
    "DlgExchange", // DLG_TYPE_EXCHANGE
    "DlgSkill", // DLG_TYPE_SKILL
    "", // DLG_TYPE_SYSTEMMSG
    "DlgNInput", // DLG_TYPE_N_INPUT
    "", // DLG_TYPE_MSGBOX
    "DlgOption", // DLG_TYPE_OPTION
    "", // DLG_TYPE_TARGET_MENU
    "DlgParty", // DLG_TYPE_PARTY
    "DlgBank", // DLG_TYPE_BANK
    "DlgHelp", // DLG_TYPE_HELP
    "DlgInfo", // DLG_TYPE_INFO
    "DlgMemo", // DLG_TYPE_MEMO
    "DlgItem", // DLG_TYPE_ITEM
    "DlgPrivateStore", // DLG_TYPE_PRIVATESTORE
    "DlgAvatarStore", // DLG_TYPE_AVATARSTORE
    "DlgGoods", // DLG_TYPE_GOODS
    "DlgAddFriend", // DLG_TYPE_ADDFRIEND
    "DlgPrivateChat", // DLG_TYPE_PRIVATECHAT
    "DlgChatRoom", // DLG_TYPE_CHATROOM
    "DlgMemoView", // DLG_TYPE_MEMOVIEW
    "DlgSeparate", // DLG_TYPE_SEPARATE
    "DlgUpgrade", // DLG_TYPE_UPGRADE
    "DlgClan", // DLG_TYPE_CLAN
    "DlgOrganizeClan", // DLG_TYPE_CLAN_ORGANIZE
    "DlgClanRegNotice", // DLG_TYPE_CLAN_NOTICE
    "DlgChatFilter", // DLG_TYPE_CHATFILTER
    "MsgBox", // DLG_TYPE_MSGBOX_SPECIAL
    "DlgSelectEvent", // DLG_TYPE_SELECTEVENT
    "DlgDialogEvent", // DLG_TYPE_EVENTDIALOG
    "dlgnotify", // DLG_TYPE_NOTIFY
    "dlgskilltree", // DLG_TYPE_SKILLTREE
    "dlginputname", // DLG_TYPE_INPUTNAME
    "DlgBankWindow", // DLG_TYPE_BANKWINDOW
    "dlgquickbar2", // DLG_TYPE_QUICKBAR_EXT
    "", // DLG_TYPE_CONSOLE
};

IT_MGR::IT_MGR() {
    m_pDialogIconFromMenu = NULL;

    m_pStates[STATE_NORMAL] = new CITStateNormal;
    m_pStates[STATE_REPAIR] = new CITStateRepair;
    m_pStates[STATE_APPRAISAL] = new CITStateAppraisal;
    m_pStates[STATE_DEAD] = new CITStateDead;
    m_pStates[STATE_WAITDISCONNECT] = new CITStateWaitDisconnect;
    m_pStates[STATE_NPCDIALOG] = new CITStateNpcDialog;
    m_pStates[STATE_RIDECART] = new CITStateRideCart;

    m_pCurrState = m_pStates[STATE_NORMAL];
    m_pNotifyButtonDlg = NULL;
}

IT_MGR::~IT_MGR() {
    SAFE_DELETE(m_pDialogIconFromMenu);

    for (int i = 0; i < STATE_MAX; ++i)
        SAFE_DELETE(m_pStates[i]);

    m_pCurrState = 0;
    SAFE_DELETE(m_pNotifyButtonDlg);
}

void
IT_MGR::RefreshDlg() {
    list_dlgs_itor itorDlgs;
    CTDialog* pDlg = NULL;
    for (itorDlgs = m_Dlgs.begin(); itorDlgs != m_Dlgs.end(); ++itorDlgs) {
        pDlg = *itorDlgs;
        pDlg->RefreshDlg();
    }
}

bool
IT_MGR::Init() {
    InitDLG();

    m_pNotifyButtonDlg = new CNotifyButtonDlg;

    CVFSManager::GetSingleton().load_stb(g_TblHELP, HELP_STB);

    m_listSavedDialog.push_back(DLG_TYPE_ITEM);
    m_listSavedDialog.push_back(DLG_TYPE_CHAR);
    m_listSavedDialog.push_back(DLG_TYPE_SYSTEM);
    m_listSavedDialog.push_back(DLG_TYPE_QUEST);
    m_listSavedDialog.push_back(DLG_TYPE_SKILL);
    m_listSavedDialog.push_back(DLG_TYPE_OPTION);
    m_listSavedDialog.push_back(DLG_TYPE_HELP);
    m_listSavedDialog.push_back(DLG_TYPE_CONSOLE);

    //// add Observers to Observables

    CTDialog* pDlg = NULL;

    pDlg = FindDlg(DLG_TYPE_UPGRADE);
    if (pDlg) {
        CUpgradeDlg* pUpgradeDlg = (CUpgradeDlg*)pDlg;
        CUpgrade::GetInstance().AddObserver(pUpgradeDlg);
    }

    pDlg = FindDlg(DLG_TYPE_SEPARATE);
    if (pDlg) {
        CSeparateDlg* pSeparateDlg = (CSeparateDlg*)pDlg;
        CSeparate::GetInstance().AddObserver(pSeparateDlg);
    }

    pDlg = FindDlg(DLG_TYPE_STORE);
    if (pDlg) {
        CStoreDLG* pStoreDlg = (CStoreDLG*)pDlg;
        CStore::GetInstance().AddObserver(pStoreDlg);
    } else {
        assert(0 && "Not Found StoreDlg @IT_MGR::Init");
    }

    pDlg = FindDlg(DLG_TYPE_DEAL);
    if (pDlg) {
        CDealDLG* pDealDlg = (CDealDLG*)pDlg;
        CDealData::GetInstance().AddObserver(pDealDlg);
    } else {
        assert(0 && "Not Found DealDlg @IT_MGR::Init");
    }

    pDlg = FindDlg(DLG_TYPE_BANK);
    if (pDlg) {
        CBankDlg* pBankDlg = (CBankDlg*)pDlg;
        CBank::GetInstance().AddObserver(pBankDlg);
    } else {
        assert(0 && "Not Found BankDlg @IT_MGR::Init");
    }

    pDlg = FindDlg(DLG_TYPE_EXCHANGE);
    if (pDlg) {
        CExchangeDLG* pExchangeDlg = (CExchangeDLG*)pDlg;
        CExchange::GetInstance().AddObserver(pExchangeDlg);
    } else {
        assert(0 && "Not Found ExchangeDlg @IT-MGR::Init");
    }

    pDlg = FindDlg(DLG_TYPE_PARTY);
    if (pDlg) {
        CPartyDlg* pPartyDlg = (CPartyDlg*)pDlg;
        CParty::GetInstance().AddObserver(pPartyDlg);
    }

    if (pDlg = FindDlg(DLG_TYPE_PARTYOPTION)) {
        CPartyOptionDlg* p = (CPartyOptionDlg*)pDlg;
        CParty::GetInstance().AddObserver(p);
    }

    pDlg = FindDlg(DLG_TYPE_CLAN);
    if (pDlg) {
        CClanDlg* pClanDlg = (CClanDlg*)pDlg;
        CClan::GetInstance().AddObserver(pClanDlg);

        CClanSkillSlot* pClanSkillSlot = CClan::GetInstance().GetClanSkillSlot();
        pClanSkillSlot->AddObserver(pClanDlg);
    }

    /*m_pReloadProcess = new CReloadProcess();
    if( m_pReloadProcess->LoadReloadTexture() == false )
    {
        assert( 0 && "ReloadProcess loading failed" );
    }*/

    return true;
}

void
IT_MGR::Free() {
    ///�������̽� ���� Profile�� ����� Data�� g_ClientStorage�� �ְ� Save�Ѵ�.
    CTDialog* pDlg = NULL;
    list_dlgs_itor iter;

    for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
        pDlg = *iter;
        g_ClientStorage.SetSavedDialogPos(pDlg->GetDialogType(), pDlg->GetPosition());

        /////Quick Type
        if (pDlg->GetDialogType() == DLG_TYPE_QUICKBAR) {
            CQuickBAR* pQuickBarDlg = (CQuickBAR*)pDlg;
            g_ClientStorage.SetQuickBarDlgType(pQuickBarDlg->GetType());
        }

        if (pDlg->GetDialogType() == DLG_TYPE_QUICKBAR_EXT) {
            CQuickBAR* pQuickBarDlg = (CQuickBAR*)pDlg;
            g_ClientStorage.SetQuickBarExtDlgType(pQuickBarDlg->GetType());
        }
    }
    FreeDLG();
    g_ClientStorage.Save();
}

UINT
IT_MGR::MsgProc(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    if (m_pNotifyButtonDlg->Process(uiMsg, wParam, lParam))
        return uiMsg;

    return m_pCurrState->Process(uiMsg, wParam, lParam);
}

// *----------------------------------------------------------------------------------* //

void
IT_MGR::Update() {
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);

    ProcCommand();

    if (m_pCurrState->Draw())
        DrawDLGs(ptMouse);

    m_pCurrState->Update(ptMouse);
    // g_DragNDrop.DrawDRAG_Item();
    CDragNDropMgr::GetInstance().Draw(ptMouse);
    CCursor::GetInstance().Draw(ptMouse);
}

void
IT_MGR::InitDLG() {
    CBankWindowDlg* pBankWindowDlg = new CBankWindowDlg;
    pBankWindowDlg->Create(DIALOG_FILENAME[DLG_TYPE_BANKWINDOW]);
    AppendDlg(DLG_TYPE_BANKWINDOW, pBankWindowDlg, pBankWindowDlg->GetControlID());

    CPartyOptionDlg* pPartyOptionDlg = new CPartyOptionDlg;
    pPartyOptionDlg->Create(DIALOG_FILENAME[DLG_TYPE_PARTYOPTION]);
    AppendDlg(DLG_TYPE_PARTYOPTION, pPartyOptionDlg, pPartyOptionDlg->GetControlID());

    CInputNameDlg* pInputNameDlg = new CInputNameDlg;
    pInputNameDlg->Create(DIALOG_FILENAME[DLG_TYPE_INPUTNAME]);
    AppendDlg(DLG_TYPE_INPUTNAME, pInputNameDlg, pInputNameDlg->GetControlID());

    CSkillTreeDlg* pSkilltreedlg = new CSkillTreeDlg;
    pSkilltreedlg->Create(DIALOG_FILENAME[DLG_TYPE_SKILLTREE]);
    AppendDlg(DLG_TYPE_SKILLTREE, pSkilltreedlg, pSkilltreedlg->GetControlID());

    //	CFileSystem* pFileSystem = (CVFSManager::GetSingleton()).GetFileSystem();
    //	pFileSystem->OpenFile( "3DData\\Control\\xml\\dlgnotify.xml", OPEN_READ_BIN );
    //	pFileSystem->ReadToMemory();
    CNotifyDlg* pNotifyDlg = new CNotifyDlg;
    //	pNotifyDlg->Create( (const char*)pFileSystem->GetData() );
    pNotifyDlg->Create(DIALOG_FILENAME[DLG_TYPE_NOTIFY]);
    AppendDlg(DLG_TYPE_NOTIFY, pNotifyDlg, pNotifyDlg->GetControlID());
    //	pFileSystem->CloseFile();

    CEventDialog* pEventDlg = new CEventDialog;
    pEventDlg->Create(DIALOG_FILENAME[DLG_TYPE_EVENTDIALOG]);
    AppendDlg(DLG_TYPE_EVENTDIALOG, pEventDlg, pEventDlg->GetControlID());

    CSelectEventDlg* pSelectEventDlg = new CSelectEventDlg;
    pSelectEventDlg->Create(DIALOG_FILENAME[DLG_TYPE_SELECTEVENT]);
    AppendDlg(DLG_TYPE_SELECTEVENT, pSelectEventDlg, pSelectEventDlg->GetControlID());

    CMsgBoxSpecial* pSpecialMsgBox = new CMsgBoxSpecial;
    pSpecialMsgBox->Create(DIALOG_FILENAME[DLG_TYPE_MSGBOX_SPECIAL]);
    AppendDlg(DLG_TYPE_MSGBOX_SPECIAL, pSpecialMsgBox, pSpecialMsgBox->GetControlID());

    // DLG_TYPE_MSGBOX_SPECIAL
    CChatFilterDlg* pFilterDlg = new CChatFilterDlg;
    pFilterDlg->Create(DIALOG_FILENAME[DLG_TYPE_CHATFILTER]);
    AppendDlg(DLG_TYPE_CHATFILTER, pFilterDlg, pFilterDlg->GetControlID());

    CClanRegistNotice* pClanRegistNotice = new CClanRegistNotice;
    pClanRegistNotice->Create(DIALOG_FILENAME[DLG_TYPE_CLAN_NOTICE]);
    AppendDlg(DLG_TYPE_CLAN_NOTICE, pClanRegistNotice, pClanRegistNotice->GetControlID());

    CClanOrganizeDlg* pClanOrganizeDlg = new CClanOrganizeDlg;
    pClanOrganizeDlg->Create(DIALOG_FILENAME[DLG_TYPE_CLAN_ORGANIZE]);
    AppendDlg(DLG_TYPE_CLAN_ORGANIZE, pClanOrganizeDlg, pClanOrganizeDlg->GetControlID());

    /*CClanRegistNotice* pClanRegistNotice = new CClanRegistNotice;
    pClanRegistNotice->Create( "DlgClan*/

    CClanDlg* pClanDlg = new CClanDlg;
    pClanDlg->Create(DIALOG_FILENAME[DLG_TYPE_CLAN]);
    AppendDlg(DLG_TYPE_CLAN, pClanDlg, pClanDlg->GetControlID());

    CUpgradeDlg* pUpgradeDlg = new CUpgradeDlg(DLG_TYPE_UPGRADE);
    pUpgradeDlg->Create(DIALOG_FILENAME[DLG_TYPE_UPGRADE]);
    AppendDlg(DLG_TYPE_UPGRADE, pUpgradeDlg, pUpgradeDlg->GetControlID());

    CDialogDlg* pDialogDlg = new CDialogDlg;
    pDialogDlg->Create(DIALOG_FILENAME[DLG_TYPE_DIALOG]);
    AppendDlg(DLG_TYPE_DIALOG, pDialogDlg, pDialogDlg->GetControlID());

    CSeparateDlg* pSeparateDlg = new CSeparateDlg;
    pSeparateDlg->Create(DIALOG_FILENAME[DLG_TYPE_SEPARATE]);
    AppendDlg(DLG_TYPE_SEPARATE, pSeparateDlg, pSeparateDlg->GetControlID());

    CMemoViewDlg* pMemoViewDlg = new CMemoViewDlg;
    pMemoViewDlg->Create(DIALOG_FILENAME[DLG_TYPE_MEMOVIEW]);
    AppendDlg(DLG_TYPE_MEMOVIEW, pMemoViewDlg, pMemoViewDlg->GetControlID());

    CChatRoomDlg* pChatroomDlg = new CChatRoomDlg(DLG_TYPE_CHATROOM);
    pChatroomDlg->Create(DIALOG_FILENAME[DLG_TYPE_CHATROOM]);
    AppendDlg(DLG_TYPE_CHATROOM, pChatroomDlg, pChatroomDlg->GetControlID());

    // CPrivateChatDlg* pPrivateChatDlg = new CPrivateChatDlg( DLG_TYPE_PRIVATECHAT );
    // pPrivateChatDlg->Create( "DlgPrivateChat" );
    // AppendDlg( DLG_TYPE_PRIVATECHAT, pPrivateChatDlg, pPrivateChatDlg->GetControlID () );

    CMemoDlg* pMemoDlg = new CMemoDlg(DLG_TYPE_MEMO);
    pMemoDlg->Create(DIALOG_FILENAME[DLG_TYPE_MEMO]);
    AppendDlg(DLG_TYPE_MEMO, pMemoDlg, pMemoDlg->GetControlID());

    CAddFriendDlg* pAddFriendDlg = new CAddFriendDlg(DLG_TYPE_ADDFRIEND);
    pAddFriendDlg->Create(DIALOG_FILENAME[DLG_TYPE_ADDFRIEND]);
    AppendDlg(DLG_TYPE_ADDFRIEND, pAddFriendDlg, pAddFriendDlg->GetControlID());

    CCommDlg* pCommDlg = new CCommDlg(DLG_TYPE_COMMUNITY);
    pCommDlg->Create(DIALOG_FILENAME[DLG_TYPE_COMMUNITY]);
    AppendDlg(DLG_TYPE_COMMUNITY, pCommDlg, pCommDlg->GetControlID());

    CAvatarStoreDlg* pAvatarStoreDlg = new CAvatarStoreDlg(DLG_TYPE_AVATARSTORE);
    pAvatarStoreDlg->Create(DIALOG_FILENAME[DLG_TYPE_AVATARSTORE]);
    AppendDlg(DLG_TYPE_AVATARSTORE, pAvatarStoreDlg, pAvatarStoreDlg->GetControlID());

    CGoodsDlg* pGoodsDlg = new CGoodsDlg(DLG_TYPE_GOODS);
    pGoodsDlg->Create(DIALOG_FILENAME[DLG_TYPE_GOODS]);
    AppendDlg(DLG_TYPE_GOODS, pGoodsDlg, pGoodsDlg->GetControlID());

    CPrivateStoreDlg* pPrivateStoreDlg = new CPrivateStoreDlg(DLG_TYPE_PRIVATESTORE);
    pPrivateStoreDlg->Create(DIALOG_FILENAME[DLG_TYPE_PRIVATESTORE]);
    AppendDlg(DLG_TYPE_PRIVATESTORE, pPrivateStoreDlg, pPrivateStoreDlg->GetControlID());

    CItemDlg* pItemDlg = new CItemDlg(DLG_TYPE_ITEM);
    pItemDlg->Create(DIALOG_FILENAME[DLG_TYPE_ITEM]);
    AppendDlg(DLG_TYPE_ITEM, pItemDlg, pItemDlg->GetControlID());

    CRestartDLG* pRestartDLG = new CRestartDLG;
    pRestartDLG->Create(DIALOG_FILENAME[DLG_TYPE_RESTART]);
    AppendDlg(DLG_TYPE_RESTART, pRestartDLG, pRestartDLG->GetControlID());

    // �⺻â
    CMenuDlg* pMenuDlg = new CMenuDlg;
    pMenuDlg->Create(DIALOG_FILENAME[DLG_TYPE_MENU]);
    AppendDlg(DLG_TYPE_MENU, pMenuDlg, pMenuDlg->GetControlID());

    // �ý��� ���̾˷α�
    CSystemDLG* pSystemDLG = new CSystemDLG;
    pSystemDLG->Create(DIALOG_FILENAME[DLG_TYPE_SYSTEM]);
    AppendDlg(DLG_TYPE_SYSTEM, pSystemDLG, pSystemDLG->GetControlID());

    // �̴ϸ� ���̾˷α�
    CMinimapDLG* pMinimapDLG = new CMinimapDLG;
    pMinimapDLG->Create(DIALOG_FILENAME[DLG_TYPE_MINIMAP]);
    AppendDlg(DLG_TYPE_MINIMAP, pMinimapDLG, pSystemDLG->GetControlID());

    //�κ��丮
    // CInventoryDLG* pAvtInvDLG = new CInventoryDLG( DLG_TYPE_INVENTORY );
    // pAvtInvDLG->Create("DlgInven");
    // AppendDlg( DLG_TYPE_INVENTORY, pAvtInvDLG, pAvtInvDLG->GetControlID() );

    //����
    CStoreDLG* pStoreDLG = new CStoreDLG(DLG_TYPE_STORE);
    pStoreDLG->Create(DIALOG_FILENAME[DLG_TYPE_STORE]);
    AppendDlg(DLG_TYPE_STORE, pStoreDLG, pStoreDLG->GetControlID());

    // Deal
    CDealDLG* pDealDLG = new CDealDLG(DLG_TYPE_DEAL);
    pDealDLG->Create(DIALOG_FILENAME[DLG_TYPE_DEAL]);
    AppendDlg(DLG_TYPE_DEAL, pDealDLG, pDealDLG->GetControlID());

    //ĳ����â

    CCharacterDLG* pCharDLG = new CCharacterDLG(DLG_TYPE_CHAR);
    pCharDLG->Create(DIALOG_FILENAME[DLG_TYPE_CHAR]);
    AppendDlg(DLG_TYPE_CHAR, pCharDLG, pCharDLG->GetControlID());

    // Ÿ�� ������Ʈ ���̾˷α�
    /*CTargetObjectDLG* pTargetObjectDLG = new CTargetObjectDLG;
    pTargetObjectDLG->Create("DlgTarget");
    AppendDlg( DLG_TYPE_TARGET_OBJECT, pTargetObjectDLG, pTargetObjectDLG->GetControlID() );*/

    //����
    CQuickBAR* pQuickBAR = new CQuickBAR(DLG_TYPE_QUICKBAR);
    pQuickBAR->Create(DIALOG_FILENAME[DLG_TYPE_QUICKBAR]);
    AppendDlg(DLG_TYPE_QUICKBAR, pQuickBAR, pQuickBAR->GetControlID());

    /* Second skillbar? */
    pQuickBAR = new CQuickBAR(DLG_TYPE_QUICKBAR_EXT);
    pQuickBAR->Create(DIALOG_FILENAME[DLG_TYPE_QUICKBAR_EXT]);
    pQuickBAR->SetQuickBarType(QUICKBAR_TYPE_EXTENSION);
    AppendDlg(DLG_TYPE_QUICKBAR_EXT, pQuickBAR, pQuickBAR->GetControlID());

    CQuestDlg* pQuestDlg = new CQuestDlg;
    pQuestDlg->Create(DIALOG_FILENAME[DLG_TYPE_QUEST]);
    AppendDlg(DLG_TYPE_QUEST, pQuestDlg, pQuestDlg->GetControlID());

    //��ųâ
    CSkillDLG* pSkillDLG = new CSkillDLG(DLG_TYPE_SKILL);
    pSkillDLG->Create(DIALOG_FILENAME[DLG_TYPE_SKILL]);
    AppendDlg(DLG_TYPE_SKILL, pSkillDLG, pSkillDLG->GetControlID());

    CNumberInputDlg* pNInputDlg = new CNumberInputDlg;
    pNInputDlg->Create(DIALOG_FILENAME[DLG_TYPE_N_INPUT]);
    AppendDlg(DLG_TYPE_N_INPUT, pNInputDlg, pNInputDlg->GetControlID());

    CSystemMsgDlg* pSystemMsgDlg = new CSystemMsgDlg;
    AppendDlg(DLG_TYPE_SYSTEMMSG, pSystemMsgDlg, pSystemMsgDlg->GetControlID());

    ///�ɼ� ���� â
    // <Option Dialog>
    ///*
    COptionDlg* pOptionDlg = new COptionDlg;
    pOptionDlg->Create(DIALOG_FILENAME[DLG_TYPE_OPTION]);
    AppendDlg(DLG_TYPE_OPTION, pOptionDlg, pOptionDlg->GetControlID());
    //*/
    // </Option Dialog>

    ///������ ��ȯ â
    CExchangeDLG* pExchangeDlg = new CExchangeDLG(DLG_TYPE_EXCHANGE);
    pExchangeDlg->Create(DIALOG_FILENAME[DLG_TYPE_EXCHANGE]);
    AppendDlg(DLG_TYPE_EXCHANGE, pExchangeDlg, pExchangeDlg->GetControlID());

    ///��Ƽâ
    CPartyDlg* pPartyDlg = new CPartyDlg;
    pPartyDlg->Create(DIALOG_FILENAME[DLG_TYPE_PARTY]);
    AppendDlg(DLG_TYPE_PARTY, pPartyDlg, pPartyDlg->GetControlID());

    CMakeDLG* pMakeDlg = new CMakeDLG(DLG_TYPE_MAKE);
    pMakeDlg->Create(DIALOG_FILENAME[DLG_TYPE_MAKE]);
    AppendDlg(DLG_TYPE_MAKE, pMakeDlg, pMakeDlg->GetControlID());

    /// REMARK TEST CODE
    CBankDlg* pBankDlg = new CBankDlg(DLG_TYPE_BANK);
    pBankDlg->Create(DIALOG_FILENAME[DLG_TYPE_BANK]);
    AppendDlg(DLG_TYPE_BANK, pBankDlg, pBankDlg->GetControlID());

    CHelpDlg* pHelpDlg = new CHelpDlg;
    pHelpDlg->Create(DIALOG_FILENAME[DLG_TYPE_HELP]);
    AppendDlg(DLG_TYPE_HELP, pHelpDlg, pHelpDlg->GetControlID());

    CAvatarInfoDlg* pInfoDlg = new CAvatarInfoDlg(DLG_TYPE_INFO);
    pInfoDlg->Create(DIALOG_FILENAME[DLG_TYPE_INFO]);
    AppendDlg(DLG_TYPE_INFO, pInfoDlg, pInfoDlg->GetControlID());

    // CPatDlg* pPatDlg = new CPatDlg( DLG_TYPE_PAT );
    // pPatDlg->Create("DlgPat");
    // AppendDlg( DLG_TYPE_PAT, pPatDlg, pPatDlg->GetControlID());

    // ä��â
    CChatDLG* pChatDLG = new CChatDLG;
    pChatDLG->Create(DIALOG_FILENAME[DLG_TYPE_CHAT]);
    AppendDlg(DLG_TYPE_CHAT, pChatDLG, pChatDLG->GetControlID());

    // TODO: RAM: Move this to an XML file?
    int console_width = static_cast<int>(g_pCApp->GetWIDTH() * 0.75f);
    int console_height = static_cast<int>(g_pCApp->GetHEIGHT() * 0.50f);
    int console_x = (g_pCApp->GetWIDTH() - console_width) / 2;
    int console_y = (g_pCApp->GetHEIGHT() - console_height) / 2;

    ConsoleDialog* console_dialog = new ConsoleDialog();
    console_dialog->Create(console_x, console_y, console_width, console_height);
    AppendDlg(DLG_TYPE_CONSOLE, console_dialog, console_dialog->GetControlID());

    MoveDlg2ListEnd(pChatDLG);

    InitInterfacePos();

    if (g_ClientStorage.HasSavedDialogPos()) {
        SetInterfacePosBySavedData();
    }

#ifdef _NEWUI
    SetInterfacePos_After();
#endif

    return;
}

void
IT_MGR::SetInterfacePos_After() {
    CTDialog* pDlg = NULL;
    list_dlgs_itor iter;
    for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
        pDlg = *iter;
        pDlg->SetInterfacePos_After();
    }
}

void
IT_MGR::FreeDLG() {
    CTDialog* pDlg = NULL;
    if (g_pAVATAR) {
        if (pDlg = FindDlg(DLG_TYPE_ITEM)) {
            CItemDlg* pItemDlg = (CItemDlg*)pDlg;
            std::list<S_InventoryData> list;
            pItemDlg->GetVirtualInventory(list);
            /// CClientDB::GetInstance().SetInventoryData( g_pAVATAR->Get_NAME(), list );
            g_ClientStorage.SetInventoryData(g_pAVATAR->Get_NAME(), list);
        }
    }

    list_dlgs_itor iterDlg;

    for (iterDlg = m_Dlgs.begin(); iterDlg != m_Dlgs.end();) {
        pDlg = *iterDlg;
        delete pDlg;
        iterDlg = m_Dlgs.erase(iterDlg);
    }

    std::list<CIconDialog*>::iterator iterIcon;
    for (iterIcon = m_Icons.begin(); iterIcon != m_Icons.end();) {
        delete *iterIcon;
        iterIcon = m_Icons.erase(iterIcon);
    }
    //	SAFE_DELETE( m_pDrawImpl );
}

void
IT_MGR::DrawDLGs(POINT ptMouse) {
    g_HelpMgr.UpdateFrame();

    /// Control���� ���������� ó���ϴ� ���� �ʱ�ȭ
    /// Update�� MouseOveró�� ���� �ʱ�ȭ
    CWinCtrl::SetProcessMouseOver(false);
    CWinCtrl::SetProcessMouseOverCtrl(NULL);
    CTDialog::SetProcessMouseOverDialog(NULL);
    ///
    CTDialog* pDlg = NULL;

    for_each(m_Dlgs.rbegin(), m_Dlgs.rend(), [ptMouse](auto d) { d->Update(ptMouse); });
    for_each(m_Icons.rbegin(), m_Icons.rend(), [ptMouse](auto i) { i->Update(ptMouse); });
    m_pNotifyButtonDlg->Update(ptMouse);

    for_each(m_Icons.begin(), m_Icons.end(), [](auto i) { i->Draw(); });
    for_each(m_Dlgs.begin(), m_Dlgs.end(), [](auto d) { d->Draw(); });
    m_pNotifyButtonDlg->Draw();

    if (!IsMouseOnInterface()) {
        CInfo& MouseInfo = CCursor::GetInstance().GetMouseInfo();
        ptMouse.y -= 15;

        if (MouseInfo.GetHeight()) {
            MouseInfo.SetPosition(ptMouse);
            CToolTipMgr::GetInstance().RegistInfo(MouseInfo);
        }
    }

    g_pAVATAR->m_EndurancePack.Draw();

    CToolTipMgr::GetInstance().Draw();

    //	m_Indicate.Update( ptMouse );
    //	m_Indicate.Draw();
}

CChatDLG*
IT_MGR::GetChatDLG() {
    CChatDLG* pDLG = (CChatDLG*)FindDlg(DLG_TYPE_CHAT);
    return pDLG;
}

CDialogDlg*
IT_MGR::GetQueryDLG() {
    CDialogDlg* pDLG = (CDialogDlg*)FindDlg(DLG_TYPE_DIALOG);
    return pDLG;
}

CQuickBAR*
IT_MGR::GetQuickBAR() {
    CQuickBAR* pDLG = (CQuickBAR*)FindDlg(DLG_TYPE_QUICKBAR);
    return pDLG;
}

CQuickBAR*
IT_MGR::GetQuickBAR_EXT() {
    CQuickBAR* pDLG = (CQuickBAR*)FindDlg(DLG_TYPE_QUICKBAR_EXT);
    return pDLG;
}

CMinimapDLG*
IT_MGR::GetMinimapDLG() {
    CMinimapDLG* pDLG = (CMinimapDLG*)FindDlg(DLG_TYPE_MINIMAP);
    return pDLG;
}

/// �ŷ�â�� ���´�.
CExchangeDLG*
IT_MGR::GetExchangeDLG() {
    CExchangeDLG* pDLG = (CExchangeDLG*)FindDlg(DLG_TYPE_EXCHANGE);
    return pDLG;
}

///
/// Open, close dlgs..
///
bool
IT_MGR::OpenMakeDlg(int iSkillSlotIdx) {
    if (!IsDlgOpened(DLG_TYPE_MAKE)) ///�ߺ��Ǽ� ���°��� �����Ѵ�.
    {
        CManufacture::GetInstance().SetSkillSlotIndex(iSkillSlotIdx);

        OpenDialog(DLG_TYPE_MAKE, false);
        OpenDialog(DLG_TYPE_ITEM, false);
    } else {
        OpenMsgBox(STR_USING_MAKESKILL);
        return false;
    }
    return false;
}
void
IT_MGR::OpenDialogBySkill(int iSkillSlotIdx, int iTargetDialogID) {
    switch (iTargetDialogID) {
        case DLG_TYPE_MAKE:
            OpenMakeDlg(iSkillSlotIdx);
            break;
        case DLG_TYPE_SEPARATE: {
            CSeparate::GetInstance().SetSkillSlotIdx(iSkillSlotIdx);
            g_itMGR.OpenDialog(DLG_TYPE_ITEM, false);
            g_itMGR.OpenDialog(DLG_TYPE_SEPARATE, false);
            break;
        }
        case DLG_TYPE_UPGRADE: {
            CUpgrade::GetInstance().SetSkillSlotIdx(iSkillSlotIdx);
            g_itMGR.OpenDialog(DLG_TYPE_ITEM, false);
            g_itMGR.OpenDialog(DLG_TYPE_UPGRADE, false);
            break;
        }
        default:
            break;
    }
}

bool
IT_MGR::OpenQueryDLG(short nSX,
    short nSY,
    char* szQuery,
    short nMaxCharLen,
    int iEventID,
    void (*fpCloseProc)(int iEventID),
    int iEventOwner,
    int iEventDlgType) {
    CDialogDlg* pDlg = NULL;

    switch (iEventDlgType) {
        /// NPC ��ȭâ
        case EVENT_DLG_NPC: {
            /// ��ž�½ô� ���� �ʴ´�.
            if (g_pAVATAR->GetPetMode() > 0)
                return true;

            pDlg = (CDialogDlg*)FindDlg(DLG_TYPE_DIALOG);
            if (!pDlg && pDlg->IsInValidShow())
                return false;

            pDlg->SetScript(szQuery);
            CObjCHAR* pTarget = (CObjCHAR*)g_pAVATAR->Get_TARGET();
            if (pTarget)
                pDlg->SetTargetNpcClientObjectIndex(pTarget->Get_INDEX());

            OpenDialog(DLG_TYPE_DIALOG, false);
        }

        break;
        /// Warp DLG
        case EVENT_DLG_WARP: {
            pDlg = (CDialogDlg*)FindDlg(DLG_TYPE_SELECTEVENT);
            if (!pDlg && pDlg->IsInValidShow())
                return false;

            CSelectEventDlg* pEventDlg = (CSelectEventDlg*)pDlg;
            pEventDlg->SetTitle(szQuery);
            pEventDlg->SetTargetClientObjectIndex(iEventOwner);
            OpenDialog(DLG_TYPE_SELECTEVENT, false);
        } break;
        /// Event Object Dlg
        case EVENT_DLG_EVENT_OBJECT: {
            pDlg = (CDialogDlg*)FindDlg(DLG_TYPE_EVENTDIALOG);
            if (!pDlg && pDlg->IsInValidShow())
                return false;

            CEventDialog* pEventDlg = (CEventDialog*)pDlg;
            pEventDlg->SetScript(szQuery);
            pEventDlg->SetTargetNpcClientObjectIndex(iEventOwner);
            OpenDialog(DLG_TYPE_EVENTDIALOG, false);
        } break;
    }

    return true;
}

bool
IT_MGR::QueryDLG_AppendExam(char* szExam,
    int iEventID,
    void (*fpExamEvent)(int iEventID),
    int iType) {
    switch (iType) {
        case QUERYTYPE_ITEM: {
            if (CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_EVENTDIALOG)) {
                CEventDialog* pEventDlg = (CEventDialog*)pDlg;
                pEventDlg->AddAnswerExample(szExam, iEventID, fpExamEvent);
            }
        } break;
        case QUERYTYPE_NPC: {
            CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_DIALOG);
            if (pDlg) {
                CDialogDlg* pDialogDlg = (CDialogDlg*)pDlg;
                pDialogDlg->AddAnswerExample(szExam, iEventID, fpExamEvent);
                return true;
            }
        } break;
        case QUERYTYPE_SELECT: {
            if (CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_SELECTEVENT)) {
                CSelectEventDlg* pSelectDlg = (CSelectEventDlg*)pDlg;
                pSelectDlg->AddEvent(szExam, iEventID, fpExamEvent);
            }
        } break;
        default:
            break;
    }

    return false;
}

void
IT_MGR::CloseQueryDlg() {
    /// Query���� �� �ݾƹ�����.
    CloseDialog(DLG_TYPE_DIALOG);
    CloseDialog(DLG_TYPE_SELECTEVENT);
    CloseDialog(DLG_TYPE_EVENTDIALOG);
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief CHotIconSlot ������ �������� QuickDlg �� ���Ե��� ������Ʈ
//----------------------------------------------------------------------------------------------------

void
IT_MGR::UpdateQuickSlot() {
    CQuickBAR* pDLG = NULL;
    pDLG = GetQuickBAR();
    if (pDLG) {
        pDLG->UpdateHotIconSlot();
    }

    pDLG = GetQuickBAR_EXT();
    if (pDLG) {
        pDLG->UpdateHotIconSlot();
    }
}

short
IT_MGR::GetItemInventoryTAB(tagITEM sItem) {
    short nType = sItem.GetTYPE();

    switch (nType) {
        case ITEM_TYPE_FACE_ITEM:
        case ITEM_TYPE_HELMET:
        case ITEM_TYPE_ARMOR:
        case ITEM_TYPE_GAUNTLET:
        case ITEM_TYPE_BOOTS:
        case ITEM_TYPE_KNAPSACK:
        case ITEM_TYPE_WEAPON:
        case ITEM_TYPE_SUBWPN:
        case ITEM_TYPE_JEWEL:
            return 0;
        case ITEM_TYPE_USE:
            return 1;
        case ITEM_TYPE_ETC:
        case ITEM_TYPE_NATURAL:
        case ITEM_TYPE_QUEST:
            return 2;
    }
    return 0;
}

void
IT_MGR::Close_store() {
    // ���� �ݱ�
    CStoreDLG* pStore = (CStoreDLG*)FindDlg(DLG_TYPE_STORE);
    if (pStore) {
        pStore->SetTab(0);
        pStore->Hide();
    }

    // �ŷ�â �ݱ�
    CDealDLG* pDeal = (CDealDLG*)FindDlg(DLG_TYPE_DEAL);

    if (pDeal)
        pDeal->Hide();

    ///�����Է±Ⱑ �� �ִٸ� �ݴ´�.
    CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_N_INPUT);
    if (pDlg && pDlg->IsVision())
        pDlg->Hide();
}

void
IT_MGR::InsertDlg(short nDlgType, CTDialog* pDlg, int iNextID, int iID) {
    list_dlgs_itor iter;
    for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
        if (iNextID == (*iter)->GetControlID()) {
            pDlg->SetDialogType(nDlgType);
            pDlg->SetControlID(iID);
            m_Dlgs.insert(iter, pDlg); ///�ش� Dialog������ �ִ´�.
            return;
        }
    }
    AppendDlg(nDlgType, pDlg, iID);
}

void
IT_MGR::AppendDlg(short nDlgType, CTDialog* pDlg, int iID) {
    pDlg->SetDialogType(nDlgType);
    pDlg->SetControlID(iID);
    if (pDlg->IsModal())
        m_Dlgs.push_back(pDlg);
    else {
        list_dlgs_itor iter;
        for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
            CTDialog* pDlg = *iter;
            if (pDlg->IsVision() && pDlg->IsModal())
                break;
        }
        m_Dlgs.insert(iter, pDlg);
    }
}

CTDialog*
IT_MGR::FindDlg(DWORD dwDlgType) {
    /// REMARK : NEW UI LIB ->
    list_dlgs_itor itorDlgs;
    CTDialog* pDlg = NULL;
    for (itorDlgs = m_Dlgs.begin(); itorDlgs != m_Dlgs.end(); ++itorDlgs) {
        /// REMARK : For Debuging
        pDlg = *itorDlgs;
        if (pDlg->GetDialogType() == dwDlgType)
            return pDlg;
    }
    ///<-
    return NULL;
}

int
IT_MGR::IsDlgOpened(int iDlgID) {
    CTDialog* pDlg = FindDlg((short)iDlgID);
    if (pDlg && pDlg->IsVision())
        return 1;

    return 0;
}

/// CMsgBox�� ��� ���� Type���� �������� ������ �ִ�.
void
IT_MGR::MoveDlg2ListEnd(CTDialog* pDlg) {
    list_dlgs_itor itorDlgs;
    for (itorDlgs = m_Dlgs.begin(); itorDlgs != m_Dlgs.end(); ++itorDlgs) {
        /// REMARK : For Debuging
        if (*itorDlgs == pDlg) {
            m_Dlgs.erase(itorDlgs);
            m_Dlgs.push_back(pDlg);
            return;
        }
    }
    ///<-
}
void
IT_MGR::MoveDlgIcon2ListEnd(CIconDialog* pIcon) {
    std::list<CIconDialog*>::iterator iter;
    for (iter = m_Icons.begin(); iter != m_Icons.end(); ++iter) {
        if (*iter == pIcon) {
            m_Icons.erase(iter);
            m_Icons.push_back(pIcon);
            return;
        }
    }
}
//
// void IT_MGR::AppendChatMsg( const char* pMsg,D3DCOLOR dwCol )
//{
//	CTDialog* pDlg = FindDlg( DLG_TYPE_CHAT );
//	if( pDlg == NULL )
//	{
//		LogString(LOG_DEBUG_,"Not Found ChatDlg/IT_MGR::AppendChatMsg\n");
//		return;
//	}
//	CChatDLG* pChatDlg = ( CChatDLG* ) pDlg;
//	pChatDlg->AppendMsg( pMsg, dwCol, 0);
//}

void
IT_MGR::AppendChatMsg(const char* pszMsg, int iType, DWORD forceapply_color) {
    CTDialog* pDlg = FindDlg(DLG_TYPE_CHAT);
    if (pDlg == NULL) {
        LogString(LOG_DEBUG_, "Not Found ChatDlg/IT_MGR::AppendChatMsg\n");
        return;
    }

    CChatDLG* pChatDlg = (CChatDLG*)pDlg;
    DWORD dwColor = c_dwChatColorAll;

    int iFilterType = CChatDLG::FILTER_NORMAL;

    switch (iType) {
        case CHAT_TYPE_SHOUT:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorShout;

            iFilterType = CChatDLG::FILTER_NORMAL;
            break;
        case CHAT_TYPE_PARTY:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorParty;

            iFilterType = CChatDLG::FILTER_PARTY;
            break;
        case CHAT_TYPE_WHISPER:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorWhisper;

            iFilterType = CChatDLG::FILTER_WHISPER;
            break;
        case CHAT_TYPE_NOTICE:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorNotice;

            pChatDlg->AppendMsg2System(pszMsg, dwColor);
            return;
        case CHAT_TYPE_SYSTEM:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorSystem;

            pChatDlg->AppendMsg2System(pszMsg, dwColor);
            return;
        case CHAT_TYPE_QUEST:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorQuest;

            pChatDlg->AppendMsg2System(pszMsg, dwColor);
            return;
        case CHAT_TYPE_QUESTREWARD:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorQuestReward;

            pChatDlg->AppendMsg2System(pszMsg, dwColor);
            return;
        case CHAT_TYPE_TRADE:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorAll;
            iFilterType = CChatDLG::FILTER_TRADE;
            break;
        case CHAT_TYPE_CLAN:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorClan;
            iFilterType = CChatDLG::FILTER_CLAN;
            break;
        case CHAT_TYPE_ALLIED:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorClan;
            iFilterType = CChatDLG::FILTER_ALLIED;
            break;
        case CHAT_TYPE_FRIEND:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorWhisper;

            iFilterType = CChatDLG::FILTER_SYSTEM;
            break;
        case CHAT_TYPE_ALL:
        default:
            if (forceapply_color)
                dwColor = forceapply_color;
            else
                dwColor = c_dwChatColorAll;
            iFilterType = CChatDLG::FILTER_NORMAL;
            break;
    }

    pChatDlg->AppendMsg(pszMsg, dwColor, iFilterType);
}

// bool IT_MGR::PacketProc( t_PACKET * pRecvPacket )
//{
//	if( pRecvPacket == NULL) return false;
//
//	//switch( pRecvPacket->m_HEADER.m_wType )
//	//{
//	/////��Ƽ�� �Ἲ�ǰų� ���εǾ������ �������� �����´�.
//	/////��Ƽâ�� Show�ϴ°��� �׻� �̰��̴�.
//	//case GSV_PARTY_MEMBER:
//	//	{
//	//		break;
//	//	}
//	//default:
//	//	return false;
//	//}
//	return true;
//}

/// Invoker Dialog ID = iDlgID
void
IT_MGR::ShowNumberInputDlg(CTCmdNumberInput* pCmd, CTObject* pCmdParam, __int64 iMaxNumber) {
    CTDialog* pDlg = NULL;
    pDlg = FindDlg(DLG_TYPE_N_INPUT);
    if (pDlg) {
        CNumberInputDlg* pNInputDlg = (CNumberInputDlg*)pDlg;

        pNInputDlg->SetMaxNumber(iMaxNumber);
        pNInputDlg->SetCommand(pCmd, pCmdParam);

        pDlg->Show();
        MoveDlg2ListEnd(pDlg);
    }
}

bool
IT_MGR::AddTCommand(int iDlgID, CTCommand* pCmd) {
    if (pCmd == NULL)
        return false;

    //��� ���� Ŀ�ǵ�
    if (iDlgID == 0) {
        pCmd->Exec(NULL);

#ifndef __SRVDISCONNECTRELOGIN
        delete pCmd;
#endif

    } else if (iDlgID
        == DLG_TYPE_MAX) /// IT_MGR�� CommandQ�� ������ �����ȴ�. Q�� �ְ�Update���� �����Ű��
    {
        m_CommandQ.Push_Back(pCmd);
    } else {
        CTDialog* pDlg = FindDlg(iDlgID);
        if (pDlg == NULL) {
            delete pCmd;
            return false;
        }
        pDlg->Push_Back_Cmd(pCmd);
    }
    return true;
}

bool
IT_MGR::OpenInputNameDlg(CTCmdInputName* pCmd) {
    if (IsDlgOpened(DLG_TYPE_INPUTNAME))
        return false;
    if (CTDialog* pDlg = FindDlg(DLG_TYPE_INPUTNAME)) {
        CInputNameDlg* p = (CInputNameDlg*)pDlg;
        p->SetCommand(pCmd);
        OpenDialog(DLG_TYPE_INPUTNAME);
        return true;
    }
    return false;
}

void
IT_MGR::OpenMsgBox(const char* szMsg,
    short int iButtonType,
    bool bModal,
    unsigned int iInvokerDlgID,
    CTCommand* pCmdOk,
    CTCommand* pCmdCancel,
    int iMsgType) {
    CreateMsgBoxData Data;
    Data.bModal = bModal;
    Data.iButtonType = iButtonType;
    Data.iInvokerDlgID = iInvokerDlgID;
    Data.iMsgType = iMsgType;
    Data.pCmdCancel = pCmdCancel;
    Data.pCmdOk = pCmdOk;
    Data.strMsg = szMsg;
    CTCmdCreateMsgBox* pCmd = new CTCmdCreateMsgBox(Data);
    AddTCommand(DLG_TYPE_MAX, pCmd);
}

void
IT_MGR::OpenMsgBox_CartRide(const char* szMsg,
    short int iButtonType,
    bool bModal,
    unsigned int iInvokerDlgID,
    CTCommand* pCmdOk,
    CTCommand* pCmdCancel,
    int iMsgType,
    WORD parm1,
    WORD parm2) {
    CreateMsgBoxData Data;
    Data.bModal = bModal;
    Data.iButtonType = iButtonType;
    Data.iInvokerDlgID = iInvokerDlgID;
    Data.iMsgType = iMsgType;
    Data.pCmdCancel = pCmdCancel;
    Data.pCmdOk = pCmdOk;
    Data.strMsg = szMsg;

    Data.parm1 = parm1;
    Data.parm2 = parm2;

    CTCmdCreateMsgBox* pCmd = new CTCmdCreateMsgBox(Data);
    AddTCommand(DLG_TYPE_MAX, pCmd);
}

void
IT_MGR::OpenMsgBox2(CreateMsgBoxData& Data) {
    CTCmdCreateMsgBox* pCmd = new CTCmdCreateMsgBox(Data);
    AddTCommand(DLG_TYPE_MAX, pCmd);
}

void
IT_MGR::InitInterfacePos() {
    POINT ptNew = {0, 0};
    int iDefaultX, iDefaultY, iAdjustX = 0, iAdjustY = 0;
    CTDialog* pDlg = NULL;
    int iTempHeight = 0;

    list_dlgs_itor iter;
    for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
        pDlg = *iter;
        iDefaultX = pDlg->GetDefaultPosX();
        iDefaultY = pDlg->GetDefaultPosY();
        iAdjustX = pDlg->GetDefaultAdjustPosX();
        iAdjustY = pDlg->GetDefaultAdjustPosY();

        switch (iDefaultX) {
            case CTDialog::TDXP_CENTER:
                ptNew.x = g_pCApp->GetWIDTH() / 2 - pDlg->GetWidth() / 2;
                break;
            case CTDialog::TDXP_RIGHT:
                ptNew.x = g_pCApp->GetWIDTH() - pDlg->GetWidth();
                break;
            case CTDialog::TDXP_LEFT:
            default:
                ptNew.x = 0;
                break;
        }

        switch (iDefaultY) {
            case CTDialog::TDYP_CENTER:
                ptNew.y = g_pCApp->GetHEIGHT() / 2 - pDlg->GetHeight() / 2;
                break;
            case CTDialog::TDYP_BOTTOM:
                ptNew.y = g_pCApp->GetHEIGHT() - pDlg->GetHeight();
                break;
            case CTDialog::TDYP_TOP:
            default:
                ptNew.y = 0;
                break;
        }

        ptNew.x += iAdjustX;
        ptNew.y += iAdjustY;
        pDlg->MoveWindow(ptNew);
        if (pDlg->IsDefaultVisible() && !pDlg->IsVision()) {
            pDlg->Show();

#ifdef _NEWUI
            pDlg->RefreshDlg(); //�ػ� ����ÿ� ������ ��ġ�� �̵��ϰ� ����� �Լ�.
#endif
        }
    }

    if (m_pNotifyButtonDlg) {
        m_pNotifyButtonDlg->UpdatePosition();
    }
}

void
IT_MGR::CloseDialogAllExceptDefaultView() {
    list_dlgs_itor iter;
    for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
        if (!(*iter)->IsDefaultVisible()) {
            CloseDialog((*iter)->GetDialogType());
            DelDialogIcon((*iter)->GetDialogType()); ///������ȭ �Ǿ� �ִ� �͵� �� ������.
        }
    }
}

void
IT_MGR::SetInterfacePosBySavedData() {
    POINT ptNew = {0, 0};
    list_dlgs_itor iter;
    CTDialog* pDlg = NULL;

    for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
        pDlg = *iter;

        if (pDlg->GetCaption()) {
            if (pDlg->GetCaption()) /// Caption�� �ְ� Ÿ��â�� �ƴҰ�쿡�� Save�� Position�� �����Ѵ�.
            {
                ptNew.x = g_ClientStorage.GetSavedDialogPosX(pDlg->GetDialogType());
                ptNew.y = g_ClientStorage.GetSavedDialogPosY(pDlg->GetDialogType());
                pDlg->MoveWindow(ptNew);
            }
        }

        /// Chat List Type Set
        // if( pDlg->GetDialogType() == DLG_TYPE_CHAT )
        //{
        //	CChatDLG* pChatDlg = (CChatDLG*)pDlg;
        //	pChatDlg->ChangeListType( g_ClientStorage.GetChatDlgType() );
        //	if( g_ClientStorage.GetChatDlgType() == CChatDLG::LIST_TYPE_BIG )
        //	{
        //		///REMARK TEMP CODE:Big�ϰ�� ChangeListType���� ������ -143�� ���ش�.�̸� �ӽ������� ����
        //�ڵ�
        //		///
        //		POINT pt = pChatDlg->GetPosition();
        //		pt.y += 143;
        //		pChatDlg->MoveWindow( pt );
        //	}
        //}

        /// Quick Type
        if (pDlg->GetDialogType() == DLG_TYPE_QUICKBAR) {
            CQuickBAR* pQuickBarDlg = (CQuickBAR*)pDlg;
            pQuickBarDlg->ChangeType(g_ClientStorage.GetQuickBarDlgType(), true);
        }

        if (pDlg->GetDialogType() == DLG_TYPE_QUICKBAR_EXT) {
            CQuickBAR* pQuickBarDlg = (CQuickBAR*)pDlg;
            pQuickBarDlg->ChangeType(g_ClientStorage.GetQuickBarExtDlgType(), true);
        }
    }
}

void
IT_MGR::ServerDisconnected() {
    assert(m_pCurrState);
    if (m_pCurrState->GetID() == STATE_NORMAL) {
        // 05. 10. 27 - ������
        // __SRVDISCONNECTRELOGIN(����������Ʈ�� �Ǿ��� ���)�� �α���â���� �̵��Ұ��ΰ�
        // �ƴϸ�... ���α׷��� �����Ұ��ΰ�..

#ifdef __SRVDISCONNECTRELOGIN
        CTCommand* pCmd = new CTCmdReLogin; // �̰� ��α���
#else
        CTCommand* pCmd = new CTCmdExit; // �̰� ����
#endif
        OpenMsgBox("Server Disconnected", CMsgBox::BT_OK, true, 0, pCmd, NULL);
    } else if (m_pCurrState->GetID() == STATE_WAITDISCONNECT) {
        if (g_itMGR.GetWaitDisconnectType() == 0) {
#ifdef __SRVDISCONNECTRELOGIN
            CGame::GetInstance().ChangeState(CGame::GS_RELOGIN);
#else
            g_pCApp->SetExitGame();
#endif
        }

    } else {
        g_pCApp->SetExitGame();
    }
}

void
IT_MGR::OpenDialog(int iDlgType, bool bToggle, int iPosX, int iPosY) {
    CTDialog* pDlg = NULL;
    if (pDlg = FindDlg(iDlgType)) {
        if (iPosX >= 0 && iPosY >= 0) {
            POINT pt = {iPosX, iPosY};
            pDlg->MoveWindow(pt);
        }

        if (bToggle) {
            if (pDlg->IsVision()) {
                pDlg->Hide();
            } else {
                MoveDlg2ListEnd(pDlg);
                DelDialogIcon(iDlgType);
                pDlg->Show();
            }
        } else {
            if (!pDlg->IsVision()) {
                MoveDlg2ListEnd(pDlg);
                DelDialogIcon(iDlgType);
                pDlg->Show();
            }
        }
    }
}

void
IT_MGR::CloseDialog(int iDlgType) {
    CTDialog* pDlg = NULL;
    if (pDlg = FindDlg(iDlgType)) {
        if (pDlg->IsVision())
            pDlg->Hide();
    }
}

bool
IT_MGR::IsMouseOnInterface() {
    if (CTDialog::GetProcessMouseOverDialog() || CDragNDropMgr::GetInstance().IsDraging())
        return true;
    return false;
}

void
IT_MGR::ProcessEscKeyDown() {
    std::list<int>::iterator iter;
    std::list<int> listDialogSet;

    for (iter = m_listSavedDialog.begin(); iter != m_listSavedDialog.end(); ++iter) {
        if (IsDlgOpened(*iter))
            listDialogSet.push_back(*iter);
    }

    if (listDialogSet.empty()) {
        if (!m_StackDialogSet.empty()) {
            std::list<int> DialogSet = m_StackDialogSet.back();

            for (iter = DialogSet.begin(); iter != DialogSet.end(); ++iter)
                OpenDialog(*iter, false);

            m_StackDialogSet.pop_back();
        }
    } else {
        m_StackDialogSet.push_back(listDialogSet);
        for (iter = listDialogSet.begin(); iter != listDialogSet.end(); ++iter)
            CloseDialog(*iter);
    }
}

void
IT_MGR::ProcCommand() {
    CTCommand* pCmd = m_CommandQ.GetCommand();
    if (pCmd) {
        if (pCmd->Exec(NULL))
            delete pCmd;
        else {
            if (pCmd->IsWaitExecSucceed())
                m_CommandQ.Push_Front(pCmd);
            else
                m_CommandQ.Push_Back(pCmd);
        }
    }
}

CMsgBox*
IT_MGR::FindMsgBox(int iMsgType) {
    list_dlgs_itor itorDlgs;
    CTDialog* pDlg = NULL;
    CMsgBox* pMsgBox = NULL;
    for (itorDlgs = m_Dlgs.begin(); itorDlgs != m_Dlgs.end(); ++itorDlgs) {
        pDlg = *itorDlgs;
        if (pDlg->GetDialogType() == DLG_TYPE_MSGBOX) {
            pMsgBox = (CMsgBox*)pDlg;
            if (pMsgBox->GetMsgType() == iMsgType) {
                return pMsgBox;
            }
        }
    }
    return NULL;
}

CPrivateChatDlg*
IT_MGR::GetPrivateChatDlg(DWORD dwUserTag) {
    list_dlgs_itor iter;
    CTDialog* pDlg;
    for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
        pDlg = *iter;
        if (pDlg->GetDialogType() == DLG_TYPE_PRIVATECHAT && pDlg->GetControlID() == dwUserTag)
            return (CPrivateChatDlg*)pDlg;
    }
    return NULL;
}

void
IT_MGR::OpenPrivateChatDlg(DWORD dwUserTag, BYTE btStatus, const char* pszName) {
    CTCmdOpenPrivateChatDlg Cmd(dwUserTag, btStatus, pszName);
    Cmd.Exec(NULL);
}

// void IT_MGR::BeginRepairItem()
//{
//	OpenDialog( DLG_TYPE_ITEM );
//	///Ŀ�� �ٲ�.. �׻� �ٲ۴�.
//	//CGame::GetInstance().BeginRepairMode();
//}

///���� TYPE�� �޼��� �ڽ��� ���ִ��� üũ�Ѵ�.
bool
IT_MGR::IsOpenedMsgBox(int iType) {
    CMsgBox* pMsgBox = NULL;
    list_dlgs_itor iter;
    for (iter = m_Dlgs.begin(); iter != m_Dlgs.end(); ++iter) {
        if ((*iter)->GetDialogType() == DLG_TYPE_MSGBOX) {
            pMsgBox = (CMsgBox*)*iter;
            if (pMsgBox->GetMsgType() == iType)
                return true;
        }
    }
    return false;
}

void
IT_MGR::AddDialogIcon(int iDialogType) {
    std::list<CIconDialog*>::iterator iterIcon;
    for (iterIcon = m_Icons.begin(); iterIcon != m_Icons.end(); ++iterIcon) {
        if ((*iterIcon)->GetDialogType() == iDialogType) {
            return;
        }
    }

    CIconDialog* pIcon = CIconDialog::CreateIcon(iDialogType, true);
    if (pIcon) {
        std::map<int, POINT>::iterator iterPtIcons;
        iterPtIcons = m_ptIcons.find(iDialogType);
        if (iterPtIcons == m_ptIcons.end()) {
            CTDialog* pDlg = FindDlg(iDialogType);
            pIcon->SetPosition(pDlg->GetPosition());
            m_ptIcons.insert(std::map<int, POINT>::value_type(iDialogType, pDlg->GetPosition()));
        } else {
            pIcon->SetPosition(iterPtIcons->second);
        }
        m_Icons.push_back(pIcon);
        CloseDialog(iDialogType);
    }
}

void
IT_MGR::DelDialogIcon(int iDialogType) {
    std::list<CIconDialog*>::iterator iterIcon;
    for (iterIcon = m_Icons.begin(); iterIcon != m_Icons.end(); ++iterIcon) {
        if ((*iterIcon)->GetDialogType() == iDialogType) {
            delete *iterIcon;
            m_Icons.erase(iterIcon);
            return;
        }
    }
}

CIconDialog*
IT_MGR::FindDialogIcon(int iDialogType) {
    std::list<CIconDialog*>::iterator iterIcon;
    for (iterIcon = m_Icons.begin(); iterIcon != m_Icons.end(); ++iterIcon) {
        if ((*iterIcon)->GetDialogType() == iDialogType)
            return *iterIcon;
    }
    return NULL;
}
void
IT_MGR::SetDialogIconFromMenu(CIconDialog* pIcon) {
    m_pDialogIconFromMenu = pIcon;
}
void
IT_MGR::DelDialogiconFromMenu() {
    SAFE_DELETE(m_pDialogIconFromMenu);
}

void
IT_MGR::SetDialogIconPosition(int iDialogType, POINT pt) {
    std::map<int, POINT>::iterator iterPtIcons;
    iterPtIcons = m_ptIcons.find(iDialogType);
    if (iterPtIcons == m_ptIcons.end()) {
        m_ptIcons.insert(std::map<int, POINT>::value_type(iDialogType, pt));
    } else {
        iterPtIcons->second = pt;
    }

    if (CIconDialog* pIcon = FindDialogIcon(iDialogType))
        pIcon->SetPosition(pt);
}

void
IT_MGR::BeginRepair() {
    // ChangeState( STATE_REPAIR );
    CTCommand* pCmd = new CTCmdChangeStateInterface(STATE_REPAIR);
    AddTCommand(DLG_TYPE_MAX, pCmd);
}

void
IT_MGR::BeginAppraisal() {
    // ChangeState( STATE_APPRAISAL );

    CTCommand* pCmd = new CTCmdChangeStateInterface(STATE_APPRAISAL);
    AddTCommand(DLG_TYPE_MAX, pCmd);

    g_itMGR.OpenDialog(DLG_TYPE_ITEM, false);
}

void
IT_MGR::EndRepair() {
    ChangeState(STATE_NORMAL);
}

void
IT_MGR::EndAppraisal() {
    ChangeState(STATE_NORMAL);
}

void
IT_MGR::ChangeState(int iIndex) {
    assert(iIndex >= 0 && iIndex < STATE_MAX);
    if (iIndex >= 0 && iIndex < STATE_MAX) {
        if (m_pCurrState->GetID() != iIndex) {
            m_pCurrState->Leave();
            m_pCurrState = m_pStates[iIndex];
            m_pCurrState->Enter();
        }
    }
}
int
IT_MGR::GetState() {
    return m_pCurrState->GetID();
}

void
IT_MGR::SetWaitDisconnectTime(int iSec) {
    m_iWaitDisconnectTime = iSec;
}

int
IT_MGR::GetWaitDisconnectTime() {
    return m_iWaitDisconnectTime;
}

void
IT_MGR::SetWaitDisconnectType(int iType) {
    m_iWaitDisconnectType = iType;
}

int
IT_MGR::GetWaitDisconnectType() {
    return m_iWaitDisconnectType;
}
void
IT_MGR::ClearNotifyButtons() {
    assert(m_pNotifyButtonDlg);
    if (m_pNotifyButtonDlg)
        m_pNotifyButtonDlg->Clear();
}
void
IT_MGR::AddNotifybutton(int iEventIndex) {
    int ID = iEventIndex;
    int iWidth = 36;
    int iHeight = 36;

    int iNormalGraphicID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_BTN_EVENTNOTIFY_NORMAL");
    int iOverGraphicID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_BTN_EVENTNOTIFY_OVER");
    int iDownGraphicID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_BTN_EVENTNOTIFY_DOWN");
    int iBlinkGraphicID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_BTN_EVENTNOTIFY_BLINK");
    int iModuleID = IMAGE_RES_UI;

    CTButton* pBtn = new CTButton;
    pBtn->Create(0,
        0,
        iWidth,
        iHeight,
        iNormalGraphicID,
        iOverGraphicID,
        iDownGraphicID,
        iModuleID);
    pBtn->SetControlID(ID);
    pBtn->SetBlinkGid(iBlinkGraphicID);
    pBtn->SetBlink(1, 500);

    assert(m_pNotifyButtonDlg);
    if (m_pNotifyButtonDlg)
        m_pNotifyButtonDlg->AddButton(pBtn);
}

void
IT_MGR::ShowQuestStartMessage(int index) {
    g_itMGR.AppendChatMsg(CStringManager::GetSingleton().GetQuestDesc(index),
        IT_MGR::CHAT_TYPE_QUEST);

    // if( ISFX* p = CSFXManager::GetSingleton().FindSFXWithType( SFX_FONT ))
    //{
    //	char* startmsg = (char*)QUEST_DESC( index );
    //	if( startmsg == NULL ) return;
    //	if( startmsg[0] == '\0' ) return;

    //	CSFXFont* sfx_font =(CSFXFont*)p;
    //	sfx_font->RemoveEffectStringsByType( CSFXFont::TYPE_QUEST_NAME );
    //	sfx_font->RemoveEffectStringsByType( CSFXFont::TYPE_QUEST_STARTMSG );

    //	POINT draw_position;
    //	int   draw_width;

    //	///Zone Name
    //	SIZE  size = getFontTextExtent( g_GameDATA.m_hFONT[ FONT_OUTLINE_18_BOLD ], QUEST_NAME(
    // index ) ); 	draw_width		= size.cx; 	draw_position.x = g_pCApp->GetWIDTH() / 2 - size.cx
    // / 2; 	draw_position.y = 150;///�ϴ� ����

    //	CEffectString* child = new CEffectString;
    //	child->SetType( CSFXFont::TYPE_QUEST_NAME );
    //	child->SetString( FONT_OUTLINE_18_BOLD, (char*)QUEST_NAME( index ), draw_position,
    // draw_width, 5 * 1000 ); 	sfx_font->AddEffectString( child );

    //	///Zone Description
    //	draw_width      = 400;
    //	draw_position.x = g_pCApp->GetWIDTH() / 2 - 400 / 2;
    //	draw_position.y = 200;
    //	child = new CEffectString;
    //	child->SetType( CSFXFont::TYPE_QUEST_STARTMSG );
    //	child->SetString( FONT_OUTLINE_14_BOLD, startmsg, draw_position, draw_width, 5 * 1000 );
    //	sfx_font->AddEffectString( child );
    //}
}

//*-------------------------------------------------------------------------------------------/
/// ���� ��Ŀ���� ������ �ִ� ����Ʈ�ڽ��� ������(CTEditBox)�� �����Ѵ�.
//*-------------------------------------------------------------------------------------------/
CTEditBox*
IT_MGR::GetFocusEditBox() {
    return CTEditBox::s_pFocusEdit;
}

void
IT_MGR::PostMsg2Dlg(int dialog_id, unsigned msg_id, unsigned param1, unsigned param2) {
    CTDialog* pDlg = FindDlg(dialog_id);
    assert(pDlg);
    if (pDlg)
        pDlg->PostMsg(msg_id, param1, param2);
}

CReloadProcess*
IT_MGR::GetReloadProcess() {
    return m_pReloadProcess;
}

CTDrawImpl*
IT_MGR::GetDrawImplPtr() {
    return &g_DrawImpl;
}

void
IT_MGR::reload_dialogs() {
    for (CTDialog* dialog: m_Dlgs) {
        int dialog_type = dialog->GetDialogType();
        if (dialog_type >= DLG_TYPE_MAX) {
            continue;
        }

        const char* filename = DIALOG_FILENAME[dialog_type];
        if (strcmp(filename, "") == 0) {
            continue;
        }

        int status = dialog->GetCtrlStatus();
        POINT offset = dialog->GetOffset();
        POINT position = dialog->GetPosition();
        bool visible = dialog->IsVision();

        dialog->Clear();
        dialog->Create(filename);

        dialog->SetCtrlStatus(status);
        dialog->SetOffset(offset);
        dialog->MoveWindow(position);
        dialog->RefreshDlg();

        if (dialog->IsDefaultVisible() || visible) {
            dialog->Show();
        }

        if (dialog_type == DLG_TYPE_SKILL) {
            g_pAVATAR->GetSkillSlot()->UpdateObservers();
        }

        POINT mouse_pos;
        CGame::GetInstance().Get_MousePos(mouse_pos);
        dialog->Update(mouse_pos);
    }
}