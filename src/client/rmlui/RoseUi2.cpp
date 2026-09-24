#include "stdafx.h"

#include "RoseUi2.h"
#include "RoseRmlUi.h"

#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "tgamectrl/tdialog.h"

#include "rose/common/log.h"

#include <stdlib.h>

namespace {

/// Legacy dialogs that have a UI2 replacement. Add a type here only once its
/// RmlUi panel covers everything the player needs from the old one -- this
/// table is the whole of the "converted" switch.
const int kReplacedDialogs[] = {
    DLG_TYPE_INFO, ///< RoseRmlStatusPanel ( name, level, HP/MP/EXP, menu )
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

void
HideReplacedDialogs() {
    if (!IsActive())
        return;
    for (int iDlgType : kReplacedDialogs) {
        CTDialog* pDlg = g_itMGR.FindDlg(iDlgType);
        if (pDlg != NULL && pDlg->IsVision())
            pDlg->Hide();
    }
}

} // namespace RoseUi2
