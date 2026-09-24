#ifndef _ROSE_UI2_H_
#define _ROSE_UI2_H_

/**
 * "UI2": the RmlUi remake of the retail interface, converted one dialog at a
 * time. The player chooses between the classic UI and UI2; with UI2 on, every
 * legacy dialog that already has an RmlUi replacement is kept hidden and the
 * replacement is shown instead. Dialogs not converted yet stay legacy, so UI2
 * is always a complete interface, just a mixed one while the work is ongoing.
 *
 * The switch is [VIDEO] UI2=1 in rose-next.ini ( or ROSE_UI2=1 ), and the
 * "/ui2" chat command flips it live and saves the choice. UI2 implies RmlUi.
 *
 * Converting a dialog = add its DLG_TYPE_* to kReplacedDialogs in RoseUi2.cpp
 * and give the RmlUi panel a visibility rule that reads IsActive(). Nothing in
 * the legacy dialog class has to change: it is hidden from the outside, every
 * frame, before the dialog manager draws.
 *
 * Some of the HUD is not a dialog at all but a draw call made straight from
 * IT_MGR ( the buff bar ). Those are "pieces": the legacy call site asks
 * IsPieceReplaced() and skips its draw.
 */

namespace RoseUi2 {

/// Whether UI2 is the chosen interface. Cheap; safe to call per frame.
bool IsActive();

/// The player's saved choice, whether or not it is live yet ( see SetActive ).
bool IsChosen();

/// Record the choice in rose-next.ini and apply it live. Returns false when it
/// could not be applied now -- RmlUi was not initialised at startup and the
/// overlay cannot be brought up mid-session -- in which case the saved choice
/// takes effect on the next start ( UI2=1 also switches RmlUi on ).
bool SetActive(bool bActive);

/// True when this legacy dialog type is currently replaced by a UI2 panel.
bool IsReplaced(int iDlgType);

/// HUD elements drawn outside the dialog system.
enum Piece {
    PIECE_BUFF_BAR, ///< CEndurancePack::Draw: buffs, summon/fuel gauges, worn gear
};

/// True when this legacy HUD piece is currently replaced by a UI2 panel.
bool IsPieceReplaced(Piece ePiece);

/// Called by IT_MGR::Update before any dialog draws: hides the replaced
/// legacy dialogs. Game code re-shows them freely ( state changes,
/// InitInterfacePos, OpenDialog ), so this is enforced per frame rather than
/// once.
void HideReplacedDialogs();

} // namespace RoseUi2

#endif /// _ROSE_UI2_H_
