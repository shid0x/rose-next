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
    bool bBossAlive = false;
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
        this->EndRun(users, mobs, items);
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
        // The sleeper, once the lair is empty of monsters.
        if (!bBossAlive && Reached(now, m_dwLastSpawn + RESPAWN_MS)) {
            m_dwLastSpawn = now;
            if (mobs.empty()) {
                pZone->RegenCharacter(CRATER_X, CRATER_Y, 1, CERBERUS_ASLEEP, 1, TEAMNO_MOB, true);
            } else {
                for (CObjCHAR* pMob: mobs)
                    pMob->Add_DAMAGE(pMob->Get_HP() + 1);
            }
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
                this->Draw();
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
                        fmt::format("{} zuly taken. Cerberus sleeps in the crater to the east. "
                                    "You have {} minutes.",
                            FEE,
                            RUN_LIMIT_MS / 60000));
                    LOG_INFO("[cerberus] {} arrived and paid", pUSER->Get_NAME());
                }
                nInside++;
            }

            // A draw right after a reset can beat the sleeper's respawn: put it in
            // the crater, and count a kill only once Cerberus has been seen alive.
            if (!m_bBossSeen) {
                if (bBossAlive) {
                    m_bBossSeen = true;
                } else if (Reached(now, m_dwLastSpawn + RESPAWN_MS)) {
                    m_dwLastSpawn = now;
                    if (mobs.empty())
                        pZone->RegenCharacter(
                            CRATER_X, CRATER_Y, 1, CERBERUS_ASLEEP, 1, TEAMNO_MOB, true);
                }
            }

            if (m_bBossSeen && !bBossAlive) {
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
                this->EndRun(users, mobs, items);
            } else if (nInside == 0 && Reached(now, m_dwRunStart + ARRIVAL_MS)) {
                LOG_INFO("[cerberus] nobody left in the lair, ending the run");
                this->EndRun(users, mobs, items);
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
                this->EndRun(users, mobs, items);
            break;
    }
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex, from the lair's thread. Only reads the drawn players and sends
/// them packets; their money is taken when they arrive, in the lair's thread.
void
CCerberusLair::Draw() {
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
    m_dwRunStart = ::GetTickCount();
    Announce(fmt::format("{} descend{} into the Cerberus Lair.",
        Names(chosen),
        chosen.size() == 1 ? "s" : ""));
    LOG_INFO("[cerberus] draw: {} of {} chosen", chosen.size(), pool.size());
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex, from the lair's thread: everyone out, the lair reset.
void
CCerberusLair::EndRun(const std::vector<classUSER*>& users,
    const std::vector<CObjCHAR*>& mobs,
    const std::vector<CObjITEM*>& items) {
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
    m_dwLastSpawn = ::GetTickCount(); // the sleeper returns once the bodies are gone
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
        s += fmt::format(", run {} s old", (now - m_dwRunStart) / 1000);
    for (const Entry& e: m_Roster)
        s += fmt::format(" | {}{}", e.name, e.arrived ? "" : " (on the way)");
    return s;
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
