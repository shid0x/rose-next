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
};

/// Non-dialog HUD pieces with a UI2 replacement ( see RoseUi2::Piece ).
const RoseUi2::Piece kReplacedPieces[] = {
    RoseUi2::PIECE_BUFF_BAR, ///< RoseRmlBuffBar
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
