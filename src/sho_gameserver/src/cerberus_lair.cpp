#include "stdafx.h"

#include "cerberus_lair.h"

#include <algorithm>
#include <ctime>
#include <random>

#include "CObjITEM.h"
#include "CObjNPC.h"
#include "GS_ListUSER.h"
#include "GS_Party.h"
#include "GS_ThreadZONE.h"
#include "GS_USER.h"
#include "ZoneLIST.h"

namespace {

// Data written by scripts/import-cerberus.py; keep the two in step.
constexpr short LAIR_ZONE = 49;
constexpr short EXIT_ZONE = 53; // Arumic Valley, the temple door
const char* EXIT_EVENT = "WARP-CERBERUS-LP02";
constexpr int GATEKEEPER_NPC = 4149;
constexpr int CERBERUS_AWAKE = 2682;
constexpr int CERBERUS_ASLEEP = 2683;
// The Hellhounds Cerberus calls at HOUND_CALL_PCT of its HP. They are spawned here,
// at the crater, not by its AI: a monster the client creates is put on the highest
// surface at its spot (CObjMOB::Create -> GetHeightTop), and the fight drifts under
// the lair's rock arches, so hounds called where Cerberus stood landed on the rock
// roof -- heard, never seen (2026-10-09). The crater is open sky: Cerberus's own
// spawn renders there.
constexpr int HELLHOUND = 2684;
constexpr int HOUND_CALL_PCT[] = {66, 33};
constexpr int HOUNDS_PER_CALL = 2;
constexpr int HOUND_SPREAD = 100; // cm around the crater point
// The way in, spawned when a run starts: two packs of whelps, then the Warden of the
// Seal on the crater rim. Cerberus is put in the crater only once the Warden falls.
// World cm, picked on open walkable ground (heights and editor shots, 2026-10-09).
constexpr int WHELP = 2680;
constexpr int WARDEN = 2681;
struct SpawnAt {
    float x, y;
    int range; // cm; 0 is not allowed by RegenCharacter
    int npc, count;
};
constexpr SpawnAt RUN_SPAWNS[] = {
    {489800.f, 547600.f, 200, WHELP, 3}, // the clearing in the pines, south of the entrance
    {499000.f, 548900.f, 200, WHELP, 3}, // the open ice south of the river (placed in game)
    {503200.f, 553700.f, 1, WARDEN, 1}, // open snow before the crater rim
};
// The ice wall between the Warden and the crater: dynamic zone objects
// (GSV_ZONE_OBJECTS), up from the draw until the Warden dies: invisible collision
// panels along the ridge (the map's own kind of invisible wall) and Luna ice
// crystals across the southern gap, the one place it is meant to be seen. The
// placements are scripts/cerberus-wall.py's, which proves with a flood fill over
// the lair's real collision that they seal the crater. Re-run it with --emit after
// any change here or to the lair's objects.
constexpr BYTE WALL_GROUP = 1;
// scripts/cerberus-wall.py --spacing 5 --seed 7 --emit: 30 pieces
const tagZONE_OBJECT ICE_WALL[] = {   // id, kind (1 = invisible panel), world cm, rotation deg, scale % x/y/z
    {2, 1, 506800.f, 558820.f, 735.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 558050.f, 737.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 557280.f, 589.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 556520.f, 610.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 555750.f, 694.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 554980.f, 489.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 553450.f, 236.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 552680.f, 370.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 551920.f, 391.f, 270, 639, 1454, 555},
    {2, 1, 506800.f, 550380.f, 229.f, 270, 639, 1454, 555},
    {48, 0, 506720.f, 549840.f, 143.f, 150, 100, 100, 100},
    {50, 0, 507100.f, 550040.f, 521.f, 60, 100, 100, 60},
    {48, 0, 507170.f, 549620.f, 160.f, 180, 100, 100, 100},
    {48, 0, 507550.f, 549820.f, 338.f, 300, 100, 100, 100},
    {50, 0, 507620.f, 549400.f, 119.f, 15, 100, 100, 60},
    {48, 0, 508000.f, 549600.f, 199.f, 30, 100, 100, 100},
    {48, 0, 508060.f, 549170.f, 99.f, 255, 100, 100, 100},
    {50, 0, 508440.f, 549370.f, 89.f, 45, 100, 100, 60},
    {48, 0, 508510.f, 548950.f, 50.f, 165, 100, 100, 100},
    {48, 0, 508890.f, 549150.f, 66.f, 270, 100, 100, 100},
    {50, 0, 508960.f, 548730.f, 33.f, 15, 100, 100, 60},
    {48, 0, 509340.f, 548930.f, 40.f, 240, 100, 100, 100},
    {48, 0, 509410.f, 548500.f, 8.f, 90, 100, 100, 100},
    {50, 0, 509790.f, 548700.f, 0.f, 15, 100, 100, 60},
    {48, 0, 509850.f, 548280.f, 2.f, 30, 100, 100, 100},
    {48, 0, 510230.f, 548480.f, 0.f, 195, 100, 100, 100},
    {50, 0, 510300.f, 548050.f, 5.f, 195, 100, 100, 60},
    {48, 0, 510680.f, 548260.f, 16.f, 30, 100, 100, 100},
    {48, 0, 510750.f, 547830.f, 14.f, 105, 100, 100, 100},
    {50, 0, 511130.f, 548030.f, 67.f, 30, 100, 100, 60},
};
constexpr int ICE_WALL_COUNT = (int)(sizeof(ICE_WALL) / sizeof(ICE_WALL[0]));
const char* REGISTER_TRIGGER = "Cerberus-Register";
const char* SPEAKER = "Yelena";
// The crater, tsuki's own spawn point (31_30.IFO), world cm.
constexpr float CRATER_X = 510801.f;
constexpr float CRATER_Y = 554720.f;

constexpr short GATE_CLOSED = 0;
constexpr short GATE_OPEN = 1; // the trigger's COND_011 and the dialog's --open-when

constexpr int OPEN_MINUTES = 10; // registration runs :00 to :10, server local time
constexpr size_t MAX_DRAWN = 5;
constexpr int MIN_LEVEL = 140;
constexpr int FEE = 100000;
constexpr BYTE PARTY_LEVEL = 5; // a party holds level / 5 + 4 members

constexpr unsigned long TICK_MS = 500;
constexpr unsigned long RUN_LIMIT_MS = 20 * 60 * 1000;
constexpr unsigned long GRACE_MS = 60 * 1000;
constexpr unsigned long ARRIVAL_MS = 60 * 1000;
constexpr unsigned long GM_OPEN_MS = 2 * 60 * 1000;
constexpr unsigned long SWEEP_MS = 5 * 1000;
constexpr unsigned long RESPAWN_MS = 5 * 1000;

/// a - b >= 0 on a wrapping millisecond clock
bool
Reached(unsigned long now, unsigned long when) {
    return (long)(now - when) >= 0;
}

void
LocalClock(long long& hourKey, int& minute, int& second) {
    time_t t = time(nullptr);
    tm lt;
    localtime_s(&lt, &t);
    minute = lt.tm_min;
    second = lt.tm_sec;
    hourKey = (long long)(t - lt.tm_min * 60 - lt.tm_sec) / 60;
}

void
Whisper(classUSER* pUSER, const std::string& msg) {
    pUSER->Send_gsv_WHISPER((char*)SPEAKER, (char*)msg.c_str());
}

void
Announce(const std::string& msg) {
    g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT((char*)msg.c_str(), (char*)SPEAKER);
}

/// classUSER::C_Cheater's mask (private there): GMs may stay in the lair to watch.
bool
IsGM(classUSER* pUSER) {
    return 0 != (pUSER->Get_RIGHT() & (RIGHT_MASTER | RIGHT_DEV | RIGHT_MG | RIGHT_NG));
}

std::string
Names(const std::vector<classUSER*>& users) {
    std::string out;
    for (size_t i = 0; i < users.size(); i++) {
        if (i)
            out += (i + 1 == users.size()) ? " and " : ", ";
        out += users[i]->Get_NAME();
    }
    return out;
}

} // namespace

CCerberusLair&
CCerberusLair::Instance() {
    static CCerberusLair s_Lair;
    return s_Lair;
}

CCerberusLair::CCerberusLair():
    m_State(State::Idle),
    m_LastOpenedHour(-1),
    m_dwOpenUntil(0),
    m_dwRunStart(0),
    m_dwGraceUntil(0),
    m_bReqOpen(false),
    m_bReqDraw(false),
    m_bReqReset(false),
    m_bBossSeen(false),
    m_bRunSpawned(false),
    m_bWardenSeen(false),
    m_bSealBroken(false),
    m_bWallUp(false),
    m_nHoundCalls(0),
    m_nGateValue(GATE_CLOSED),
    m_dwLairTick(0),
    m_dwLastSweep(0),
    m_dwLastSpawn(0),
    m_dwGateTick(0),
    m_RegisterHash(::StrToHashKey(REGISTER_TRIGGER)) {}

bool
CCerberusLair::IsRegisterTrigger(unsigned long hash) const {
    return hash == m_RegisterHash;
}

const char*
CCerberusLair::StateName(State s) {
    switch (s) {
        case State::Idle:
            return "idle";
        case State::Open:
            return "open";
        case State::Run:
            return "run";
        case State::Grace:
            return "grace";
    }
    return "?";
}

void
CCerberusLair::ProcZone(CZoneTHREAD* pZone) {
    const short nZoneNO = pZone->Get_ZoneNO();
    if (nZoneNO == LAIR_ZONE)
        this->ProcLair(pZone);
    else if (nZoneNO == EXIT_ZONE)
        this->SyncGatekeeper(pZone);
}

//-------------------------------------------------------------------------------------------------
/// The gatekeeper's zone: event value 0 follows the state. Set_ObjVAR(0) sends
/// GSV_SET_EVENT_STATUS to the NPC's sector when the value changes, which is what
/// the dialog's QF_getNpcQuestZeroVal reads.
void
CCerberusLair::SyncGatekeeper(CZoneTHREAD* pZone) {
    const unsigned long now = ::GetTickCount();
    if (!Reached(now, m_dwGateTick + TICK_MS))
        return;
    m_dwGateTick = now;

    short nWant;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        nWant = m_nGateValue;
    }
    CObjNPC* pNPC = g_pZoneLIST->Get_LocalNPC(GATEKEEPER_NPC);
    if (pNPC && pNPC->GetZONE() == pZone && pNPC->Get_ObjVAR(0) != nWant)
        pNPC->Set_ObjVAR(0, nWant);
}

//-------------------------------------------------------------------------------------------------
void
CCerberusLair::OnRegister(classUSER* pUSER) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != State::Open) {
        Whisper(pUSER, "The seal holds fast. Come back on the hour.");
        return;
    }
    for (const Entry& e: m_Registered) {
        if (e.dbid == pUSER->m_dwDBID) {
            Whisper(pUSER,
                fmt::format("Your name is already written. {} signed so far.", m_Registered.size()));
            return;
        }
    }
    m_Registered.push_back({pUSER->m_dwDBID, pUSER->Get_NAME(), false});
    const long remain = (long)(m_dwOpenUntil - ::GetTickCount()) / 1000;
    Whisper(pUSER,
        fmt::format("Your name is written ({} signed). The draw is in {} min {} s. Those chosen "
                    "pay {} zuly; the others pay nothing.",
            m_Registered.size(),
            (std::max)(0L, remain) / 60,
            (std::max)(0L, remain) % 60,
            FEE));
    LOG_INFO("[cerberus] {} registered ({} signed)", pUSER->Get_NAME(), m_Registered.size());
}

CCerberusLair::Entry*
CCerberusLair::FindRoster(unsigned long dbid) {
    for (Entry& e: m_Roster) {
        if (e.dbid == dbid)
            return &e;
    }
    return nullptr;
}

void
CCerberusLair::Evict(classUSER* pUSER) {
    tagEVENTPOS* pEvent = g_pZoneLIST->Get_EventPOS(EXIT_ZONE, (char*)EXIT_EVENT);
    tPOINTF pos = pEvent ? pEvent->m_Position : g_pZoneLIST->GetZONE(EXIT_ZONE)->Get_StartPOS();
    pUSER->Send_gsv_RELAY_REQ(RELAY_TYPE_RECALL, EXIT_ZONE, pos);
}

//-------------------------------------------------------------------------------------------------
/// The lair's thread. Everything that touches the lair's objects or the players in
/// it happens here.
void
CCerberusLair::ProcLair(CZoneTHREAD* pZone) {
    const unsigned long now = ::GetTickCount();
    if (!Reached(now, m_dwLairTick + TICK_MS))
        return;
    m_dwLairTick = now;

    long long hourKey;
    int minute, second;
    LocalClock(hourKey, minute, second);

    std::vector<classUSER*> users;
    std::vector<CObjCHAR*> mobs;
    std::vector<CObjITEM*> items;
    bool bBossAlive = false, bWardenAlive = false;
    CObjCHAR* pAwake = nullptr;
    CObjCHAR* pSleeper = nullptr;
    pZone->ForEachObject([&](CGameOBJ* pObj) {
        if (pObj->IsUSER()) {
            users.push_back((classUSER*)pObj);
        } else if (pObj->IsA(OBJ_MOB)) {
            CObjCHAR* pMob = (CObjCHAR*)pObj;
            if (pMob->Get_HP() > 0) {
                mobs.push_back(pMob);
                const int n = pMob->Get_CharNO();
                if (n == CERBERUS_AWAKE || n == CERBERUS_ASLEEP)
                    bBossAlive = true;
                if (n == CERBERUS_AWAKE)
                    pAwake = pMob;
                if (n == CERBERUS_ASLEEP)
                    pSleeper = pMob;
                if (n == WARDEN)
                    bWardenAlive = true;
            }
        } else if (pObj->IsITEM()) {
            items.push_back((CObjITEM*)pObj);
        }
    });

    std::lock_guard<std::mutex> lock(m_Mutex);

    if (m_bReqReset) {
        m_bReqReset = false;
        m_Registered.clear();
        m_nGateValue = GATE_CLOSED;
        this->EndRun(pZone, users, mobs, items);
        LOG_INFO("[cerberus] reset by a GM");
        return;
    }

    // Anyone here who is not part of a run (a login inside, a stale relay) goes out;
    // GMs may watch.
    const bool bSweep = Reached(now, m_dwLastSweep + SWEEP_MS);
    if (bSweep)
        m_dwLastSweep = now;

    if (m_State == State::Idle || m_State == State::Open) {
        if (bSweep) {
            for (classUSER* pUSER: users) {
                if (!IsGM(pUSER)) {
                    Whisper(pUSER, "The lair is sealed. You are sent back to the temple.");
                    this->Evict(pUSER);
                }
            }
        }
        // Between runs the lair stays empty: a run spawns its own monsters.
        if (!mobs.empty() && Reached(now, m_dwLastSpawn + RESPAWN_MS)) {
            m_dwLastSpawn = now;
            for (CObjCHAR* pMob: mobs)
                pMob->Add_DAMAGE(pMob->Get_HP() + 1);
        }
    }

    switch (m_State) {
        case State::Idle:
            if ((minute < OPEN_MINUTES && hourKey != m_LastOpenedHour) || m_bReqOpen) {
                const bool bGM = m_bReqOpen;
                m_bReqOpen = false;
                m_bReqDraw = false; // a draw asked for while idle is not this window's
                m_LastOpenedHour = hourKey;
                m_Registered.clear();
                m_State = State::Open;
                m_nGateValue = GATE_OPEN;
                m_dwOpenUntil = now
                    + (bGM ? GM_OPEN_MS
                           : (unsigned long)((OPEN_MINUTES - minute) * 60 - second) * 1000);
                Announce(fmt::format(
                    "The seal beneath the Arumic temple thins. Cerberus stirs! Speak to me at the "
                    "temple in Arumic Valley within {} minutes: five of those who sign will face "
                    "it (level {}+).",
                    (m_dwOpenUntil - now + 59999) / 60000,
                    MIN_LEVEL));
                LOG_INFO("[cerberus] registration open{}", bGM ? " (GM)" : "");
            }
            break;

        case State::Open:
            if (Reached(now, m_dwOpenUntil) || m_bReqDraw) {
                m_bReqDraw = false;
                this->Draw(pZone);
            }
            break;

        case State::Run: {
            size_t nInside = 0;
            for (classUSER* pUSER: users) {
                Entry* pEntry = this->FindRoster(pUSER->m_dwDBID);
                if (!pEntry) {
                    if (bSweep && !IsGM(pUSER)) {
                        Whisper(pUSER, "This fight is not yours. You are sent back to the temple.");
                        this->Evict(pUSER);
                    }
                    continue;
                }
                if (!pEntry->arrived) {
                    pEntry->arrived = true;
                    if (pUSER->GetCur_MONEY() < FEE) {
                        Whisper(pUSER,
                            fmt::format("You can no longer pay the {} zuly. The seal casts you out.", FEE));
                        this->Evict(pUSER);
                        LOG_INFO("[cerberus] {} could not pay on arrival", pUSER->Get_NAME());
                        continue;
                    }
                    pUSER->Add_MoneyNSend(-FEE);
                    Whisper(pUSER,
                        fmt::format("{} zuly taken. Cerberus sleeps in the crater to the east, "
                                    "behind the Warden of the Seal. You have {} minutes.",
                            FEE,
                            RUN_LIMIT_MS / 60000));
                    LOG_INFO("[cerberus] {} arrived and paid", pUSER->Get_NAME());
                }
                nInside++;
            }

            // The way in, once whatever the last run left is gone.
            if (!m_bRunSpawned && Reached(now, m_dwLastSpawn + RESPAWN_MS)) {
                m_dwLastSpawn = now;
                if (mobs.empty()) {
                    for (const SpawnAt& at: RUN_SPAWNS)
                        pZone->RegenCharacter(at.x, at.y, at.range, at.npc, at.count, TEAMNO_MOB, true);
                    // Cerberus sleeps in full view from the start, behind the ice wall.
                    // On the neutral team until the seal breaks: allied with everyone,
                    // so nobody can hit it (a Hawk Shot reaches the crater from the
                    // ridge, and a hit wakes it) and its "enemies near" wake finds
                    // nothing. The seal break swaps it for the hostile sleeper.
                    pZone->RegenCharacter(CRATER_X, CRATER_Y, 1, CERBERUS_ASLEEP, 1, TEAMNO_NPC, true);
                    m_bRunSpawned = true;
                    LOG_INFO("[cerberus] whelps, Warden and the sleeping Cerberus spawned");
                } else {
                    for (CObjCHAR* pMob: mobs)
                        pMob->Add_DAMAGE(pMob->Get_HP() + 1);
                }
            }

            // The Warden's death breaks the seal: Cerberus appears in the crater. A
            // kill counts only once each has been seen alive.
            if (m_bRunSpawned && !m_bWardenSeen) {
                if (bWardenAlive) {
                    m_bWardenSeen = true;
                } else if (Reached(now, m_dwLastSpawn + RESPAWN_MS)) {
                    m_dwLastSpawn = now; // its spawn failed: try again
                    pZone->RegenCharacter(RUN_SPAWNS[2].x, RUN_SPAWNS[2].y, 1, WARDEN, 1, TEAMNO_MOB, true);
                }
            } else if (m_bWardenSeen && !bWardenAlive && !m_bSealBroken) {
                m_bSealBroken = true;
                m_bBossSeen = false; // counts the hostile Cerberus only, from here on
                m_dwLastSpawn = now;
                this->SetWall(pZone, false);
                // The neutral sleeper goes silently (the remove packet, no death) and
                // the hostile one takes its place: the GM "npc DEL" pattern.
                if (pSleeper) {
                    pZone->Sub_DIRECT(pSleeper);
                    delete pSleeper;
                    pSleeper = nullptr;
                }
                pZone->RegenCharacter(CRATER_X, CRATER_Y, 1, CERBERUS_ASLEEP, 1, TEAMNO_MOB, true);
                g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT(LAIR_ZONE,
                    (char*)"The Warden falls and the seal breaks. The ice gives way: Cerberus can be reached.",
                    (char*)SPEAKER);
                LOG_INFO("[cerberus] Warden killed, seal broken, Cerberus hostile");
            }
            // The sleeper is kept in the crater in both phases; a kill counts only once
            // the seal is broken and the hostile Cerberus has been seen alive.
            const bool bHostileBoss = pAwake || (pSleeper && pSleeper->Get_TeamNO() == TEAMNO_MOB);
            if (m_bSealBroken && bHostileBoss)
                m_bBossSeen = true;
            if (m_bRunSpawned && !bBossAlive && (!m_bSealBroken || !m_bBossSeen)
                && Reached(now, m_dwLastSpawn + RESPAWN_MS)) {
                m_dwLastSpawn = now; // gone before it could count (a GM kill, a failed spawn)
                pZone->RegenCharacter(CRATER_X, CRATER_Y, 1, CERBERUS_ASLEEP, 1,
                    m_bSealBroken ? TEAMNO_MOB : TEAMNO_NPC, true);
            }

            // The hounds, once per threshold.
            if (pAwake && m_nHoundCalls < (int)(sizeof(HOUND_CALL_PCT) / sizeof(int))) {
                const int nMaxHP = pAwake->Get_MaxHP();
                if (nMaxHP > 0
                    && (long long)pAwake->Get_HP() * 100 <= (long long)nMaxHP * HOUND_CALL_PCT[m_nHoundCalls]) {
                    m_nHoundCalls++;
                    pZone->RegenCharacter(
                        CRATER_X, CRATER_Y, HOUND_SPREAD, HELLHOUND, HOUNDS_PER_CALL, TEAMNO_MOB, true);
                    g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT(LAIR_ZONE,
                        (char*)"Cerberus howls, and hellhounds answer from the dark!",
                        (char*)"Cerberus");
                    LOG_INFO("[cerberus] hounds called ({})", m_nHoundCalls);
                }
            }

            if (m_bSealBroken && m_bBossSeen && !bBossAlive) {
                m_State = State::Grace;
                m_dwGraceUntil = now + GRACE_MS;
                std::vector<classUSER*> winners;
                for (classUSER* pUSER: users) {
                    if (this->FindRoster(pUSER->m_dwDBID))
                        winners.push_back(pUSER);
                }
                Announce(winners.empty()
                        ? std::string("Cerberus has fallen!")
                        : fmt::format("Cerberus has fallen to {}!", Names(winners)));
                g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT(LAIR_ZONE,
                    (char*)"The seal closes in one minute. Gather what the beast left behind.",
                    (char*)SPEAKER);
                LOG_INFO("[cerberus] Cerberus killed");
            } else if (Reached(now, m_dwRunStart + RUN_LIMIT_MS)) {
                Announce("Cerberus still stands. The seal closes and casts the challengers out.");
                LOG_INFO("[cerberus] run timed out");
                this->EndRun(pZone, users, mobs, items);
            } else if (nInside == 0 && Reached(now, m_dwRunStart + ARRIVAL_MS)) {
                LOG_INFO("[cerberus] nobody left in the lair, ending the run");
                this->EndRun(pZone, users, mobs, items);
            }
            break;
        }

        case State::Grace:
            if (bSweep) {
                for (classUSER* pUSER: users) {
                    if (!this->FindRoster(pUSER->m_dwDBID) && !IsGM(pUSER))
                        this->Evict(pUSER);
                }
            }
            if (Reached(now, m_dwGraceUntil))
                this->EndRun(pZone, users, mobs, items);
            break;
    }
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex, from the lair's thread. Only reads the drawn players and sends
/// them packets; their money is taken when they arrive, in the lair's thread.
void
CCerberusLair::Draw(CZoneTHREAD* pZone) {
    m_nGateValue = GATE_CLOSED;
    m_State = State::Idle;

    std::vector<classUSER*> pool;
    for (const Entry& e: m_Registered) {
        classUSER* pUSER = g_pUserLIST->Find_CHAR((char*)e.name.c_str());
        if (!pUSER || pUSER->m_dwDBID != e.dbid || !pUSER->GetZONE())
            continue;
        if (pUSER->Get_HP() <= 0) {
            Whisper(pUSER, "The draw passed you by: the fallen cannot answer the call.");
            continue;
        }
        if (pUSER->Get_LEVEL() < MIN_LEVEL || pUSER->GetCur_MONEY() < FEE) {
            Whisper(pUSER,
                fmt::format("The draw passed you by: it takes level {} and {} zuly.", MIN_LEVEL, FEE));
            continue;
        }
        pool.push_back(pUSER);
    }
    m_Registered.clear();

    static std::mt19937 s_Rng(std::random_device{}() ^ ::GetTickCount());
    std::shuffle(pool.begin(), pool.end(), s_Rng);
    const size_t nDrawn = (std::min)(pool.size(), MAX_DRAWN);
    std::vector<classUSER*> chosen(pool.begin(), pool.begin() + nDrawn);
    for (size_t i = nDrawn; i < pool.size(); i++)
        Whisper(pool[i], "You were not chosen this time. The seal thins again next hour.");

    if (chosen.empty()) {
        Announce("The seal closes. Nobody answered the call this hour.");
        LOG_INFO("[cerberus] draw: nobody registered");
        return;
    }

    // One party for the drawn, out of whatever parties they were in.
    for (classUSER* pUSER: chosen) {
        if (pUSER->GetPARTY())
            pUSER->m_pPartyBUFF->Sub_PartyUSER(pUSER->m_nPartyPOS);
    }
    if (chosen.size() >= 2 && g_pPartyBUFF->CreatePARTY(chosen[0])) {
        CParty* pParty = chosen[0]->m_pPartyBUFF;
        pParty->Raise_PartyLEV(PARTY_LEVEL);
        chosen[0]->Send_gsv_PARTY_REPLY(0, PARTY_REPLY_ACCEPT_MAKE);
        for (size_t i = 1; i < chosen.size(); i++) {
            if (BYTE btFailed = pParty->Add_PartyUSER(chosen[i]))
                LOG_WARN("[cerberus] could not add {} to the party ({})", chosen[i]->Get_NAME(), btFailed);
        }
        pParty->SendPartyLEVnEXP(nullptr, 0);
    }

    tPOINTF start = g_pZoneLIST->GetZONE(LAIR_ZONE)->Get_StartPOS();
    m_Roster.clear();
    for (classUSER* pUSER: chosen) {
        m_Roster.push_back({pUSER->m_dwDBID, pUSER->Get_NAME(), false});
        Whisper(pUSER,
            fmt::format("You are chosen! The seal opens beneath you. {} zuly is taken when you "
                        "arrive.",
                FEE));
        pUSER->Send_gsv_RELAY_REQ(RELAY_TYPE_RECALL, LAIR_ZONE, start);
    }
    m_State = State::Run;
    m_bBossSeen = false;
    m_bRunSpawned = false;
    m_bWardenSeen = false;
    m_bSealBroken = false;
    m_nHoundCalls = 0;
    // Up before anyone arrives: the join path sends it to each arrival.
    this->SetWall(pZone, true);
    m_dwRunStart = ::GetTickCount();
    Announce(fmt::format("{} descend{} into the Cerberus Lair.",
        Names(chosen),
        chosen.size() == 1 ? "s" : ""));
    LOG_INFO("[cerberus] draw: {} of {} chosen", chosen.size(), pool.size());
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex, from the lair's thread: everyone out, the lair reset.
void
CCerberusLair::SetWall(CZoneTHREAD* pZone, bool bUp) {
    if (m_bWallUp == bUp)
        return;
    m_bWallUp = bUp;
    pZone->SetZoneObjects(WALL_GROUP, ICE_WALL, ICE_WALL_COUNT, bUp);
    LOG_INFO("[cerberus] ice wall {}", bUp ? "up" : "down");
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex, from the lair's thread: everyone out, the lair reset.
void
CCerberusLair::EndRun(CZoneTHREAD* pZone,
    const std::vector<classUSER*>& users,
    const std::vector<CObjCHAR*>& mobs,
    const std::vector<CObjITEM*>& items) {
    this->SetWall(pZone, false);
    for (classUSER* pUSER: users) {
        if (this->FindRoster(pUSER->m_dwDBID) || !IsGM(pUSER)) {
            Whisper(pUSER, "The seal closes. You are sent back to the temple.");
            this->Evict(pUSER);
        }
    }
    // A suicide, as AI action 23 does it: no killer, so no drop, no EXP.
    for (CObjCHAR* pMob: mobs)
        pMob->Add_DAMAGE(pMob->Get_HP() + 1);
    for (CObjITEM* pItem: items)
        pItem->m_iRemainTIME = 0;
    m_Roster.clear();
    m_State = State::Idle;
    m_dwLastSpawn = ::GetTickCount();
    m_dwLastSweep = ::GetTickCount(); // give the relays time before sweeping again
}

//-------------------------------------------------------------------------------------------------
std::string
CCerberusLair::GmStatus() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const unsigned long now = ::GetTickCount();
    std::string s = fmt::format("Cerberus lair: {}, gate value {}, {} registered",
        StateName(m_State),
        m_nGateValue,
        m_Registered.size());
    if (m_State == State::Open)
        s += fmt::format(", draw in {} s", (long)(m_dwOpenUntil - now) / 1000);
    if (m_State == State::Run)
        s += fmt::format(", run {} s old, {}",
            (now - m_dwRunStart) / 1000,
            m_bBossSeen        ? "Cerberus up"
                : m_bSealBroken ? "seal broken"
                : m_bWardenSeen ? "Warden up"
                : m_bRunSpawned ? "spawning"
                                : "waiting to spawn");
    s += m_bWallUp ? ", ice wall up" : ", ice wall down";
    for (const Entry& e: m_Roster)
        s += fmt::format(" | {}{}", e.name, e.arrived ? "" : " (on the way)");
    return s;
}

/// The wall on its own, for testing: any state, any thread (the zone registry and
/// the broadcast are thread-safe).
std::string
CCerberusLair::GmWall(bool bUp) {
    CZoneTHREAD* pZone = g_pZoneLIST->GetZONE(LAIR_ZONE);
    if (!pZone)
        return "Cerberus: the lair zone is not loaded.";
    std::lock_guard<std::mutex> lock(m_Mutex);
    this->SetWall(pZone, bUp);
    return bUp ? "Cerberus: ice wall up." : "Cerberus: ice wall down (fading).";
}

/// A GM request is taken only in the state it applies to. It used to be latched:
/// an "open" typed during a run fired the moment the run ended (seen 2026-10-08).
std::string
CCerberusLair::GmOpen() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != State::Idle)
        return fmt::format("Cerberus: cannot open now, the lair is {}.", StateName(m_State));
    m_bReqOpen = true;
    return "Cerberus: registration opens for 2 minutes.";
}

std::string
CCerberusLair::GmDraw() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != State::Open)
        return fmt::format("Cerberus: cannot draw now, the lair is {}.", StateName(m_State));
    m_bReqDraw = true;
    return "Cerberus: drawing now.";
}

bool
CCerberusLair::IsLair(short nZoneNO) {
    return nZoneNO == LAIR_ZONE;
}

void
CCerberusLair::GmReset() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_bReqReset = true;
}
