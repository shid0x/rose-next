#pragma once

#include <windows.h>

/// What a stun means for the local player (2026-11-02, players' report: "in the middle of
/// many monsters you can't see you're stunned", "after a stun I don't go back to attacking").
///
/// - An alert: "STUNNED" pops up above the middle of the screen with a bar that runs down
///   for the stun's length, for as long as the avatar has the stun status. It reads the
///   status itself (CEndurancePack), so whatever ends the stun -- the server's clear, death,
///   a zone change -- ends the alert. Drawn in both the classic UI and UI2.
/// - Back to the fight: a stun stops the avatar on both sides (server Skill_ApplyIngSTATUS
///   -> SetCMD_STOP, client CEnduranceSkill -> SetCMD_STOP), which drops the attack. When it
///   ends, a player who was fighting a monster attacks again, as if they had pressed the
///   attack key: the monster they have selected, else the one they were fighting. Not if
///   they died, or are still asleep, or the monster is gone.
///
/// The server keeps the stuns themselves from chaining (server.toml [game]
/// stun_immunity_ms); nothing here changes what a stun does.
class CStunFeedback {
public:
    static CStunFeedback& Instance();

    /// From CEnduranceSkill::CreateEnduranceEntity, before the stun stops the avatar.
    void OnAvatarStunned();
    /// From CEndurancePack::DeleteEntityByStateType: the stun status left the avatar.
    void OnAvatarStunEnded();

    /// Inside the HUD sprite block (CGameStateMain::Render_GameMENU).
    void Draw();

    void LoadFont(const char* szFont, int iCharSet);
    void UnloadFont();

private:
    CStunFeedback();

    /// The avatar was fighting when the stun landed, and the monster's server index if it had
    /// one in hand (0 for a ground skill cast mid-attack: the selection says which).
    bool m_bWasFighting;
    int m_iFightServerIDX;
    unsigned int m_hFont; ///< HNODE (zz_interface.h); 32 px outline, loaded with the game fonts
    /// The stun status last drawn and when it first was: its start time is backdated to the
    /// packet's arrival (ApplyEffectOfSkill), which can be a second before it shows.
    const void* m_pShownStun;
    DWORD m_dwShownAt;
};
