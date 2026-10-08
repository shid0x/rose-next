#pragma once

#include <mutex>
#include <string>
#include <vector>

class CZoneTHREAD;
class classUSER;

/// The Cerberus Lair draw: a hand-run "instance" for one zone
/// (doc/cerberus-lair-brief.md, data in scripts/import-cerberus.py).
///
/// Every hour, on the hour, the gatekeeper in Arumic Valley opens registration
/// for ten minutes; at ten past, up to five of the players who signed are drawn,
/// put in a party and sent into the lair, where Cerberus sleeps. Its death (plus a
/// minute to loot) or a time limit sends everyone back to the temple door, and
/// the lair is reset for the next hour.
///
/// Threads. ProcZone runs in every zone thread; it acts in two of them only:
///   * the lair's thread owns the run -- the draw, arrivals (the fee is charged
///     there, in the player's own thread by then), evictions, the reset, the
///     sleeping Cerberus (spawned here, never by a regen point: a point cannot be
///     made to respawn on demand, CRegenPOINT::Reset only zeroes its count);
///   * the gatekeeper's zone mirrors the open/closed state into the NPC's event
///     value 0, which the register trigger (COND_011) and the dialog read.
/// OnRegister runs in the registering player's thread. One mutex covers it all.
class CCerberusLair {
public:
    static CCerberusLair& Instance();

    /// Called by every zone thread once per frame (CZoneTHREAD::Execute).
    void ProcZone(CZoneTHREAD* pZone);

    /// `Cerberus-Register` passed for this player (classUSER::Do_QuestTRIGGER).
    bool IsRegisterTrigger(unsigned long hash) const;
    void OnRegister(classUSER* pUSER);

    /// GM: `/cerberus status|open|draw|reset`.
    std::string GmStatus();
    std::string GmOpen(); // refused unless idle; returns what happened
    std::string GmDraw(); // refused unless registration is open
    void GmReset();

    /// The lair zone: no way out but the controller's (no save-point revive, no
    /// warp scroll; classUSER::Recv_cli_REVIVE_REQ / Recv_cli_USE_ITEM).
    static bool IsLair(short nZoneNO);

private:
    enum class State { Idle, Open, Run, Grace };

    struct Entry {
        unsigned long dbid;
        std::string name;
        bool arrived;
    };

    CCerberusLair();

    void ProcLair(CZoneTHREAD* pZone);
    void SyncGatekeeper(CZoneTHREAD* pZone);
    void Draw();
    void EndRun(const std::vector<classUSER*>& users,
        const std::vector<class CObjCHAR*>& mobs,
        const std::vector<class CObjITEM*>& items);
    void Evict(classUSER* pUSER);
    Entry* FindRoster(unsigned long dbid);
    static const char* StateName(State s);

    std::mutex m_Mutex;
    State m_State;
    std::vector<Entry> m_Registered;
    std::vector<Entry> m_Roster;
    long long m_LastOpenedHour;
    unsigned long m_dwOpenUntil;
    unsigned long m_dwRunStart;
    unsigned long m_dwGraceUntil;
    bool m_bReqOpen, m_bReqDraw, m_bReqReset;
    bool m_bBossSeen; // the run has seen Cerberus alive: only then is "none alive" a kill
    short m_nGateValue;

    // touched by one thread each (the lair's / the gatekeeper's)
    unsigned long m_dwLairTick;
    unsigned long m_dwLastSweep;
    unsigned long m_dwLastSpawn;
    unsigned long m_dwGateTick;
    unsigned long m_RegisterHash;
};
