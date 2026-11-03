#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CZoneTHREAD;
class classUSER;
class CObjCHAR;

/// The Wisdom Tree escort: a party follows the Tree Keeper around the great tree in
/// the Forest of Wisdom while monsters ambush him at some of his stops
/// (doc/wisdom-tree-escort-brief.md, data in scripts/import-wisdom-escort.py).
///
/// The run happens in the live forest, one at a time, started on demand from the
/// keeper's dialog, and a quest in the log while it lasts (the "!" over the keeper and
/// the timer). The escort is an ordinary monster on the players' team
/// (TEAMNO_USER): allied with every player, an enemy of every monster, fighting
/// through its own AI file. This controller walks it from stop to stop the way
/// CObjSUMMON::Proc walks a summon after its owner, and owns the waves.
///
/// Threads. ProcZone runs in every zone thread and acts in the forest's only: the
/// run, the escort, the waves, the town keeper's visibility and event value.
/// OnStart and the GM calls only record a request under the mutex.
class CWisdomEscort {
public:
    static CWisdomEscort& Instance();

    /// Called by every zone thread once per frame (CZoneTHREAD::Execute).
    void ProcZone(CZoneTHREAD* pZone);

    /// `WisdomTree-Start` passed for this player (classUSER::Do_QuestTRIGGER).
    bool IsStartTrigger(unsigned long hash) const;
    void OnStart(classUSER* pUSER);

    /// GM: `/escort status|start|stop|skip`.
    std::string GmStatus();
    std::string GmStart(classUSER* pUSER); // the GM and their party, wherever they stand
    std::string GmStop();
    std::string GmSkip(); // the current wave dies, or the escort is put on its stop

private:
    enum class State { Idle, Intro, Walk, Fight, Pause, Done };

    struct Member {
        unsigned long dbid;
        std::string name;
    };
    /// A monster of the run, by object index and row: indices are reused.
    struct Tracked {
        int idx;
        int npc;
        bool ordered; // sent at the escort (the tick after it was spawned)
    };

    CWisdomEscort();

    void StartRun(CZoneTHREAD* pZone, const std::vector<classUSER*>& users);
    void ProcRun(CZoneTHREAD* pZone, const std::vector<classUSER*>& users);
    void Arrive(CZoneTHREAD* pZone, CObjCHAR* pEscort, const std::vector<classUSER*>& roster);
    void SpawnWave(CZoneTHREAD* pZone, int iWave);
    void Succeed(CObjCHAR* pEscort, const std::vector<classUSER*>& roster);
    void EndRun(CZoneTHREAD* pZone, const std::vector<classUSER*>& users, const char* szWhy);
    void SyncKeeper(CZoneTHREAD* pZone);
    void Say(CObjCHAR* pEscort, const std::vector<classUSER*>& roster, const char* szLine, bool bWhisper);

    CObjCHAR* Resolve(const Tracked& t) const;
    CObjCHAR* Escort() const;
    bool IsRoster(unsigned long dbid) const;
    static const char* StateName(State s);

    std::mutex m_Mutex;
    State m_State;
    std::vector<Member> m_Roster;
    Tracked m_Escort;
    std::vector<Tracked> m_Wave;
    int m_iStop; // the stop the escort is heading to, or standing at
    unsigned long m_dwRunStart;
    unsigned long m_dwPhaseUntil;
    unsigned long m_dwLastMove;
    unsigned long m_dwLastNag;
    unsigned long m_dwAloneSince; // no roster member near the escort since (0 = someone is)
    bool m_bWaiting; // stopped for the party
    float m_fLegLen; // the current leg's length when it began, for the lines on the way
    int m_iChatter; // lines on the way said this leg
    std::unordered_map<unsigned long, unsigned long> m_RewardedAt; // dbid -> tick

    bool m_bReqStart, m_bReqStartGM, m_bReqStop, m_bReqSkip;
    unsigned long m_ReqDBID;

    // the forest's thread only
    unsigned long m_dwTick;
    unsigned long m_dwKeeperTick;
    unsigned long m_dwQuestSweep;
    unsigned long m_StartHash, m_JoinHash, m_EndHash;
};
