#include "stdafx.h"

#include "catacombs.h"

#include <algorithm>
#include <cmath>
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
        if (!mobs.empty() || !items.empty() || !users.empty())
            LOG_INFO("[catacomb] {} cleared: {} monsters, {} items, {} players inside",
                CryptName(nZoneNO),
                mobs.size(),
                items.size(),
                users.size());
    }

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
    return s;
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
