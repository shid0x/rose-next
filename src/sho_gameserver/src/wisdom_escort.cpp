#include "stdafx.h"

#include "wisdom_escort.h"

#include <algorithm>
#include <cmath>

#include "CObjNPC.h"
#include "Calculation.h"
#include "GS_ListUSER.h"
#include "GS_Party.h"
#include "GS_ThreadZONE.h"
#include "GS_USER.h"
#include "ZoneLIST.h"
#include "nav/nav_grid.h"

namespace {

// Data written by scripts/import-wisdom-escort.py; keep the two in step.
constexpr short ESCORT_ZONE = 26; // Forest of Wisdom (JG04)
constexpr int KEEPER_NPC = 1132; // the town keeper: the dialog, placed in 34_31.IFO
constexpr int ESCORT = 225; // the walking, fighting keeper (a monster row on TEAMNO_USER)
constexpr int BLIGHT_GRUNTER = 226;
constexpr int BLIGHT_SMOULY = 227;
constexpr int WITHERED_GUARDIAN = 228;
constexpr int GRUNTER_CHIEF = 229;
const char* START_TRIGGER = "WisdomTree-Start";
// The run is a quest in the log (LIST_QUEST QUEST_ID), so the keeper shows "!" and the
// run has a timer: the start trigger registers it, JOIN gives it to the party members
// on the roster, END takes it from everyone when the run is over (and from anyone
// still holding it outside a run).
constexpr int QUEST_ID = 5521;
const char* JOIN_TRIGGER = "WisdomTree-Join";
const char* END_TRIGGER = "WisdomTree-End";
const char* SPEAKER = "Tree Keeper";

/// One stop of the round, world cm. The route was checked on JG04.lnav: every leg is
/// walkable, the nearest monster camp is 144 m away (the brief, "The route").
struct Stop {
    float x, y;
    int wave; // index into WAVES, -1 = a line and a pause
    const char* line; // said on arrival
    const char* walk[2]; // said on the way here, a third and two thirds of the leg in
};
constexpr Stop STOPS[] = {
    {554400.f, 537300.f, -1, nullptr, {nullptr, nullptr}}, // 0: the post, south of the trunk
    {563800.f, 539500.f, 0,
        "The east roots... these gnaw marks are fresh. Something has been feeding on the "
        "tree. Careful, they are still here!",
        {"I have tended this tree for thirty years. It has never been this quiet. Even the "
         "birds are gone.",
            "Look at the roots here, black at the tips. The rot is coming from the east."}},
    {562500.f, 549000.f, -1,
        "The bark is weeping sap here. The tree knows it is under attack. Come, the north "
        "side.",
        {"Well fought. Grunters never come this deep into the grove on their own.",
            "Someone is sending them. The question is who, and why the tree."}},
    {553900.f, 552500.f, 1,
        "Smoulies this close to the trunk? They never come here. Something drove them... "
        "They are coming!",
        {"They say this tree remembers every traveller who ever rested in its shade.",
            "Do you hear that? Something is moving in the north thicket."}},
    {549000.f, 545000.f, 2,
        "A guardian of the grove, rotten to the heartwood. It does not know us any more. "
        "Strike it down, it is beyond saving!",
        {"The smoulies were frightened, not angry. Whatever drives them is worse than they are.",
            "The guardians of the grove sleep on the west side. I hope they still know me."}},
    {550500.f, 536200.f, -1,
        "The pond is clouded. Whoever leads them poisoned the water first. Back to the "
        "post, quickly.",
        {"It pains me to strike a guardian. The blight took it from us long before we came.",
            "One look at the pond, and we will know where the poison came from."}},
    {554400.f, 537300.f, 3,
        "There, their chief! It came for the heart of the tree. Stand with me!",
        {"Grunter tracks, leading back towards the post. They are going for the heart of the "
         "tree!",
            "Hurry! If their chief reaches the trunk, the whole grove will wither."}},
};
constexpr int STOP_COUNT = (int)(sizeof(STOPS) / sizeof(STOPS[0]));

/// A wave: who, where it comes from (25-30 m out from its stop, away from the tree,
/// on clear walkable ground: JG04.lnav), and how many. `perExtra` more of a kind for
/// every roster member past the first.
struct WaveMob {
    int npc, base, perExtra;
};
struct Wave {
    float x, y;
    WaveMob mobs[3];
};
constexpr Wave WAVES[] = {
    {566000.f, 538800.f, {{BLIGHT_GRUNTER, 3, 1}, {0, 0, 0}, {0, 0, 0}}},
    {553300.f, 555000.f, {{BLIGHT_SMOULY, 2, 1}, {BLIGHT_GRUNTER, 1, 0}, {0, 0, 0}}},
    {546700.f, 546300.f, {{WITHERED_GUARDIAN, 1, 0}, {BLIGHT_GRUNTER, 2, 1}, {0, 0, 0}}},
    {554600.f, 534600.f, {{GRUNTER_CHIEF, 1, 0}, {BLIGHT_GRUNTER, 2, 1}, {BLIGHT_SMOULY, 2, 0}}},
};
constexpr int WAVE_SPREAD = 300; // cm around the wave's point

constexpr float START_RANGE = 5000.f; // party members this close to the post join the run
constexpr float FOLLOW_RANGE = 2000.f; // the escort waits when nobody of the roster is this close
constexpr float ABANDON_RANGE = 4000.f;
constexpr float REWARD_RANGE = 5000.f;
constexpr float ARRIVE_RANGE = 300.f;

constexpr unsigned long TICK_MS = 250;
constexpr unsigned long KEEPER_TICK_MS = 1000;
constexpr unsigned long INTRO_MS = 4 * 1000;
constexpr unsigned long STOP_PAUSE_MS = 5 * 1000;
constexpr unsigned long CLEARED_PAUSE_MS = 4 * 1000;
constexpr unsigned long DONE_MS = 10 * 1000;
constexpr unsigned long RUN_LIMIT_MS = 15 * 60 * 1000;
constexpr unsigned long ABANDON_MS = 60 * 1000;
constexpr unsigned long NAG_MS = 15 * 1000;
constexpr unsigned long REISSUE_MS = 5 * 1000; // re-issue a walk this often while moving (a stuck path)
constexpr unsigned long RETRY_MS = 1000; // and at most this often while stopped
constexpr unsigned long QUEST_SWEEP_MS = 2 * 1000;

// The reward: placeholders until the user picks one (the brief, "Open points").
constexpr int MIN_LEVEL = 40; // the start trigger checks it too
constexpr int REWARD_EXP_PCT = 5; // of the EXP the player's level needs, at most at level...
constexpr int REWARD_EXP_LEVEL_CAP = 70;
constexpr int REWARD_ZULY = 10000;
constexpr unsigned long REWARD_COOLDOWN_MS = 60 * 60 * 1000;

constexpr short KEEPER_FREE = 0;
constexpr short KEEPER_BUSY = 1; // the start trigger's COND_011 and the dialog's busy answer

/// a - b >= 0 on a wrapping millisecond clock
bool
Reached(unsigned long now, unsigned long when) {
    return (long)(now - when) >= 0;
}

float
Dist(CObjCHAR* a, float x, float y) {
    const float dx = a->Get_CurXPOS() - x, dy = a->Get_CurYPOS() - y;
    return std::sqrt(dx * dx + dy * dy);
}

void
Whisper(classUSER* pUSER, const std::string& msg) {
    pUSER->Send_gsv_WHISPER((char*)SPEAKER, (char*)msg.c_str());
}

/// classUSER::C_Cheater's mask (private there).
bool
IsGM(classUSER* pUSER) {
    return 0 != (pUSER->Get_RIGHT() & (RIGHT_MASTER | RIGHT_DEV | RIGHT_MG | RIGHT_NG));
}

bool
HasQuest(classUSER* pUSER) {
    return pUSER->Quest_GetRegistered(QUEST_ID) < QUEST_PER_PLAYER;
}

/// Run a quest trigger for a player as if they had asked for it: the server applies it
/// and GSV_QUEST_REPLY makes the client run the same trigger on its copy of the log.
/// Do_QuestTRIGGER clears the NPC the player last talked to; a dialog they have open
/// keeps it.
void
FireTrigger(classUSER* pUSER, unsigned long hash) {
    const int iNpc = pUSER->m_iLastEventNpcIDX;
    pUSER->Do_QuestTRIGGER(hash);
    pUSER->m_iLastEventNpcIDX = iNpc;
}

} // namespace

CWisdomEscort&
CWisdomEscort::Instance() {
    static CWisdomEscort s_Escort;
    return s_Escort;
}

CWisdomEscort::CWisdomEscort():
    m_State(State::Idle),
    m_Escort{0, 0, false},
    m_iStop(0),
    m_dwRunStart(0),
    m_dwPhaseUntil(0),
    m_dwLastMove(0),
    m_dwLastNag(0),
    m_dwAloneSince(0),
    m_bWaiting(false),
    m_fLegLen(0.f),
    m_iChatter(0),
    m_bReqStart(false),
    m_bReqStartGM(false),
    m_bReqStop(false),
    m_bReqSkip(false),
    m_ReqDBID(0),
    m_dwTick(0),
    m_dwKeeperTick(0),
    m_dwQuestSweep(0),
    m_StartHash(::StrToHashKey(START_TRIGGER)),
    m_JoinHash(::StrToHashKey(JOIN_TRIGGER)),
    m_EndHash(::StrToHashKey(END_TRIGGER)) {}

bool
CWisdomEscort::IsStartTrigger(unsigned long hash) const {
    return hash == m_StartHash;
}

const char*
CWisdomEscort::StateName(State s) {
    switch (s) {
        case State::Idle:
            return "idle";
        case State::Intro:
            return "intro";
        case State::Walk:
            return "walking";
        case State::Fight:
            return "fighting";
        case State::Pause:
            return "pausing";
        case State::Done:
            return "done";
    }
    return "?";
}

CObjCHAR*
CWisdomEscort::Resolve(const Tracked& t) const {
    if (!t.idx)
        return nullptr;
    CObjCHAR* p = g_pObjMGR->Get_CharOBJ((WORD)t.idx, true);
    if (!p || p->Get_CharNO() != t.npc || !p->GetZONE() || p->GetZONE()->Get_ZoneNO() != ESCORT_ZONE)
        return nullptr;
    return p;
}

CObjCHAR*
CWisdomEscort::Escort() const {
    return this->Resolve(m_Escort);
}

bool
CWisdomEscort::IsRoster(unsigned long dbid) const {
    for (const Member& m: m_Roster) {
        if (m.dbid == dbid)
            return true;
    }
    return false;
}

//-------------------------------------------------------------------------------------------------
/// The player's thread, after the trigger's conditions passed (the keeper is free,
/// level >= MIN_LEVEL). The forest's thread picks the request up.
void
CWisdomEscort::OnStart(classUSER* pUSER) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != State::Idle || m_bReqStart) {
        Whisper(pUSER, "I am already out on my rounds with others. Come back in a little while.");
        return;
    }
    m_bReqStart = true;
    m_bReqStartGM = false;
    m_ReqDBID = pUSER->m_dwDBID;
    LOG_INFO("[escort] start asked by {}", pUSER->Get_NAME());
}

//-------------------------------------------------------------------------------------------------
void
CWisdomEscort::ProcZone(CZoneTHREAD* pZone) {
    if (pZone->Get_ZoneNO() != ESCORT_ZONE)
        return;
    const unsigned long now = ::GetTickCount();
    if (!Reached(now, m_dwTick + TICK_MS))
        return;
    m_dwTick = now;

    std::vector<classUSER*> users;
    pZone->ForEachObject([&](CGameOBJ* pObj) {
        if (pObj->IsUSER())
            users.push_back((classUSER*)pObj);
    });

    std::lock_guard<std::mutex> lock(m_Mutex);

    if (m_bReqStop) {
        m_bReqStop = false;
        if (m_State != State::Idle)
            this->EndRun(pZone, users, "The keeper calls the round off.");
    }
    if (m_bReqStart) {
        m_bReqStart = false;
        if (m_State == State::Idle)
            this->StartRun(pZone, users);
    }
    if (m_State != State::Idle)
        this->ProcRun(pZone, users);

    if (Reached(now, m_dwKeeperTick + KEEPER_TICK_MS)) {
        m_dwKeeperTick = now;
        this->SyncKeeper(pZone);
    }

    // The quest outside a run: a start the run refused (busy, too far, the escort did not
    // spawn), a player who logged out mid-run and came back, a party member who left
    // the forest before the end. After the requests, so a fresh start keeps its quest.
    if (Reached(now, m_dwQuestSweep + QUEST_SWEEP_MS)) {
        m_dwQuestSweep = now;
        for (classUSER* pUSER: users) {
            const bool bInRun = m_State != State::Idle && this->IsRoster(pUSER->m_dwDBID);
            if (!bInRun && HasQuest(pUSER)) {
                FireTrigger(pUSER, m_EndHash);
                LOG_INFO("[escort] took the quest back from {} (not in a run)", pUSER->Get_NAME());
            }
        }
    }
}

//-------------------------------------------------------------------------------------------------
/// The town keeper is hidden while the escort walks, and its event value 0 tells the
/// start trigger (COND_011) and the dialog that he is busy.
void
CWisdomEscort::SyncKeeper(CZoneTHREAD* pZone) {
    CObjNPC* pNPC = g_pZoneLIST->Get_LocalNPC(KEEPER_NPC);
    if (!pNPC || pNPC->GetZONE() != pZone)
        return;
    const bool bFree = m_State == State::Idle;
    const short nWant = bFree ? KEEPER_FREE : KEEPER_BUSY;
    if (pNPC->Get_ObjVAR(0) != nWant)
        pNPC->Set_ObjVAR(0, nWant);
    pNPC->VSet_SHOW(bFree ? 1 : 0); // sends only on a change
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex, from the forest's thread. The roster is the starter and the members
/// of the starter's party standing near the post (a GM start: the GM's party anywhere
/// in the forest).
void
CWisdomEscort::StartRun(CZoneTHREAD* pZone, const std::vector<classUSER*>& users) {
    classUSER* pStarter = nullptr;
    for (classUSER* pUSER: users) {
        if (pUSER->m_dwDBID == m_ReqDBID)
            pStarter = pUSER;
    }
    if (!pStarter || pStarter->Get_HP() <= 0) {
        LOG_INFO("[escort] start dropped: the starter is not in the forest");
        return;
    }
    const Stop& post = STOPS[0];
    if (!m_bReqStartGM && pStarter->Get_LEVEL() < MIN_LEVEL) {
        Whisper(pStarter, fmt::format("These woods are too dangerous for you yet. Come back at level {}.", MIN_LEVEL));
        return;
    }
    if (!m_bReqStartGM && Dist(pStarter, post.x, post.y) > START_RANGE) {
        Whisper(pStarter, "Come to the great tree, and bring your friends: I set out from there.");
        return;
    }

    m_Roster.clear();
    for (classUSER* pUSER: users) {
        if (pUSER->Get_HP() <= 0)
            continue;
        const bool bSelf = pUSER == pStarter;
        const bool bParty = pStarter->GetPARTY() && pUSER->GetPARTY() == pStarter->GetPARTY();
        if (!bSelf && !bParty)
            continue;
        if (!bSelf && !m_bReqStartGM && Dist(pUSER, post.x, post.y) > START_RANGE)
            continue;
        m_Roster.push_back({pUSER->m_dwDBID, pUSER->Get_NAME()});
    }

    CObjMOB* pMob =
        pZone->RegenCharacter(post.x, post.y, 1, ESCORT, 1, TEAMNO_USER, true);
    if (!pMob) {
        Whisper(pStarter, "Not now... (the escort could not be spawned: is the data baked?)");
        LOG_WARN("[escort] could not spawn the escort (NPC row {})", ESCORT);
        m_Roster.clear();
        return;
    }
    m_Escort = {pMob->Get_INDEX(), ESCORT, true};
    m_Wave.clear();
    m_iStop = 0;
    m_State = State::Intro;
    m_dwRunStart = ::GetTickCount();
    m_dwPhaseUntil = m_dwRunStart + INTRO_MS;
    m_dwLastMove = 0;
    m_dwLastNag = 0;
    m_dwAloneSince = 0;
    m_bWaiting = false;
    m_fLegLen = 0.f;
    m_iChatter = 0;

    // The quest for everyone on the roster: the starter has it from the start trigger
    // (a GM start does not), the party members get it here. Its start message is the
    // keeper's welcome.
    std::string names;
    for (size_t i = 0; i < m_Roster.size(); i++)
        names += (i ? ", " : "") + m_Roster[i].name;
    for (classUSER* pUSER: users) {
        if (this->IsRoster(pUSER->m_dwDBID) && !HasQuest(pUSER))
            FireTrigger(pUSER, m_JoinHash);
    }
    LOG_INFO("[escort] run started for {}{}", names, m_bReqStartGM ? " (GM)" : "");
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex, from the forest's thread.
void
CWisdomEscort::ProcRun(CZoneTHREAD* pZone, const std::vector<classUSER*>& users) {
    const unsigned long now = ::GetTickCount();

    std::vector<classUSER*> roster;
    for (classUSER* pUSER: users) {
        if (this->IsRoster(pUSER->m_dwDBID))
            roster.push_back(pUSER);
    }

    CObjCHAR* pEscort = this->Escort();

    if (m_State == State::Done) {
        if (Reached(now, m_dwPhaseUntil))
            this->EndRun(pZone, users, nullptr);
        return;
    }

    // The ways to fail, in every active state.
    if (!pEscort) {
        for (classUSER* pUSER: roster)
            Whisper(pUSER, "The Tree Keeper has fallen... The tree is left to its attackers.");
        LOG_INFO("[escort] the escort died at stop {}", m_iStop);
        this->EndRun(pZone, users, nullptr);
        return;
    }
    if (Reached(now, m_dwRunStart + RUN_LIMIT_MS)) {
        this->Say(pEscort, roster, "We are too slow. I must go back and warn the others alone.", true);
        LOG_INFO("[escort] run timed out at stop {}", m_iStop);
        this->EndRun(pZone, users, nullptr);
        return;
    }
    bool bNear = false, bClose = false;
    for (classUSER* pUSER: roster) {
        if (pUSER->Get_HP() <= 0)
            continue;
        const float d = Dist(pUSER, pEscort->Get_CurXPOS(), pEscort->Get_CurYPOS());
        bNear |= d <= ABANDON_RANGE;
        bClose |= d <= FOLLOW_RANGE;
    }
    if (bNear) {
        m_dwAloneSince = 0;
    } else if (!m_dwAloneSince) {
        m_dwAloneSince = now;
    } else if (Reached(now, m_dwAloneSince + ABANDON_MS)) {
        this->Say(pEscort, roster, "Alone, I cannot do this. I am going back.", true);
        LOG_INFO("[escort] abandoned at stop {}", m_iStop);
        this->EndRun(pZone, users, nullptr);
        return;
    }

    // The wave: send each new monster at the escort once, the tick after its spawn
    // (it joins the zone on the next pass). From then on its own AI decides; hitting
    // it is how a player pulls it away (the ambushers' "when hit" pattern).
    bool bWaveAlive = false;
    for (Tracked& t: m_Wave) {
        CObjCHAR* pMob = this->Resolve(t);
        if (!pMob)
            continue;
        bWaveAlive = true;
        if (!t.ordered) {
            t.ordered = true;
            pMob->SetCMD_RUNnATTACK(pEscort->Get_INDEX());
        }
    }

    if (m_bReqSkip) {
        m_bReqSkip = false;
        if (m_State == State::Fight) {
            for (const Tracked& t: m_Wave) {
                if (CObjCHAR* pMob = this->Resolve(t))
                    pMob->Add_DAMAGE(pMob->Get_HP() + 1);
            }
        } else if (m_State == State::Walk) {
            this->Arrive(pZone, pEscort, roster); // where he stands; a wave still comes from its point
            return;
        } else {
            m_dwPhaseUntil = now;
        }
    }

    switch (m_State) {
        case State::Intro:
            if (Reached(now, m_dwPhaseUntil)) {
                this->Say(pEscort, roster, "Let us begin with the east roots. Follow me.", false);
                m_iStop = 1;
                m_State = State::Walk;
                m_fLegLen = 0.f;
                m_iChatter = 0;
            }
            break;

        case State::Walk: {
            if (pEscort->Get_TARGET())
                break; // fighting: its AI has it
            const Stop& to = STOPS[m_iStop];
            const WORD wCmd = pEscort->Get_COMMAND();
            const float fLeft = Dist(pEscort, to.x, to.y);
            if (m_fLegLen <= 0.f)
                m_fLegLen = (std::max)(fLeft, 1.f);
            // The lines on the way, at a third and two thirds of the leg: a leg is
            // ~100 m, 20-25 s at his pace, and a stop on its own left long silences.
            if (m_iChatter < 2 && to.walk[m_iChatter]
                && 1.f - fLeft / m_fLegLen >= (m_iChatter + 1) / 3.f) {
                this->Say(pEscort, roster, to.walk[m_iChatter], false);
                m_iChatter++;
            }
            // A path can end a little short of a point in a blocked cell: stopped
            // close by after a walk counts too.
            if (fLeft <= ARRIVE_RANGE || (wCmd == CMD_STOP && m_dwLastMove && fLeft <= 2 * ARRIVE_RANGE)) {
                this->Arrive(pZone, pEscort, roster);
                break;
            }
            if (!bClose) {
                if (!m_bWaiting) {
                    m_bWaiting = true;
                    pEscort->SetCMD_STOP();
                }
                if (Reached(now, m_dwLastNag + NAG_MS)) {
                    m_dwLastNag = now;
                    this->Say(pEscort, roster, "Stay close! I will not go on alone.", false);
                }
                break;
            }
            // Walk to the stop when stopped (arrived somewhere else, a fight just
            // ended, the party caught up), and re-issue now and then while moving,
            // in case a path was refused or got stuck.
            const bool bMoving = wCmd == CMD_MOVE;
            if (m_bWaiting || Reached(now, m_dwLastMove + (bMoving ? REISSUE_MS : RETRY_MS))) {
                m_bWaiting = false;
                m_dwLastMove = now;
                Nav::Grid* pGrid = pZone->NavGrid();
                // at a run (LIST_NPC run speed, ~70% of a player's): at a walk the
                // party waited on him between every stop
                pEscort->Nav_MoveOrStraight(to.x, to.y, 1 /* run */, pGrid ? pGrid->AnchorZ(to.x, to.y) : -32768.f);
            }
            break;
        }

        case State::Fight:
            if (!bWaveAlive) {
                m_Wave.clear();
                if (m_iStop == STOP_COUNT - 1) {
                    this->Succeed(pEscort, roster);
                } else {
                    this->Say(pEscort, roster, "They are gone. Let us go on.", false);
                    m_State = State::Pause;
                    m_dwPhaseUntil = now + CLEARED_PAUSE_MS;
                }
            }
            break;

        case State::Pause:
            if (Reached(now, m_dwPhaseUntil)) {
                m_iStop++;
                m_State = State::Walk;
                m_dwLastMove = 0;
                m_fLegLen = 0.f;
                m_iChatter = 0;
            }
            break;

        default:
            break;
    }
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex: the escort reached STOPS[m_iStop].
void
CWisdomEscort::Arrive(CZoneTHREAD* pZone, CObjCHAR* pEscort, const std::vector<classUSER*>& roster) {
    const Stop& at = STOPS[m_iStop];
    if (at.line)
        this->Say(pEscort, roster, at.line, false);
    LOG_INFO("[escort] reached stop {}", m_iStop);
    if (at.wave >= 0) {
        this->SpawnWave(pZone, at.wave);
        m_State = State::Fight;
    } else {
        m_State = State::Pause;
        m_dwPhaseUntil = ::GetTickCount() + STOP_PAUSE_MS;
    }
}

void
CWisdomEscort::SpawnWave(CZoneTHREAD* pZone, int iWave) {
    const Wave& w = WAVES[iWave];
    const int iExtra = (std::max)(0, (int)m_Roster.size() - 1);
    m_Wave.clear();
    for (const WaveMob& m: w.mobs) {
        if (!m.npc)
            continue;
        const int iCount = m.base + m.perExtra * iExtra;
        for (int i = 0; i < iCount; i++) {
            // One at a time: RegenCharacter returns only the last monster it made.
            CObjMOB* pMob = pZone->RegenCharacter(w.x, w.y, WAVE_SPREAD, m.npc, 1, TEAMNO_MOB, true);
            if (pMob)
                m_Wave.push_back({pMob->Get_INDEX(), m.npc, false});
            else
                LOG_WARN("[escort] could not spawn NPC row {} for wave {}", m.npc, iWave);
        }
    }
    LOG_INFO("[escort] wave {}: {} monsters for a roster of {}", iWave, m_Wave.size(), m_Roster.size());
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex: the last wave is dead. The reward goes to roster members near the
/// escort, at most once an hour each; the quest leaves everyone's log.
void
CWisdomEscort::Succeed(CObjCHAR* pEscort, const std::vector<classUSER*>& roster) {
    const unsigned long now = ::GetTickCount();
    this->Say(pEscort, roster,
        "It is over. The tree will heal now, thanks to you. Take this, with the forest's "
        "gratitude.",
        true);
    for (classUSER* pUSER: roster) {
        if (pUSER->Get_HP() <= 0
            || Dist(pUSER, pEscort->Get_CurXPOS(), pEscort->Get_CurYPOS()) > REWARD_RANGE) {
            Whisper(pUSER, "You were too far away at the end to share the reward.");
            continue;
        }
        auto it = m_RewardedAt.find(pUSER->m_dwDBID);
        if (it != m_RewardedAt.end() && !Reached(now, it->second + REWARD_COOLDOWN_MS)) {
            Whisper(pUSER, "You have already been rewarded this hour. The tree thanks you all the same.");
            continue;
        }
        m_RewardedAt[pUSER->m_dwDBID] = now;
        const int iLevel = (std::min)((int)pUSER->Get_LEVEL(), REWARD_EXP_LEVEL_CAP);
        const __int64 iExp = CCal::Get_NeedRawEXP(iLevel) * REWARD_EXP_PCT / 100;
        pUSER->Add_EXP(iExp, false, 0);
        // GSV_REWARD_MONEY, not the plain money update: the client prints it as a quest
        // reward (purple, "You have received ... zuly"), where the plain one was a
        // system line nobody noticed
        pUSER->Add_MoneyNSend(REWARD_ZULY, GSV_REWARD_MONEY);
        LOG_INFO("[escort] rewarded {}: {} EXP, {} zuly", pUSER->Get_NAME(), iExp, REWARD_ZULY);
    }
    for (classUSER* pUSER: roster) {
        if (HasQuest(pUSER))
            FireTrigger(pUSER, m_EndHash);
    }
    m_State = State::Done;
    m_dwPhaseUntil = now + DONE_MS;
    LOG_INFO("[escort] run succeeded");
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex, from the forest's thread: the run's monsters go, the escort leaves
/// silently (the GM "npc DEL" pattern, as the Cerberus sleeper swap), the town
/// keeper comes back.
void
CWisdomEscort::EndRun(CZoneTHREAD* pZone, const std::vector<classUSER*>& users, const char* szWhy) {
    // The quest leaves the log of the roster members here; the sweep in ProcZone takes it
    // from the others when they come back.
    for (classUSER* pUSER: users) {
        if (this->IsRoster(pUSER->m_dwDBID) && HasQuest(pUSER))
            FireTrigger(pUSER, m_EndHash);
    }
    // A suicide, as AI action 23 does it: no killer, so no drop, no EXP.
    for (const Tracked& t: m_Wave) {
        if (CObjCHAR* pMob = this->Resolve(t))
            pMob->Add_DAMAGE(pMob->Get_HP() + 1);
    }
    m_Wave.clear();
    if (CObjCHAR* pEscort = this->Escort()) {
        if (szWhy)
            pEscort->Send_gsv_CHAT((char*)szWhy);
        pZone->Sub_DIRECT(pEscort);
        delete pEscort;
    }
    m_Escort = {0, 0, false};
    m_Roster.clear();
    m_State = State::Idle;
    m_iStop = 0;
    this->SyncKeeper(pZone);
    LOG_INFO("[escort] run ended");
}

//-------------------------------------------------------------------------------------------------
/// A line over the escort's head (the client shows a monster's local chat as a
/// bubble), and to the roster as a whisper when it matters.
void
CWisdomEscort::Say(CObjCHAR* pEscort,
    const std::vector<classUSER*>& roster,
    const char* szLine,
    bool bWhisper) {
    if (pEscort)
        pEscort->Send_gsv_CHAT((char*)szLine);
    if (bWhisper) {
        for (classUSER* pUSER: roster)
            Whisper(pUSER, szLine);
    }
}

//-------------------------------------------------------------------------------------------------
std::string
CWisdomEscort::GmStatus() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    std::string s = fmt::format("Wisdom Tree escort: {}", StateName(m_State));
    if (m_State != State::Idle) {
        s += fmt::format(", stop {}/{}, {} s in, wave {} up",
            m_iStop,
            STOP_COUNT - 1,
            (::GetTickCount() - m_dwRunStart) / 1000,
            m_Wave.size());
        if (CObjCHAR* pEscort = this->Escort())
            s += fmt::format(", keeper HP {}/{} at ({:.0f}, {:.0f}) m{}",
                pEscort->Get_HP(),
                pEscort->Get_MaxHP(),
                pEscort->Get_CurXPOS() / 100.f,
                pEscort->Get_CurYPOS() / 100.f,
                m_bWaiting ? ", waiting for the party" : "");
        for (const Member& m: m_Roster)
            s += " | " + m.name;
    }
    return s;
}

std::string
CWisdomEscort::GmStart(classUSER* pUSER) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != State::Idle || m_bReqStart)
        return fmt::format("Escort: a run is already {}.", StateName(m_State));
    if (!pUSER->GetZONE() || pUSER->GetZONE()->Get_ZoneNO() != ESCORT_ZONE)
        return "Escort: go to the Forest of Wisdom first (zone 26).";
    m_bReqStart = true;
    m_bReqStartGM = true;
    m_ReqDBID = pUSER->m_dwDBID;
    return "Escort: starting a run for you and your party.";
}

std::string
CWisdomEscort::GmStop() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State == State::Idle)
        return "Escort: no run to stop.";
    m_bReqStop = true;
    return "Escort: stopping the run.";
}

std::string
CWisdomEscort::GmSkip() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State == State::Idle)
        return "Escort: no run to skip in.";
    m_bReqSkip = true;
    return fmt::format("Escort: skipping ahead ({}).",
        m_State == State::Fight ? "the wave dies" : m_State == State::Walk ? "he is at his stop" : "no more waiting");
}
