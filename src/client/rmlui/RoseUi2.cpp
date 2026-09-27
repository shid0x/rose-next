#include "stdafx.h"

#include "RoseUi2.h"
#include "RoseRmlUi.h"

#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "tgamectrl/tdialog.h"
#include "../Sound/IO_Sound.h"

#include "rose/common/log.h"

#include <stdlib.h>

namespace {

/// Legacy dialogs that have a UI2 replacement. Add a type here only once its
/// RmlUi panel covers everything the player needs from the old one -- this
/// table is the whole of the "converted" switch.
const int kReplacedDialogs[] = {
    DLG_TYPE_INFO, ///< RoseRmlStatusPanel ( name, level, HP/MP/EXP, menu )
    /// RoseRmlSkillBar. Hidden, NOT gone: a hidden CQuickBAR still handles the
    /// F-key hotkeys and owns the page; the RmlUi bar is its view.
    DLG_TYPE_QUICKBAR,
    DLG_TYPE_QUICKBAR_EXT,
    DLG_TYPE_SKILL, ///< RoseRmlSkillWindow ( also in kReplacedWindows )
    DLG_TYPE_CHAR, ///< RoseRmlCharacterWindow ( also in kReplacedWindows )
    /// RoseRmlPartyFrames. Hidden, NOT gone: CPartyDlg is CParty's observer
    /// and still writes the join / leave / leader chat lines.
    DLG_TYPE_PARTY,
    /// RoseRmlPartyOptions ( also in kReplacedWindows ). Hidden, NOT gone:
    /// CPartyOptionDlg still writes the "party settings changed" chat lines.
    DLG_TYPE_PARTYOPTION,
    /// RoseRmlInventory ( also in kReplacedWindows ). Hidden, NOT gone:
    /// CItemDlg stays the inventory's model ( item events, the per-PC slot
    /// arrangement ) and answers the drop commands through the UI2 window.
    DLG_TYPE_ITEM,
    DLG_TYPE_QUEST, ///< RoseRmlQuestJournal ( also in kReplacedWindows )
    /// RoseRmlMinimap ( also in kReplacedWindows: the game opens it in field
    /// zones and closes it in clan zones ). Hidden, NOT gone: CMinimapDLG
    /// still loads each zone's map and keeps the scripts' indicators.
    DLG_TYPE_MINIMAP,
    /// RoseRmlConversation, one window for the three conversation dialogs
    /// ( also in kReplacedWindows ). The engine's text and answers reach it
    /// through IT_MGR::OpenQueryDLG / QueryDLG_AppendExam.
    DLG_TYPE_DIALOG,
    DLG_TYPE_SELECTEVENT,
    DLG_TYPE_EVENTDIALOG,
    /// RoseRmlShop, one window for the store and the basket ( also in
    /// kReplacedWindows ). Hidden, NOT gone: CStoreDLG and CDealDLG observe
    /// CStore / CDealData and keep the icons, drag items and commands.
    DLG_TYPE_STORE,
    DLG_TYPE_DEAL,
    /// RoseRmlNumberInput ( also in kReplacedWindows ). Hidden, NOT gone:
    /// CNumberInputDlg holds the command the answer runs.
    DLG_TYPE_N_INPUT,
    /// RoseRmlTrade ( also in kReplacedWindows ). Hidden, NOT gone: CExchangeDLG
    /// observes CExchange and keeps the icons, drag items and commands. Its
    /// Hide() ends the trade, so the classic dialog must never be shown.
    DLG_TYPE_EXCHANGE,
    /// The death window: a UI2 message box entry ( RoseRmlUi, OpenRestart ).
    DLG_TYPE_RESTART,
    /// RoseRmlStorage ( also in kReplacedWindows ). Hidden, NOT gone: CBankDlg
    /// observes CBank, keeps the icons and the drag, and holds the tab the
    /// deposit command reads. Its zuly box, CBankWindowDlg, is only opened by
    /// CBankDlg's buttons, which UI2 does not show.
    DLG_TYPE_BANK,
    /// RoseRmlAvatarStore ( also in kReplacedWindows ). Hidden, NOT gone:
    /// CAvatarStoreDlg holds the other player's goods, the slots, the buy drag
    /// and the wanted list the sell command checks. Its Hide() wipes the shop,
    /// so UI2 calls Prepare() / Reset() instead.
    DLG_TYPE_AVATARSTORE,
    /// RoseRmlPrivateStore ( also in kReplacedWindows ). Hidden, NOT gone:
    /// CPrivateStoreDlg observes CPrivateStore and holds both lists' slots,
    /// the drag items and the tab the bag's drop command reads. Its Hide()
    /// closes the shop, so UI2 calls Prepare() / Teardown() instead.
    DLG_TYPE_PRIVATESTORE,
    /// RoseRmlGoodsForm ( also in kReplacedWindows ): CGoodsDlg keeps the item
    /// and the kind of question, and confirms.
    DLG_TYPE_GOODS,
    /// RoseRmlSeparate ( also in kReplacedWindows ). Hidden, NOT gone:
    /// CSeparateDlg observes CSeparate, holds the item and output slots and
    /// the drag item, and its Start() runs the checks and sends.
    DLG_TYPE_SEPARATE,
    /// RoseRmlUpgrade ( also in kReplacedWindows ). Hidden, NOT gone:
    /// CUpgradeDlg holds the slots, the drag items and the state machine the
    /// result's OK box leaves ( which applies the result ). Hidden, its result
    /// state does not animate: RoseRmlUpgrade plays the result itself.
    DLG_TYPE_UPGRADE,
    /// RoseRmlCraft ( also in kReplacedWindows ). Hidden, NOT gone: CMakeDLG
    /// holds the slots, the drag item, the Start checks and the server's
    /// answer ( RecvResult skips the classic result state under UI2 ). Hidden,
    /// it does not update: RoseRmlCraft plays and applies the result.
    DLG_TYPE_MAKE,
    /// RoseRmlMenuBar ( also in kReplacedWindows ): the classic pop-up menu,
    /// now an always-shown bar -- the routing makes its open a no-op and its
    /// close ( every world click ) harmless.
    DLG_TYPE_MENU,
    /// RoseRmlSystemMenu ( also in kReplacedWindows ): exit / character
    /// select through CSystemDLG::RequestLeave.
    DLG_TYPE_SYSTEM,
    /// RoseRmlCommunity ( also in kReplacedWindows ). Hidden, NOT gone: the
    /// friends list lives only in CCommDlg's list box, which the messenger
    /// packets update directly. The add-friend box is the window's field.
    DLG_TYPE_COMMUNITY,
    DLG_TYPE_ADDFRIEND,
    /// RoseRmlMessages ( also in kReplacedWindows ): under UI2 no classic
    /// private chat is created ( IT_MGR::OpenPrivateChatDlg and
    /// Recv_wsv_MESSENGER_CHAT branch to UI2 ); one left from before a live
    /// switch is hidden, which deletes it -- CPrivateChatDlg::Hide's job.
    DLG_TYPE_PRIVATECHAT,
    /// RoseRmlChatRoom ( also in kReplacedWindows ). Hidden, NOT gone:
    /// CChatRoomDlg observes CChatRoom and keeps the members. Its Hide()
    /// leaves the room -- it must never be shown ( under UI2 its "room
    /// joined" event opens through IT_MGR instead of Show() ).
    DLG_TYPE_CHATROOM,
    /// RoseRmlClan ( also in kReplacedWindows ). Hidden, NOT gone: CClanDlg
    /// observes CClan, and its member list holds each member's contribution,
    /// channel, level and job ( and writes their log-in chat lines ). The
    /// notice box is the window's Notice tab.
    DLG_TYPE_CLAN,
    DLG_TYPE_CLAN_NOTICE,
    /// RoseRmlClanOrganize ( also in kReplacedWindows ): the checks and the
    /// request are CClanOrganizeDlg::RequestOrganize.
    DLG_TYPE_CLAN_ORGANIZE,
    /// RoseRmlSkillTree ( also in kReplacedWindows ): generated from
    /// LIST_SKILL; the classic dialog, its XML and its painted pages are not
    /// used under UI2.
    DLG_TYPE_SKILLTREE,
    /// RoseRmlOptions ( also in kReplacedWindows ): reads and writes
    /// CClientStorage itself; COptionDlg::ApplyResolution is shared.
    DLG_TYPE_OPTION,
    /// RoseRmlChat. Hidden, NOT gone: CChatDLG keeps the send path
    /// ( SendLine: commands, prefixes, limits, item links ) and still gets
    /// every line; its Hide() drops the classic edit box's focus.
    DLG_TYPE_CHAT,
    DLG_TYPE_CHATFILTER,
};

/// The replaced dialogs that are windows ( opened / closed by the player )
/// rather than always-on HUD: IT_MGR routes open / close / "is open" here.
const int kReplacedWindows[] = {
    DLG_TYPE_SKILL,
    DLG_TYPE_CHAR,
    DLG_TYPE_PARTYOPTION,
    DLG_TYPE_ITEM,
    DLG_TYPE_QUEST,
    DLG_TYPE_MINIMAP,
    DLG_TYPE_DIALOG,
    DLG_TYPE_SELECTEVENT,
    DLG_TYPE_EVENTDIALOG,
    DLG_TYPE_STORE,
    DLG_TYPE_DEAL,
    DLG_TYPE_N_INPUT,
    DLG_TYPE_EXCHANGE,
    DLG_TYPE_RESTART,
    DLG_TYPE_BANK,
    DLG_TYPE_AVATARSTORE,
    DLG_TYPE_PRIVATESTORE,
    DLG_TYPE_GOODS,
    DLG_TYPE_SEPARATE,
    DLG_TYPE_UPGRADE,
    DLG_TYPE_MAKE,
    DLG_TYPE_MENU,
    DLG_TYPE_SYSTEM,
    DLG_TYPE_COMMUNITY,
    DLG_TYPE_ADDFRIEND,
    DLG_TYPE_PRIVATECHAT,
    DLG_TYPE_CHATROOM,
    DLG_TYPE_CLAN,
    DLG_TYPE_CLAN_NOTICE,
    DLG_TYPE_CLAN_ORGANIZE,
    DLG_TYPE_SKILLTREE,
    DLG_TYPE_OPTION,
};

/// Non-dialog HUD pieces with a UI2 replacement ( see RoseUi2::Piece ).
const RoseUi2::Piece kReplacedPieces[] = {
    RoseUi2::PIECE_BUFF_BAR, ///< RoseRmlBuffBar
    RoseUi2::PIECE_NOTIFY_BUTTONS, ///< RoseRmlNotifyButtons
};

const char* kIniPath = ".\\rose-next.ini";

int g_iActive = -1; ///< -1 = not yet resolved

bool
ResolveActive() {
    if (g_iActive >= 0)
        return g_iActive != 0;

    g_iActive = 0;

    const char* pEnv = getenv("ROSE_UI2");
    if (pEnv != NULL && *pEnv != '0') {
        g_iActive = 1;
    } else {
        char szBuf[16] = {0};
        GetPrivateProfileStringA("VIDEO", "UI2", "0", szBuf, sizeof(szBuf), kIniPath);
        if (szBuf[0] != '\0' && szBuf[0] != '0')
            g_iActive = 1;
    }

    /// UI2 without the overlay would hide legacy dialogs and draw nothing in
    /// their place, so it only counts once RmlUi is really up.
    if (g_iActive && !RoseRmlUi::IsEnabled()) {
        LOG_WARN("[ui2] UI2 requested but RmlUi is disabled; staying on the classic UI");
        g_iActive = 0;
    }

    LOG_INFO("[ui2] interface = {}", g_iActive ? "UI2" : "classic");
    return g_iActive != 0;
}

} // namespace

namespace RoseUi2 {

bool
IsActive() {
    return ResolveActive() && RoseRmlUi::IsInitialised();
}

bool
IsChosen() {
    return ResolveActive();
}

bool
SetActive(bool bActive) {
    ResolveActive();
    g_iActive = bActive ? 1 : 0;
    WritePrivateProfileStringA("VIDEO", "UI2", bActive ? "1" : "0", kIniPath);

    if (!RoseRmlUi::IsInitialised()) {
        LOG_INFO("[ui2] {} interface saved; applies on the next start", bActive ? "UI2" : "classic");
        return false;
    }
    LOG_INFO("[ui2] switched to {} interface", bActive ? "UI2" : "classic");

    if (!bActive) {
        /// Hand the replaced dialogs back. Only the ones the game shows by
        /// default: anything else was hidden because nobody opened it.
        for (int iDlgType : kReplacedDialogs) {
            CTDialog* pDlg = g_itMGR.FindDlg(iDlgType);
            if (pDlg != NULL && pDlg->IsDefaultVisible() && !pDlg->IsVision())
                pDlg->Show();
        }
    }
    return true;
}

bool
IsReplaced(int iDlgType) {
    if (!IsActive())
        return false;
    for (int iType : kReplacedDialogs) {
        if (iType == iDlgType)
            return true;
    }
    return false;
}

bool
IsPieceReplaced(Piece ePiece) {
    if (!IsActive())
        return false;
    for (Piece eReplaced : kReplacedPieces) {
        if (eReplaced == ePiece)
            return true;
    }
    return false;
}

static bool
IsReplacedWindow(int iDlgType) {
    if (!IsActive())
        return false;
    for (int iType : kReplacedWindows) {
        if (iType == iDlgType)
            return true;
    }
    return false;
}

bool
OpenWindow(int iDlgType, bool bToggle) {
    if (!IsReplacedWindow(iDlgType))
        return false;
    /// A quantity question is asked, never toggled: CTCmdOpenNumberInputDlg
    /// opens it with the toggle default, so a second question while one was
    /// showing closed it -- and dropped the new command with it.
    /// Nor is a trade: the accept path opens it with the toggle set. Nor a
    /// player's shop: its list reply opens it with the toggle, so clicking a
    /// second shop while one was showing closed the window on the new goods.
    /// Nor the shop's price form: a second item dropped while it asks about
    /// the first must ask about the second, not close the form.
    if (iDlgType == DLG_TYPE_N_INPUT || iDlgType == DLG_TYPE_EXCHANGE
        || iDlgType == DLG_TYPE_AVATARSTORE || iDlgType == DLG_TYPE_GOODS)
        bToggle = false;
    const bool bOpen = RoseRmlUi::IsWindowOpen(iDlgType);
    RoseRmlUi::SetWindowOpen(iDlgType, bToggle ? !bOpen : true);
    return true;
}

bool
CloseWindow(int iDlgType) {
    if (!IsReplacedWindow(iDlgType))
        return false;
    RoseRmlUi::SetWindowOpen(iDlgType, false);
    return true;
}

bool
QueryWindow(int iDlgType, bool& bOpen) {
    if (!IsReplacedWindow(iDlgType))
        return false;
    bOpen = RoseRmlUi::IsWindowOpen(iDlgType);
    return true;
}

void
HideReplacedDialogs() {
    if (!IsActive())
        return;
    for (int iDlgType : kReplacedDialogs) {
        CTDialog* pDlg = g_itMGR.FindDlg(iDlgType);
        if (pDlg != NULL && pDlg->IsVision()) {
            /// Silently: CTDialog::Hide plays the dialog's close sound, and
            /// game code that just re-showed it ( the party dialog on joining
            /// a party ) already played the open one -- the pair was heard as
            /// an open immediately followed by a close.
            const int iHideSound = pDlg->GetSoundHideID();
            pDlg->SetSoundHideID(0);
            pDlg->Hide();
            pDlg->SetSoundHideID(iHideSound);
        }
    }
}

void
PlayWindowSound(int iDlgType, bool bOpen) {
    CTDialog* pDlg = g_itMGR.FindDlg(iDlgType);
    if (pDlg == NULL || g_pSoundLIST == NULL)
        return;
    const int iSound = bOpen ? pDlg->GetSoundShowID() : pDlg->GetSoundHideID();
    if (iSound > 0)
        g_pSoundLIST->IDX_PlaySound((short)iSound);
}

} // namespace RoseUi2
