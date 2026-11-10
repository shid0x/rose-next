#include "stdafx.h"

#include "catacombs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "CObjITEM.h"
#include "CObjNPC.h"
#include "GS_ListUSER.h"
#include "GS_ThreadSQL.h"
#include "GS_ThreadZONE.h"
#include "GS_USER.h"
#include "ZoneLIST.h"

namespace {

// Data written by scripts/import-catacombs.py; keep the two in step.
constexpr short CRYPT_ZONES[2] = {90, 91}; // Reminiscence, Sorrow: day number even / odd
constexpr short CEMETERY_ZONE = 87;
/// A keeper, one per Columbarium: his row, his name for the whispers, where he stands
/// and where the crypt casts out a player who went in through him (world cm).
/// `import-catacombs.py --emit` prints this table.
struct Keeper {
    int npc;
    const char* name;
    float x, y;
    float exitX, exitY;
};
constexpr Keeper KEEPERS[] = {
    {4020, "Osric", 480950.f, 479000.f, 481200.f, 478680.f},
    {4021, "Maren", 535650.f, 525510.f, 535400.f, 525830.f},
    {4022, "Aldous", 530880.f, 503340.f, 531200.f, 503590.f},
    {4023, "Edda", 565850.f, 472390.f, 565650.f, 472710.f},
};
constexpr int KEEPER_COUNT = (int)(sizeof(KEEPERS) / sizeof(KEEPERS[0]));
const char* ENTER_TRIGGER = "Catacomb-Enter";
const char* SPEAKER = "The Crypt Keepers"; // the announcements

// The toll, a placeholder (the user, 2026-11-09: "just some Karkia drop as a temp
// thing"): five Black Hearts, LIST_NATURAL 151, dropped in the Cemetery. The keeper's
// dialog names it too (import-catacombs.py DIALOG).
constexpr unsigned int TOLL_TYPE = ITEM_TYPE_NATURAL;
constexpr unsigned int TOLL_NO = 151;
constexpr unsigned int TOLL_COUNT = 5;
const char* TOLL_NAME = "five Black Hearts";

// An opening every two hours, on the even hours: open, warnings, break.
constexpr int PERIOD_S = 2 * 60 * 60;
constexpr int WARNING_AT_S = 100 * 60; // 1 h 40 in: ten minutes left
constexpr int BREAK_AT_S = 110 * 60; // 1 h 50 in: sealed for ten minutes
constexpr int OPENINGS_PER_DAY = 24 * 60 * 60 / PERIOD_S;
constexpr int WARN_STEPS_S[] = {10 * 60, 5 * 60, 60}; // announced inside the crypt

// The countdown shown to those inside (GSV_ZONE_TIMER). Id 2 is kept for the boss's
// return (M2).
constexpr BYTE TIMER_SEAL = 1;
const char* TIMER_SEAL_LABEL = "The crypt seals in";
constexpr int TIMER_DRIFT_S = 2; // re-sent only when the end moved (a GM shifted the clock)

// ---- the boss (scripts/catacomb_boss.py: keep the two in step)
constexpr int BOSS_NPC = 2556; // Crypt Necromancer
const char* BOSS_SPEAKER = "Crypt Necromancer";
constexpr short SKILL_BURST = 7205, MOTION_BURST = 6; // Grave Burst: a 10 m circle round it
constexpr short SKILL_LANCES = 7206, MOTION_LANCES = 8; // Soul Lances: five lines at one player
// A cast ordered on a target out of the skill's range makes the boss walk first, and
// the clients abandon a cast that has not started within 3 s: order only in reach.
constexpr float BURST_REACH = 750.f; // cm; the row's range is 800, the circle 1000
constexpr float LANCES_REACH = 1800.f; // the row's range is 2000, a line 2200
// The boss room: cells 6..9 of the 10 x 10 maze on both axes (catacomb_maze.BOSS), 15 m
// cells from (504500, 504500) (import-catacombs.py MAZE_SW_CM, CELL_CM). World cm.
constexpr float ROOM_MIN = 513500.f, ROOM_MAX = 519500.f, ROOM_MID = 516500.f;
constexpr unsigned long BOSS_RETURN_MS = 15 * 60 * 1000; // after a kill (the user, 2026-11-09)
constexpr int BOSS_RETURN_STEPS_S[] = {5 * 60, 60}; // announced inside
constexpr unsigned long BOSS_RESET_MS = 60 * 1000; // the room empty this long: a fresh boss
constexpr unsigned long BOSS_GRACE_MS = 3 * 1000; // a new one is not in the zone's lists at once
constexpr unsigned long BOSS_RETRY_MS = 10 * 1000;
constexpr float BOSS_HOME = 500.f; // cm from the middle: it has not left its spot
// The attack clock. One attack every CAST_GAP_MS[waves called]; before the first wave
// they alternate, after it they come as a pair: the lances, and the burst as soon as
// the lances have landed.
constexpr unsigned long FIRST_CAST_MS = 5 * 1000; // into a fight
constexpr unsigned long CAST_GAP_MS[] = {9 * 1000, 11 * 1000, 9 * 1000};
constexpr unsigned long CAST_RETRY_MS = 1000; // nobody in reach
// The dead it calls, at these shares of its HP: {row, count}, crypt by crypt
// (scripts/catacomb_monsters.py: 2550 Victim, 2551 Ghoul, 2552 Wolf, 2554 Officer).
constexpr int WAVE_PCT[] = {60, 30};
constexpr int WAVE_COUNT = (int)(sizeof(WAVE_PCT) / sizeof(int));
struct Add {
    int npc;
    int count;
};
constexpr Add WAVES[2][2][3] = {
    {{{2551, 2}, {2550, 1}, {0, 0}}, {{2551, 2}, {2550, 1}, {2554, 1}}}, // Reminiscence
    {{{2552, 2}, {2551, 1}, {0, 0}}, {{2552, 2}, {2551, 1}, {2554, 1}}}, // Sorrow
};
constexpr int ADD_SPREAD = 700; // cm round the boss
constexpr BYTE TIMER_BOSS = 2;
const char* TIMER_BOSS_LABEL = "The Necromancer returns in";

constexpr short KEEPER_SEALED = 0;
constexpr short KEEPER_OPEN = 1; // the trigger's COND_011 and the dialog's --open-when

constexpr unsigned long TICK_MS = 500;
constexpr unsigned long SWEEP_MS = 3 * 1000; // gives a relay time before the next one
constexpr float TALK_RANGE = 2000.f; // cm from the keeper: further is not a dialog click

/// a - b >= 0 on a wrapping millisecond clock
bool
Reached(unsigned long now, unsigned long when) {
    return (long)(now - when) >= 0;
}

bool
InBossRoom(const tPOINTF& pos) {
    return pos.x >= ROOM_MIN && pos.x <= ROOM_MAX && pos.y >= ROOM_MIN && pos.y <= ROOM_MAX;
}

float
Distance(const tPOINTF& a, const tPOINTF& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

void
Say(short nZoneNO, const char* szSpeaker, const std::string& msg) {
    g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT(nZoneNO, (char*)msg.c_str(), (char*)szSpeaker);
}

void
Whisper(classUSER* pUSER, int iKeeper, const std::string& msg) {
    pUSER->Send_gsv_WHISPER((char*)KEEPERS[iKeeper].name, (char*)msg.c_str());
}

std::string
Span(int iSeconds) {
    if (iSeconds >= 3600)
        return fmt::format("{} h {} min", iSeconds / 3600, (iSeconds % 3600) / 60);
    if (iSeconds >= 60)
        return fmt::format("{} min {} s", iSeconds / 60, iSeconds % 60);
    return fmt::format("{} s", iSeconds);
}

/// The toll out of the bag, across stacks; false (and nothing taken) when it is not all
/// there. The player's own thread.
bool
TakeToll(classUSER* pUSER) {
    tagITEM toll;
    toll.Init(TOLL_TYPE, TOLL_NO, 1u);
    unsigned int nHeld = 0;
    for (short nI = MAX_EQUIP_IDX; nI < INVENTORY_TOTAL_SIZE; nI++) {
        tagITEM& item = pUSER->m_Inventory.m_ItemLIST[nI];
        if (item.GetHEADER() == toll.GetHEADER())
            nHeld += item.GetQuantity();
    }
    if (nHeld < TOLL_COUNT)
        return false;
    unsigned int nLeft = TOLL_COUNT;
    for (short nI = MAX_EQUIP_IDX; nI < INVENTORY_TOTAL_SIZE && nLeft > 0; nI++) {
        tagITEM& item = pUSER->m_Inventory.m_ItemLIST[nI];
        if (item.GetHEADER() != toll.GetHEADER())
            continue;
        tagITEM sub = item;
        sub.m_uiQuantity = (std::min)(nLeft, item.GetQuantity());
        nLeft -= sub.m_uiQuantity;
        pUSER->Sub_ItemNSend(nI, sub);
    }
    return true;
}

} // namespace

CCatacombs&
CCatacombs::Instance() {
    static CCatacombs s_Catacombs;
    return s_Catacombs;
}

CCatacombs::CCatacombs():
    m_llSkew(0),
    m_dwCryptTick{0, 0},
    m_dwCryptSweep{0, 0},
    m_llCleaned{-1, -1},
    m_llWarned{-1, -1},
    m_bReqBoss{false, false},
    m_dwCemeteryTick(0),
    m_llAnnounced(-1),
    m_EnterHash(::StrToHashKey(ENTER_TRIGGER)) {}

bool
CCatacombs::IsEnterTrigger(unsigned long hash) const {
    return hash == m_EnterHash;
}

bool
CCatacombs::IsCrypt(short nZoneNO) {
    return nZoneNO == CRYPT_ZONES[0] || nZoneNO == CRYPT_ZONES[1];
}

const char*
CCatacombs::StateName(State s) {
    switch (s) {
        case State::Open:
            return "open";
        case State::Warning:
            return "open, sealing soon";
        case State::Break:
            return "sealed";
    }
    return "?";
}

const char*
CCatacombs::CryptName(short nZoneNO) {
    return nZoneNO == CRYPT_ZONES[0] ? "Catacombs of Reminiscence" : "Catacombs of Sorrow";
}

//-------------------------------------------------------------------------------------------------
/// Under m_Mutex. The server's local time, read back as a plain count of seconds so
/// that a day is the local calendar day and the openings fall on local even hours.
CCatacombs::Now
CCatacombs::Clock() const {
    time_t t = time(nullptr) + (time_t)m_llSkew;
    tm lt;
    localtime_s(&lt, &t);
    const long long llLocal = (long long)_mkgmtime(&lt);

    Now n;
    n.day = (int)(llLocal / 86400);
    const int iSecondOfDay = (int)(llLocal % 86400);
    n.opening = iSecondOfDay / PERIOD_S;
    n.phase = iSecondOfDay % PERIOD_S;
    n.state = n.phase < WARNING_AT_S ? State::Open : (n.phase < BREAK_AT_S ? State::Warning : State::Break);
    n.crypt = CRYPT_ZONES[n.day % 2];
    n.toSeal = (std::max)(0, BREAK_AT_S - n.phase);
    n.toOpen = n.state == State::Break ? PERIOD_S - n.phase : 0;
    return n;
}

void
CCatacombs::OnLoaded(unsigned long dbid, int iDay, int iOpening) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    // Memory wins while the server runs: a payment's write may still be queued behind
    // this very read (a relog seconds after paying).
    if (iDay >= 0 && m_Records.find(dbid) == m_Records.end())
        m_Records[dbid] = {iDay, iOpening, 0};
}

void
CCatacombs::Save(unsigned long dbid, const Record& r) {
    if (g_pThreadSQL)
        g_pThreadSQL->Add_CatacombSave(dbid, r.day, r.opening);
}

void
CCatacombs::Evict(classUSER* pUSER, int iEntrance) {
    const Keeper& k = KEEPERS[iEntrance >= 0 && iEntrance < KEEPER_COUNT ? iEntrance : 0];
    tPOINTF pos;
    pos.x = k.exitX;
    pos.y = k.exitY;
    pUSER->Send_gsv_RELAY_REQ(RELAY_TYPE_RECALL, CEMETERY_ZONE, pos);
}

void
CCatacombs::ProcZone(CZoneTHREAD* pZone) {
    const short nZoneNO = pZone->Get_ZoneNO();
    if (nZoneNO == CEMETERY_ZONE)
        this->ProcCemetery(pZone);
    else if (nZoneNO == CRYPT_ZONES[0])
        this->ProcCrypt(pZone, 0);
    else if (nZoneNO == CRYPT_ZONES[1])
        this->ProcCrypt(pZone, 1);
}

//-------------------------------------------------------------------------------------------------
/// A crypt's own thread: who may stand here, the warnings, and the clearing of a crypt
/// that is not open.
void
CCatacombs::ProcCrypt(CZoneTHREAD* pZone, int iSlot) {
    const unsigned long now = ::GetTickCount();
    if (!Reached(now, m_dwCryptTick[iSlot] + TICK_MS))
        return;
    m_dwCryptTick[iSlot] = now;

    std::vector<classUSER*> users;
    std::vector<CObjCHAR*> mobs;
    std::vector<CObjITEM*> items;
    pZone->ForEachObject([&](CGameOBJ* pObj) {
        if (pObj->IsUSER()) {
            users.push_back((classUSER*)pObj);
        } else if (pObj->IsA(OBJ_MOB)) {
            if (((CObjCHAR*)pObj)->Get_HP() > 0)
                mobs.push_back((CObjCHAR*)pObj);
        } else if (pObj->IsITEM()) {
            items.push_back((CObjITEM*)pObj);
        }
    });

    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    const short nZoneNO = CRYPT_ZONES[iSlot];
    const bool bOpenHere = n.crypt == nZoneNO && n.state != State::Break;
    const long long llOpening = (long long)n.day * OPENINGS_PER_DAY + n.opening;

    // A crypt that is not open is cleared once per opening: at its own break, and the
    // other day's crypt whenever the clock moves on. A suicide, as AI action 23 does it:
    // no killer, so no drop and no EXP.
    if (!bOpenHere && m_llCleaned[iSlot] != llOpening) {
        m_llCleaned[iSlot] = llOpening;
        for (CObjCHAR* pMob: mobs)
            pMob->Add_DAMAGE(pMob->Get_HP() + 1);
        for (CObjITEM* pItem: items)
            pItem->m_iRemainTIME = 0;
        // The spawn points start over: nothing counted alive, no escalation carried
        // into the next opening. They refill a sealed crypt at their own pace, so
        // an opening starts with its packs standing.
        pZone->Reset_REGEN();
        if (!mobs.empty() || !items.empty() || !users.empty())
            LOG_INFO("[catacomb] {} cleared: {} monsters, {} items, {} players inside",
                CryptName(nZoneNO),
                mobs.size(),
                items.size(),
                users.size());
    }

    this->ProcBoss(pZone, iSlot, now, n, bOpenHere, llOpening, users, mobs);

    // The seal's countdown on screen: set once per opening, and again when the end
    // moves. The clients count down by themselves; an arrival gets what is left.
    {
        const int iShown = pZone->ZoneTimerLeft(TIMER_SEAL);
        if (bOpenHere) {
            if (iShown < 0 || abs(iShown - n.toSeal) > TIMER_DRIFT_S)
                pZone->SetZoneTimer(TIMER_SEAL, TIMER_SEAL_LABEL, n.toSeal);
        } else if (iShown >= 0) {
            pZone->SetZoneTimer(TIMER_SEAL, "", 0);
        }
    }

    // The warnings, to those inside.
    if (bOpenHere && n.state == State::Warning) {
        int iStep = 0;
        for (int iI = 0; iI < (int)(sizeof(WARN_STEPS_S) / sizeof(int)); iI++) {
            if (n.toSeal <= WARN_STEPS_S[iI])
                iStep = iI + 1;
        }
        const long long llKey = llOpening * 4 + iStep;
        if (iStep > 0 && m_llWarned[iSlot] < llKey) {
            m_llWarned[iSlot] = llKey;
            const int iLeft = WARN_STEPS_S[iStep - 1];
            g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT(nZoneNO,
                (char*)fmt::format("The crypt seals in {}. Those still inside will be cast out.",
                    iLeft >= 120 ? fmt::format("{} minutes", iLeft / 60) : std::string("one minute"))
                    .c_str(),
                (char*)SPEAKER);
        }
    }

    // Who may stand here: those registered for this opening, in the crypt of the day,
    // while it is open. Anyone else (a login after the opening ended, a stale relay, a
    // teleport) goes back to the crypt mouth. GMs too (the user, 2026-11-10): a GM tests
    // the crypt as a player does, and `/catacomb pass` gives one a fresh registration.
    if (!Reached(now, m_dwCryptSweep[iSlot] + SWEEP_MS))
        return;
    m_dwCryptSweep[iSlot] = now;
    for (classUSER* pUSER: users) {
        auto it = m_Records.find(pUSER->m_dwDBID);
        const bool bRegistered =
            it != m_Records.end() && it->second.day == n.day && it->second.opening == n.opening;
        if (bOpenHere && bRegistered)
            continue;
        // out through the entrance they came in by (the first, for one who has no record)
        const int iEntrance = it != m_Records.end() ? it->second.entrance : 0;
        Whisper(pUSER,
            iEntrance,
            bOpenHere ? "You are not on the list for this opening. Speak to me at the crypt mouth."
                      : "The crypt is sealed. You are cast out to the crypt mouth.");
        this->Evict(pUSER, iEntrance);
    }
}

//-------------------------------------------------------------------------------------------------
/// The boss of an open crypt, from its own zone thread (the caller holds m_Mutex and has
/// collected the living monsters and the players): raise it, notice its death, bring it
/// back, run its fight.
void
CCatacombs::ProcBoss(CZoneTHREAD* pZone,
    int iSlot,
    unsigned long now,
    const Now& n,
    bool bOpenHere,
    long long llOpening,
    const std::vector<classUSER*>& users,
    const std::vector<CObjCHAR*>& mobs) {
    Boss& b = m_Boss[iSlot];
    const short nZoneNO = CRYPT_ZONES[iSlot];

    if (!bOpenHere) {
        // sealed: the clearing took the boss with everything else
        b = Boss();
        m_bReqBoss[iSlot] = false;
        if (pZone->ZoneTimerLeft(TIMER_BOSS) >= 0)
            pZone->SetZoneTimer(TIMER_BOSS, "", 0);
        return;
    }

    CObjCHAR* pBoss = nullptr;
    for (CObjCHAR* pMob: mobs) {
        if (pMob->Get_CharNO() == BOSS_NPC) {
            pBoss = pMob;
            break;
        }
    }

    // A fresh boss in the middle of its room; every counter starts over.
    auto raise = [&]() {
        b = Boss();
        b.llOpening = llOpening;
        if (pZone->ZoneTimerLeft(TIMER_BOSS) >= 0)
            pZone->SetZoneTimer(TIMER_BOSS, "", 0);
        if (pZone->RegenCharacter(ROOM_MID, ROOM_MID, 1, BOSS_NPC, 1, TEAMNO_MOB, true)) {
            b.bUp = true;
            b.dwRaisedAt = now;
        } else {
            b.dwRetryAt = now + BOSS_RETRY_MS;
            LOG_WARN("[catacomb] {}: the boss (npc {}) did not spawn", CryptName(nZoneNO), BOSS_NPC);
        }
    };
    // The boss out with no death and no drop, and whatever stands in its room with it.
    auto remove = [&]() {
        for (CObjCHAR* pMob: mobs) {
            if (pMob != pBoss && InBossRoom(pMob->m_PosCUR))
                pMob->Add_DAMAGE(pMob->Get_HP() + 1);
        }
        if (pBoss) {
            pZone->Sub_DIRECT(pBoss);
            delete pBoss;
            pBoss = nullptr;
        }
    };

    if (m_bReqBoss[iSlot]) {
        m_bReqBoss[iSlot] = false;
        remove();
        raise();
        LOG_INFO("[catacomb] {}: a fresh boss, by GM", CryptName(nZoneNO));
        return;
    }
    if (b.llOpening != llOpening) {
        // An opening starts (or the server did, in the middle of one): the boss is up.
        if (pBoss) {
            b = Boss();
            b.llOpening = llOpening;
            b.bUp = true;
            b.dwRaisedAt = now;
        } else {
            raise();
        }
        return;
    }

    if (!pBoss) {
        if (b.bUp) {
            if (!Reached(now, b.dwRaisedAt + BOSS_GRACE_MS))
                return;
            // Killed. It rises again in 15 minutes if the opening lasts that long.
            b.bUp = false;
            if ((long long)n.toSeal * 1000 > (long long)BOSS_RETURN_MS) {
                b.bReturning = true;
                b.dwReturnAt = now + BOSS_RETURN_MS;
                b.iReturnStep = 0;
                pZone->SetZoneTimer(TIMER_BOSS, TIMER_BOSS_LABEL, (int)(BOSS_RETURN_MS / 1000));
                Say(nZoneNO, SPEAKER, "The Crypt Necromancer has fallen. It will rise again in 15 minutes.");
            } else {
                Say(nZoneNO, SPEAKER, "The Crypt Necromancer has fallen. It will not rise again before the crypt seals.");
            }
            LOG_INFO("[catacomb] {}: the boss was killed{}", CryptName(nZoneNO), b.bReturning ? "" : " (no return)");
        } else if (b.bReturning) {
            const int iLeft = (int)((long)(b.dwReturnAt - now) / 1000);
            const int nSteps = (int)(sizeof(BOSS_RETURN_STEPS_S) / sizeof(int));
            if (Reached(now, b.dwReturnAt)) {
                raise();
                Say(nZoneNO, SPEAKER, "The Crypt Necromancer has risen again in the far room.");
                LOG_INFO("[catacomb] {}: the boss returned", CryptName(nZoneNO));
            } else if (b.iReturnStep < nSteps && iLeft <= BOSS_RETURN_STEPS_S[b.iReturnStep]) {
                const int iStep = BOSS_RETURN_STEPS_S[b.iReturnStep++];
                Say(nZoneNO,
                    SPEAKER,
                    fmt::format("Something stirs in the far room. The Necromancer rises in {}.",
                        iStep >= 120 ? fmt::format("{} minutes", iStep / 60) : std::string("one minute")));
            }
        } else if (b.dwRetryAt && Reached(now, b.dwRetryAt)) {
            raise();
        }
        return;
    }
    if (!b.bUp) {
        // one that did not come from here (a GM's /mon): it is the boss now
        const bool bWas = b.bReturning;
        b = Boss();
        b.llOpening = llOpening;
        b.bUp = true;
        b.dwRaisedAt = now;
        if (bWas && pZone->ZoneTimerLeft(TIMER_BOSS) >= 0)
            pZone->SetZoneTimer(TIMER_BOSS, "", 0);
    }

    // ---- the fight
    std::vector<classUSER*> inRoom;
    for (classUSER* pUSER: users) {
        if (pUSER->Get_HP() > 0 && InBossRoom(pUSER->m_PosCUR))
            inRoom.push_back(pUSER);
    }
    const int nMaxHP = pBoss->Get_MaxHP();
    const bool bHurt = pBoss->Get_HP() < nMaxHP;
    if (inRoom.empty()) {
        // Nobody left in the room (a wipe, a retreat). After a minute a boss that was
        // hurt, has called its dead or has wandered off is replaced by a fresh one.
        tPOINTF mid;
        mid.x = ROOM_MID;
        mid.y = ROOM_MID;
        if (!b.dwEmptySince) {
            b.dwEmptySince = now ? now : 1;
        } else if (Reached(now, b.dwEmptySince + BOSS_RESET_MS)
                   && (bHurt || b.iWaves > 0 || Distance(pBoss->m_PosCUR, mid) > BOSS_HOME)) {
            remove();
            raise();
            LOG_INFO("[catacomb] {}: the boss room stood empty, a fresh boss", CryptName(nZoneNO));
            return;
        }
        b.bEngaged = false;
        b.bPair = false;
        return;
    }
    b.dwEmptySince = 0;

    // The dead it calls, once per threshold.
    if (b.iWaves < WAVE_COUNT && nMaxHP > 0
        && (long long)pBoss->Get_HP() * 100 <= (long long)nMaxHP * WAVE_PCT[b.iWaves]) {
        for (const Add& a: WAVES[iSlot][b.iWaves]) {
            if (a.npc)
                pZone->RegenCharacter(
                    pBoss->m_PosCUR.x, pBoss->m_PosCUR.y, ADD_SPREAD, a.npc, a.count, TEAMNO_MOB, true);
        }
        b.iWaves++;
        Say(nZoneNO, BOSS_SPEAKER, b.iWaves == 1 ? "Rise. There are guests in my crypt." : "Rise, all of you! Bury them here!");
        LOG_INFO("[catacomb] {}: the boss called wave {}", CryptName(nZoneNO), b.iWaves);
    }

    // Its attacks, on the clock.
    if (!bHurt && pBoss->Get_TargetIDX() == 0) {
        b.bEngaged = false;
        b.bPair = false;
        return;
    }
    if (!b.bEngaged) {
        b.bEngaged = true;
        b.bPair = false;
        b.dwNextCast = now + FIRST_CAST_MS;
    }
    const WORD wCmd = pBoss->Get_COMMAND();
    if (wCmd == CMD_SKILL2SELF || wCmd == CMD_SKILL2OBJ || wCmd == CMD_SKILL2POS || pBoss->Get_ActiveSKILL())
        return; // one at a time: the pair's second waits for the first to end
    if (pBoss->m_IngSTATUS.IsIgnoreSTATUS())
        return; // stunned: the order would be dropped
    if (!b.bPair && !Reached(now, b.dwNextCast))
        return;

    CObjMOB* pCaster = (CObjMOB*)pBoss;
    auto burst = [&]() {
        classUSER* pNear = nullptr;
        float fNear = BURST_REACH;
        for (classUSER* pUSER: inRoom) {
            const float fDist = Distance(pUSER->m_PosCUR, pBoss->m_PosCUR);
            if (fDist <= fNear) {
                fNear = fDist;
                pNear = pUSER;
            }
        }
        return pNear && pCaster->SetCMD_Skill2OBJ(pNear->Get_INDEX(), SKILL_BURST, MOTION_BURST);
    };
    auto lances = [&]() {
        // at anyone in reach, not only at whoever it is hitting: everybody watches the floor
        std::vector<classUSER*> reach;
        for (classUSER* pUSER: inRoom) {
            if (Distance(pUSER->m_PosCUR, pBoss->m_PosCUR) <= LANCES_REACH)
                reach.push_back(pUSER);
        }
        return !reach.empty()
               && pCaster->SetCMD_Skill2OBJ(reach[rand() % reach.size()]->Get_INDEX(), SKILL_LANCES, MOTION_LANCES);
    };

    if (b.bPair) {
        b.bPair = false;
        burst();
        return;
    }
    bool bCast;
    if (b.iWaves == 0) {
        bCast = (b.iRotation % 2 == 0) ? (lances() || burst()) : (burst() || lances());
    } else {
        bCast = lances();
        if (bCast)
            b.bPair = true;
        else
            bCast = burst();
    }
    if (bCast) {
        b.iRotation++;
        b.dwNextCast = now + CAST_GAP_MS[b.iWaves];
    } else {
        b.dwNextCast = now + CAST_RETRY_MS;
    }
}

//-------------------------------------------------------------------------------------------------
/// The Cemetery's thread: every keeper's event value 0 follows the clock (Set_ObjVAR(0)
/// sends GSV_SET_EVENT_STATUS on a change, which the dialog of the keeper talked to
/// reads), and the Cemetery hears the crypt open and seal.
void
CCatacombs::ProcCemetery(CZoneTHREAD* pZone) {
    const unsigned long now = ::GetTickCount();
    if (!Reached(now, m_dwCemeteryTick + TICK_MS))
        return;
    m_dwCemeteryTick = now;

    Now n;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        n = this->Clock();
    }
    const bool bSealed = n.state == State::Break;
    const short nWant = bSealed ? KEEPER_SEALED : KEEPER_OPEN;
    for (const Keeper& k: KEEPERS) {
        CObjNPC* pNPC = g_pZoneLIST->Get_LocalNPC(k.npc);
        if (pNPC && pNPC->GetZONE() == pZone && pNPC->Get_ObjVAR(0) != nWant)
            pNPC->Set_ObjVAR(0, nWant);
    }

    const long long llKey = ((long long)n.day * OPENINGS_PER_DAY + n.opening) * 2 + (bSealed ? 1 : 0);
    if (m_llAnnounced != llKey) {
        const bool bFirst = m_llAnnounced < 0; // the server just started: say nothing
        m_llAnnounced = llKey;
        if (!bFirst) {
            const std::string msg = bSealed
                ? fmt::format("The crypt is sealed. It opens again in {}.", Span(n.toOpen))
                : fmt::format("The crypt is open: the {}. It seals in {}.", CryptName(n.crypt), Span(n.toSeal));
            g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT(CEMETERY_ZONE, (char*)msg.c_str(), (char*)SPEAKER);
        }
        LOG_INFO("[catacomb] {}: day {}, opening {}, {}",
            bSealed ? "sealed" : "open",
            n.day,
            n.opening,
            CryptName(n.crypt));
    }
}

//-------------------------------------------------------------------------------------------------
/// The player's own thread (the Cemetery's), after the trigger found the keeper open.
/// Registered for this opening: in, free. Paid today for an earlier opening: refused,
/// nothing taken. Otherwise the toll is taken, the record written, and in.
void
CCatacombs::OnEnter(classUSER* pUSER) {
    CZoneTHREAD* pHere = pUSER->GetZONE();
    if (!pHere || pHere->Get_ZoneNO() != CEMETERY_ZONE || pUSER->Get_HP() <= 0)
        return;
    // The keeper talked to is the nearest one; none in reach is not a click on a keeper
    // but a trigger sent from elsewhere.
    int iKeeper = -1;
    float fNearest = TALK_RANGE;
    for (int iI = 0; iI < KEEPER_COUNT; iI++) {
        const float fDX = pUSER->Get_CurXPOS() - KEEPERS[iI].x, fDY = pUSER->Get_CurYPOS() - KEEPERS[iI].y;
        const float fDist = std::sqrt(fDX * fDX + fDY * fDY);
        if (fDist <= fNearest) {
            fNearest = fDist;
            iKeeper = iI;
        }
    }
    if (iKeeper < 0)
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    if (n.state == State::Break) {
        Whisper(pUSER, iKeeper, fmt::format("The crypt is sealed. It opens again in {}.", Span(n.toOpen)));
        return;
    }
    CZoneTHREAD* pCrypt = g_pZoneLIST->GetZONE(n.crypt);
    if (!pCrypt) {
        Whisper(pUSER, iKeeper, "The door will not open today.");
        LOG_WARN("[catacomb] zone {} is not loaded", n.crypt);
        return;
    }

    const unsigned long dbid = pUSER->m_dwDBID;
    auto it = m_Records.find(dbid);
    const bool bThisOpening = it != m_Records.end() && it->second.day == n.day && it->second.opening == n.opening;
    if (bThisOpening) {
        it->second.entrance = iKeeper;
        Whisper(pUSER, iKeeper, fmt::format("Back again. The crypt seals in {}.", Span(n.toSeal)));
    } else {
        if (it != m_Records.end() && it->second.day == n.day) {
            Whisper(pUSER,
                iKeeper,
                "You have already entered the catacombs today, and your opening has ended. Come "
                "back tomorrow.");
            return;
        }
        if (!TakeToll(pUSER)) {
            Whisper(pUSER,
                iKeeper,
                fmt::format("The price is {}, once a day. You do not carry it. The dead of this "
                            "cemetery do.",
                    TOLL_NAME));
            return;
        }
        const Record rec = {n.day, n.opening, iKeeper};
        m_Records[dbid] = rec;
        this->Save(dbid, rec);
        Whisper(pUSER,
            iKeeper,
            fmt::format("The price is paid. The {} are yours until the crypt seals, in {}. Leave "
                        "and return as you like until then.",
                CryptName(n.crypt),
                Span(n.toSeal)));
        LOG_INFO("[catacomb] {} paid: day {}, opening {}, {}", pUSER->Get_NAME(), n.day, n.opening, CryptName(n.crypt));
    }
    tPOINTF pos = pCrypt->Get_StartPOS();
    pUSER->Send_gsv_RELAY_REQ(RELAY_TYPE_RECALL, n.crypt, pos);
}

//-------------------------------------------------------------------------------------------------
std::string
CCatacombs::GmStatus() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    size_t nRegistered = 0, nToday = 0;
    for (const auto& kv: m_Records) {
        if (kv.second.day == n.day) {
            nToday++;
            if (kv.second.opening == n.opening)
                nRegistered++;
        }
    }
    std::string s = fmt::format("Catacombs: {}, {} (zone {}), day {}, opening {} of {}",
        StateName(n.state),
        CryptName(n.crypt),
        n.crypt,
        n.day,
        n.opening + 1,
        OPENINGS_PER_DAY);
    s += n.state == State::Break ? fmt::format(", opens in {}", Span(n.toOpen))
                                 : fmt::format(", seals in {}", Span(n.toSeal));
    s += fmt::format(" | {} registered for this opening, {} paid today", nRegistered, nToday);
    if (m_llSkew)
        s += fmt::format(" | clock shifted by {} (`catacomb clock` to undo)", Span((int)m_llSkew));
    if (n.state != State::Break) {
        const Boss& b = m_Boss[n.crypt == CRYPT_ZONES[0] ? 0 : 1];
        const unsigned long now = ::GetTickCount();
        s += b.bUp ? fmt::format(" | boss up, {} wave(s) called", b.iWaves)
                   : b.bReturning ? fmt::format(" | boss returns in {}", Span((int)((long)(b.dwReturnAt - now) / 1000)))
                                  : std::string(" | boss down for this opening");
    }
    return s;
}

std::string
CCatacombs::GmBoss() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    if (n.state == State::Break)
        return "The crypt is sealed: no boss until it opens.";
    m_bReqBoss[n.crypt == CRYPT_ZONES[0] ? 0 : 1] = true;
    return fmt::format("A fresh boss rises in {} within a second.", CryptName(n.crypt));
}

std::string
CCatacombs::GmClose() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    if (n.state == State::Break)
        return "Catacombs: already sealed.";
    if (n.toSeal > 60)
        m_llSkew += n.toSeal - 60;
    return "Catacombs: the crypt seals in one minute.";
}

std::string
CCatacombs::GmOpen() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    m_llSkew += PERIOD_S - n.phase;
    return "Catacombs: the next opening starts now (earlier registrations are spent).";
}

std::string
CCatacombs::GmDaily() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_llSkew += 86400;
    return "Catacombs: one day later. The other crypt, and every pass is spent.";
}

std::string
CCatacombs::GmClock() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_llSkew = 0;
    return "Catacombs: back on the real clock.";
}

std::string
CCatacombs::GmPass(const std::string& name) {
    classUSER* pUSER = g_pUserLIST->Find_CHAR((char*)name.c_str());
    if (!pUSER)
        return fmt::format("Catacombs: {} is not online.", name);
    std::lock_guard<std::mutex> lock(m_Mutex);
    const unsigned long dbid = pUSER->m_dwDBID;
    m_Records.erase(dbid);
    if (g_pThreadSQL)
        g_pThreadSQL->Add_CatacombSave(dbid, -1, 0);
    return fmt::format("Catacombs: {} may register again.", pUSER->Get_NAME());
}
