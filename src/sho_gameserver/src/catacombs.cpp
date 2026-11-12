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
#include "nav/nav_grid.h"
#include "nav/nav_path.h"

#include "rose/network/packets/combat_generated.h"

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
// The pool (scripts/catacomb_boss.py, catacomb_pool.py). A body is in the daily draw
// once its fight is written here; the others exist only for /mon until then.
enum { BOSS_NECROMANCER, BOSS_ARGENTIA, BOSS_KINDS };
struct BossBody {
    int npc;
    const char* key; // `/catacomb boss <key>`
    const char* name;
    const char* timer; // the return countdown's label
};
constexpr BossBody BOSSES[BOSS_KINDS] = {
    {2556, "necromancer", "The Crypt Necromancer", "The Necromancer returns in"},
    {2558, "argentia", "Argentia, the Mad Giant", "Argentia returns in"},
};

int
BossKindOf(int iNpc) {
    for (int k = 0; k < BOSS_KINDS; k++) {
        if (BOSSES[k].npc == iNpc)
            return k;
    }
    return -1;
}

// ---- Argentia (LIST_NPC 2558): it never walks, and its fight is the floor
// (scripts/catacomb_argentia.py writes these rows; keep the two in step).
constexpr short ARG_SKILL_FLOOR = 7207; // Shattered Floor: a tile erupting
constexpr short ARG_SKILL_GRASP = 7208; // Grasp of the Mad God: an arm coming down
constexpr short ARG_SKILL_HOWL = 7209; // Howl of Madness: the ring
constexpr short ARG_SKILL_SEED = 7210; // Seed of Madness: the circle under a player
constexpr short ARG_ANIM_HOWL = 7211, ARG_MOTION_HOWL = 8; // its 4 s channel: nothing but the clips
constexpr short ARG_ANIM_SLAM = 7212, ARG_MOTION_SLAM = 6; // its slam: the arms land 2.5 s in
constexpr short FX_TILE = 2902; // KCAT_TILE_ERUPT.eft: rock over a whole tile
constexpr short FX_ROCKS = 3002; // EARTHSPLIT_ROCKS.eft: one cluster
constexpr short FX_SHOCKWAVE = 400;
// FILE_SOUND rows: what is heard as a mechanic is drawn, and as it lands
constexpr short SND_FLOOR_WARN = 1002, SND_FLOOR_FIRE = 1551; // casting02, point_heavy01
constexpr short SND_GRASP_WARN = 1005, SND_GRASP_FIRE = 147; // casting06, HeavyAtt_001
constexpr short SND_HOWL_WARN = 1003, SND_HOWL_FIRE = 1569; // casting04, point_thunder01
constexpr short SND_SEED_FIRE = 1551;
constexpr int TILES = 4; // the room is 4 x 4 tiles of TILE_CM (the maze's cells)
constexpr float TILE_CM = 1500.f;
constexpr int ARG_GRASP_CM = 2800, ARG_GRASP_DEG = 50; // each of three arms, 120 degrees apart
constexpr int ARG_HOWL_CM = 1400, ARG_HOWL_FAR_CM = 4300; // inside it, then outside it
constexpr int ARG_SEED_CM = 450;
constexpr unsigned long ARG_GAP_MS[] = {11000, 10000, 8500}; // between mechanics, by waves called
constexpr unsigned long ARM_WAIT_MS = 2500; // the animation's cast did not start: go on without it
constexpr float HAZARD_MARGIN = 40.f; // cm inside a shape's edge that do not count: in the player's favour
enum { ARG_FLOOR = 1, ARG_GRASP, ARG_HOWL };
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

// ---- the maze's walls (scripts/catacomb_art.py: object 1 of each crypt's decoration
// table is its wall, a 1 m model placed at 15x)
constexpr WORD WALL_OBJECT = 1;
constexpr WORD WALL_SCALE_PCT = 1500;
constexpr BYTE WALL_GROUP = 10; // zone object groups 10..13
constexpr int WALL_GROUPS = 4; // 50 walls each; a maze has about 65, never more than 81
constexpr int WALLS_PER_GROUP = 50;

/// A crypt's router: its maze's doorways (Nav::Router, asked by chases and walks).
class MazeRouter: public Nav::Router {
public:
    const CatacombMaze::Maze* pMaze = nullptr;
    bool Route(float x0, float y0, float x1, float y1, std::vector<tPOINTF>& out) override {
        return pMaze && CatacombMaze::Route(*pMaze, x0, y0, x1, y1, out);
    }
};
MazeRouter s_Router[2];

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

/// Whether a point stands in a warning's shape, HAZARD_MARGIN inside its edge.
bool
InShape(const tagZONE_WARN& s, const tPOINTF& pos) {
    const float dx = pos.x - s.m_fX, dy = pos.y - s.m_fY;
    const float fDist = std::sqrt(dx * dx + dy * dy);
    const float a = s.m_nDirDeg * 3.14159265f / 180.f;
    const float ux = std::cos(a), uy = std::sin(a);
    switch (s.m_btShape) {
        case ZONE_WARN_CIRCLE:
            return fDist <= s.m_wLength - HAZARD_MARGIN && (!s.m_wInner || fDist >= s.m_wInner + HAZARD_MARGIN);
        case ZONE_WARN_STRIP: {
            const float fAlong = dx * ux + dy * uy, fAcross = std::fabs(-dx * uy + dy * ux);
            return fAlong >= HAZARD_MARGIN && fAlong <= s.m_wLength - HAZARD_MARGIN
                   && fAcross <= s.m_wWidth * 0.5f - HAZARD_MARGIN;
        }
        case ZONE_WARN_CONE: {
            if (fDist > s.m_wLength - HAZARD_MARGIN)
                return false;
            if (fDist < 1.f)
                return true;
            const float fCos = (dx * ux + dy * uy) / fDist;
            return fCos >= std::cos((s.m_wWidth * 0.5f - 2.f) * 3.14159265f / 180.f);
        }
    }
    return false;
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
    m_iForceKind(-1),
    m_iReqSeed{0, 0},
    m_ullReqSeed{0, 0},
    m_dwCemeteryTick(0),
    m_llAnnounced(-1),
    m_EnterHash(::StrToHashKey(ENTER_TRIGGER)) {
    // the C++ generator against the Python one's test vectors: a day's maze must be the
    // one `catacomb_maze.py` prints for it
    std::string report;
    if (CatacombMaze::SelfTest(report))
        LOG_ERROR("[catacomb] maze generator self-test FAILED:{}", report);
    else
        LOG_INFO("[catacomb] maze generator self-test: {}", report);
}

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
    else if (nZoneNO == CRYPT_ZONES[0] || nZoneNO == CRYPT_ZONES[1]) {
        const int iSlot = nZoneNO == CRYPT_ZONES[0] ? 0 : 1;
        this->ProcFloor(pZone, iSlot); // every frame: a dodge window is two seconds
        this->ProcCrypt(pZone, iSlot);
    }
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

    // The maze: the day's own, and from the start of the day's last break (23:50)
    // tomorrow's, so the walls change while nobody is inside. Before the clearing
    // below: what respawns after it re-rolls against the new walls.
    {
        const long long llMazeDay =
            n.state == State::Break && n.opening == OPENINGS_PER_DAY - 1 ? (long long)n.day + 1 : n.day;
        Crypt& c = m_Crypt[iSlot];
        const int iReq = m_iReqSeed[iSlot];
        m_iReqSeed[iSlot] = 0;
        if (iReq == 1) {
            c.bForced = true;
            this->BuildMaze(pZone, iSlot, m_ullReqSeed[iSlot], llMazeDay, mobs);
        } else if (c.llDay != llMazeDay || iReq == 2) {
            c.bForced = false;
            this->BuildMaze(pZone, iSlot, CatacombMaze::SeedFor(nZoneNO, llMazeDay), llMazeDay, mobs);
        }
    }

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

bool
CCatacombs::WallBetween(CObjCHAR* pFrom, CObjCHAR* pTo) {
    constexpr float SLACK = 200.f; // cm
    if (!pFrom || !pTo || pFrom == pTo || !pFrom->GetZONE() || !IsCrypt(pFrom->GetZONE()->Get_ZoneNO()))
        return false;
    float fLX = pTo->m_PosCUR.x, fLY = pTo->m_PosCUR.y;
    if (pFrom->Nav_CanWalkTo(pTo->m_PosCUR.x, pTo->m_PosCUR.y, &fLX, &fLY))
        return false;
    const float dx = pTo->m_PosCUR.x - fLX, dy = pTo->m_PosCUR.y - fLY;
    return dx * dx + dy * dy > SLACK * SLACK;
}

//-------------------------------------------------------------------------------------------------
/// A crypt's maze from a seed, on its own zone thread: the walls on every client, the
/// same walls on the walk grid, the router. Whatever stands in the corridors goes (a wall
/// may land on it) and the spawn points start over; the boss room never changes, so the
/// boss stays.
void
CCatacombs::BuildMaze(CZoneTHREAD* pZone,
    int iSlot,
    unsigned long long ullSeed,
    long long llDay,
    const std::vector<CObjCHAR*>& mobs) {
    Crypt& c = m_Crypt[iSlot];
    CatacombMaze::Generate(ullSeed, c.maze);
    c.ullSeed = ullSeed;
    c.llDay = llDay;

    // The old walls down, the new ones up. A client that sees both takes the second as
    // "up again mid-fade" and rebuilds the group at once (client zoneobjects.cpp).
    std::vector<CatacombMaze::Wall> walls;
    CatacombMaze::Walls(c.maze, walls);
    c.nWalls = (int)walls.size();
    std::vector<tagZONE_OBJECT> objs;
    for (int g = 0; g < WALL_GROUPS; g++) {
        pZone->SetZoneObjects((BYTE)(WALL_GROUP + g), nullptr, 0, false);
        objs.clear();
        for (int k = g * WALLS_PER_GROUP; k < (g + 1) * WALLS_PER_GROUP && k < (int)walls.size(); k++) {
            tagZONE_OBJECT o;
            o.m_wObjID = WALL_OBJECT;
            o.m_btKind = 0;
            o.m_fX = walls[k].x;
            o.m_fY = walls[k].y;
            o.m_fZ = 0.f;
            o.m_wRotDeg = (WORD)walls[k].rotDeg;
            o.m_wScaleX = o.m_wScaleY = o.m_wScaleZ = WALL_SCALE_PCT;
            objs.push_back(o);
        }
        if (!objs.empty())
            pZone->SetZoneObjects((BYTE)(WALL_GROUP + g), objs.data(), (int)objs.size(), true);
    }
    if ((int)walls.size() > WALL_GROUPS * WALLS_PER_GROUP)
        LOG_ERROR("[catacomb] {}: {} walls do not fit {} groups", CryptName(CRYPT_ZONES[iSlot]), walls.size(), WALL_GROUPS);

    std::vector<unsigned char> mask;
    CatacombMaze::BlockMask(c.maze, mask);
    if (pZone->NavGrid())
        pZone->NavGrid()->SetBlockMask(
            CatacombMaze::SW_CM, CatacombMaze::SW_CM, CatacombMaze::MASK_SIDE, CatacombMaze::MASK_SIDE, mask.data());
    else
        LOG_WARN("[catacomb] {}: no walk grid, the maze is walls on screen only", CryptName(CRYPT_ZONES[iSlot]));
    s_Router[iSlot].pMaze = &c.maze;
    pZone->m_pNavRouter = &s_Router[iSlot];

    int nGone = 0;
    for (CObjCHAR* pMob: mobs) {
        if (BossKindOf(pMob->Get_CharNO()) < 0 && !InBossRoom(pMob->m_PosCUR)) {
            pMob->Add_DAMAGE(pMob->Get_HP() + 1);
            nGone++;
        }
    }
    pZone->Reset_REGEN();
    LOG_INFO("[catacomb] {}: the maze of day {}{}: seed {:016X}, hash {:016X}, {} walls, {} monsters removed",
        CryptName(CRYPT_ZONES[iSlot]),
        llDay,
        c.bForced ? " (a GM's seed)" : "",
        ullSeed,
        CatacombMaze::Hash(c.maze),
        walls.size(),
        nGone);
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
        m_Cues[iSlot].clear();
        m_Hazards[iSlot].clear();
        if (pZone->ZoneTimerLeft(TIMER_BOSS) >= 0)
            pZone->SetZoneTimer(TIMER_BOSS, "", 0);
        return;
    }

    CObjCHAR* pBoss = nullptr;
    for (CObjCHAR* pMob: mobs) {
        if (BossKindOf(pMob->Get_CharNO()) >= 0) {
            pBoss = pMob;
            break;
        }
    }
    // the body to raise: a GM's choice, else the day's draw (one for both crypts)
    const int iWantKind =
        m_iForceKind >= 0 ? m_iForceKind : (int)(CatacombMaze::SeedFor(7, n.day) % BOSS_KINDS);

    // A fresh boss in the middle of its room; every counter starts over.
    auto raise = [&]() {
        b = Boss();
        b.llOpening = llOpening;
        b.iKind = iWantKind;
        m_Cues[iSlot].clear();
        m_Hazards[iSlot].clear();
        if (pZone->ZoneTimerLeft(TIMER_BOSS) >= 0)
            pZone->SetZoneTimer(TIMER_BOSS, "", 0);
        if (pZone->RegenCharacter(ROOM_MID, ROOM_MID, 1, BOSSES[iWantKind].npc, 1, TEAMNO_MOB, true)) {
            b.bUp = true;
            b.dwRaisedAt = now;
        } else {
            b.dwRetryAt = now + BOSS_RETRY_MS;
            LOG_WARN("[catacomb] {}: the boss (npc {}) did not spawn", CryptName(nZoneNO), BOSSES[iWantKind].npc);
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
        LOG_INFO("[catacomb] {}: a fresh boss, by GM: {}", CryptName(nZoneNO), BOSSES[iWantKind].name);
        return;
    }
    if (b.llOpening != llOpening) {
        // An opening starts (or the server did, in the middle of one): the boss is up.
        if (pBoss) {
            b = Boss();
            b.llOpening = llOpening;
            b.bUp = true;
            b.dwRaisedAt = now;
            b.iKind = BossKindOf(pBoss->Get_CharNO());
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
                pZone->SetZoneTimer(TIMER_BOSS, BOSSES[b.iKind].timer, (int)(BOSS_RETURN_MS / 1000));
                Say(nZoneNO, SPEAKER, fmt::format("{} has fallen. The far room will not stay empty: 15 minutes.", BOSSES[b.iKind].name));
            } else {
                Say(nZoneNO, SPEAKER, fmt::format("{} has fallen. Nothing rises again before the crypt seals.", BOSSES[b.iKind].name));
            }
            LOG_INFO("[catacomb] {}: the boss was killed{}", CryptName(nZoneNO), b.bReturning ? "" : " (no return)");
        } else if (b.bReturning) {
            const int iLeft = (int)((long)(b.dwReturnAt - now) / 1000);
            const int nSteps = (int)(sizeof(BOSS_RETURN_STEPS_S) / sizeof(int));
            if (Reached(now, b.dwReturnAt)) {
                raise();
                Say(nZoneNO, SPEAKER, fmt::format("{} has risen in the far room.", BOSSES[iWantKind].name));
                LOG_INFO("[catacomb] {}: the boss returned", CryptName(nZoneNO));
            } else if (b.iReturnStep < nSteps && iLeft <= BOSS_RETURN_STEPS_S[b.iReturnStep]) {
                const int iStep = BOSS_RETURN_STEPS_S[b.iReturnStep++];
                Say(nZoneNO,
                    SPEAKER,
                    fmt::format("Something stirs in the far room. It rises in {}.",
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
        b.iKind = BossKindOf(pBoss->Get_CharNO());
        if (bWas && pZone->ZoneTimerLeft(TIMER_BOSS) >= 0)
            pZone->SetZoneTimer(TIMER_BOSS, "", 0);
    }

    b.iObjIdx = pBoss->Get_INDEX();

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
        if (b.iKind == BOSS_NECROMANCER)
            Say(nZoneNO, BOSSES[b.iKind].name, b.iWaves == 1 ? "Rise. There are guests in my crypt." : "Rise, all of you! Bury them here!");
        else
            Say(nZoneNO, SPEAKER, fmt::format("The dead of the crypt come to {}.", BOSSES[b.iKind].name));
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
    if (b.iKind == BOSS_ARGENTIA)
        this->FightArgentia(iSlot, now, pBoss);
    else
        this->FightNecromancer(iSlot, now, pBoss, inRoom);
}

//-------------------------------------------------------------------------------------------------
/// The Necromancer's turn: its two attacks on the clock, each a cast of its own.
void
CCatacombs::FightNecromancer(int iSlot, unsigned long now, CObjCHAR* pBoss, const std::vector<classUSER*>& inRoom) {
    Boss& b = m_Boss[iSlot];
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
/// The floor of a crypt's boss room, every frame of its thread: the cues that are due
/// become warnings on every client, and the warnings whose time is up hit whoever
/// stands in them, as the boss. Nothing fires once the boss is dead or gone.
void
CCatacombs::ProcFloor(CZoneTHREAD* pZone, int iSlot) {
    std::vector<Cue>& cues = m_Cues[iSlot];
    std::vector<Hazard>& hazards = m_Hazards[iSlot];
    Boss& b = m_Boss[iSlot];
    if (cues.empty() && hazards.empty() && !b.iArmed)
        return;
    const unsigned long now = ::GetTickCount();
    // indices are reused: the object must still be that body, here
    CObjCHAR* pBoss = b.iObjIdx ? g_pObjMGR->Get_CharOBJ((WORD)b.iObjIdx, true) : nullptr;
    if (pBoss && (BossKindOf(pBoss->Get_CharNO()) != b.iKind || pBoss->GetZONE() != pZone))
        pBoss = nullptr;
    if (!pBoss) {
        cues.clear();
        hazards.clear();
        b.iArmed = 0;
        return;
    }

    // A mechanic starts with its animation: the floor is timed from the moment the cast
    // begins (an order waits for the swing in progress), or without it after a while.
    if (b.iArmed
        && ((pBoss->m_bCastingSTART && pBoss->Get_ActiveSKILL() == b.nArmedSkill)
            || Reached(now, b.dwArmedAt + ARM_WAIT_MS))) {
        const int iMechanic = b.iArmed;
        b.iArmed = 0;
        this->ArgentiaMechanic(iSlot, iMechanic, now);
    }

    bool bDue = false;
    for (const Hazard& h: hazards)
        bDue = bDue || Reached(now, h.dwFireAt);
    bool bCue = false;
    for (const Cue& c: cues)
        bCue = bCue || Reached(now, c.dwAt);
    if (!bDue && !bCue)
        return;

    std::vector<classUSER*> inRoom;
    pZone->ForEachObject([&](CGameOBJ* pObj) {
        if (pObj->IsUSER() && ((classUSER*)pObj)->Get_HP() > 0 && InBossRoom(((classUSER*)pObj)->m_PosCUR))
            inRoom.push_back((classUSER*)pObj);
    });

    for (size_t i = 0; i < cues.size();) {
        Cue& c = cues[i];
        if (!Reached(now, c.dwAt)) {
            ++i;
            continue;
        }
        if (c.iSeedRadius > 0) {
            c.shapes.clear();
            for (classUSER* pUSER: inRoom) {
                tagZONE_WARN s = {};
                s.m_btShape = ZONE_WARN_CIRCLE;
                s.m_fX = pUSER->m_PosCUR.x;
                s.m_fY = pUSER->m_PosCUR.y;
                s.m_wLength = (WORD)c.iSeedRadius;
                c.shapes.push_back(s);
            }
        }
        if (!c.shapes.empty()) {
            pZone->SendZoneWarn(
                c.shapes.data(), (int)c.shapes.size(), c.wWarnMS, c.nEffect, c.nWarnSound, c.nFireSound);
            for (const tagZONE_WARN& s: c.shapes) {
                Hazard h;
                h.shape = s;
                h.dwFireAt = now + c.wWarnMS;
                h.nSkill = c.nSkill;
                hazards.push_back(h);
            }
        }
        cues.erase(cues.begin() + i);
    }

    for (size_t i = 0; i < hazards.size();) {
        const Hazard h = hazards[i];
        if (!Reached(now, h.dwFireAt)) {
            ++i;
            continue;
        }
        hazards.erase(hazards.begin() + i);
        for (classUSER* pUSER: inRoom) {
            if (pUSER->Get_HP() <= 0 || !InShape(h.shape, pUSER->m_PosCUR))
                continue;
            // as that skill's result: the digit comes with the row's hit effect and sound
            pBoss->Hit_WithSKILL(pUSER, h.nSkill);
        }
    }
}

//-------------------------------------------------------------------------------------------------
/// Argentia's turn, on the half-second tick: one mechanic at a time, in rotation. It
/// orders the animation (a skill row that is nothing but its clips and their effects)
/// and arms the mechanic; ProcFloor lays the floor when the cast starts.
void
CCatacombs::FightArgentia(int iSlot, unsigned long now, CObjCHAR* pBoss) {
    Boss& b = m_Boss[iSlot];
    if (b.iArmed || !m_Cues[iSlot].empty() || !m_Hazards[iSlot].empty())
        return;
    if (!Reached(now, b.dwNextCast) || pBoss->m_IngSTATUS.IsIgnoreSTATUS())
        return;
    const WORD wCmd = pBoss->Get_COMMAND();
    if (wCmd == CMD_SKILL2SELF || wCmd == CMD_SKILL2OBJ || wCmd == CMD_SKILL2POS || pBoss->Get_ActiveSKILL())
        return;
    const int iMechanic = 1 + b.iRotation % 3; // floor, grasp, howl
    const bool bSlam = iMechanic == ARG_GRASP;
    ((CObjMOB*)pBoss)->SetCMD_Skill2SELF(bSlam ? ARG_ANIM_SLAM : ARG_ANIM_HOWL, bSlam ? ARG_MOTION_SLAM : ARG_MOTION_HOWL);
    b.iArmed = iMechanic;
    b.nArmedSkill = bSlam ? ARG_ANIM_SLAM : ARG_ANIM_HOWL;
    b.dwArmedAt = now;
    b.iRotation++;
    b.dwNextCast = now + ARG_GAP_MS[b.iWaves < 2 ? b.iWaves : 2];
}

/// One of Argentia's mechanics as cues from `now` (the moment its animation began). The
/// phase is the number of waves it has called: later ones chain and overlap more.
void
CCatacombs::ArgentiaMechanic(int iSlot, int iMechanic, unsigned long now) {
    std::vector<Cue>& cues = m_Cues[iSlot];
    const int iPhase = m_Boss[iSlot].iWaves;
    auto cue = [&](unsigned long dwAfter, WORD wWarnMS, short nEffect, short nSkill, short nWarnSound,
                   short nFireSound) -> Cue& {
        Cue c;
        c.dwAt = now + dwAfter;
        c.wWarnMS = wWarnMS;
        c.nEffect = nEffect;
        c.nSkill = nSkill;
        c.nWarnSound = nWarnSound;
        c.nFireSound = nFireSound;
        c.iSeedRadius = 0;
        cues.push_back(c);
        return cues.back();
    };
    // a tile: a strip as wide as it is long, from its west edge
    auto tile = [](int a, int t) {
        tagZONE_WARN s = {};
        s.m_btShape = ZONE_WARN_STRIP;
        s.m_fX = ROOM_MIN + a * TILE_CM;
        s.m_fY = ROOM_MIN + (t + 0.5f) * TILE_CM;
        s.m_nDirDeg = 0;
        s.m_wLength = (WORD)TILE_CM;
        s.m_wWidth = (WORD)TILE_CM;
        return s;
    };
    auto tiles = [&](Cue& c, int iPattern, int iArg) {
        for (int t = 0; t < TILES; t++) {
            for (int a = 0; a < TILES; a++) {
                const bool bInner = (a == 1 || a == 2) && (t == 1 || t == 2);
                const bool bCorner = (a == 0 || a == 3) && (t == 0 || t == 3);
                bool bOn = false;
                switch (iPattern) {
                    case 0: bOn = (a + t) % 2 == iArg; break; // a checkerboard
                    case 1: bOn = t == iArg; break; // a row
                    case 2: bOn = a == iArg; break; // a column
                    case 3: bOn = !bCorner; break; // the cross: the corners are safe
                    case 4: bOn = !bInner; break; // the ring: the middle is safe
                }
                if (bOn)
                    c.shapes.push_back(tile(a, t));
            }
        }
    };

    switch (iMechanic) {
        case ARG_FLOOR: {
            const int iPick = iPhase == 0 ? 0 : rand() % 3;
            if (iPick == 0) {
                // one half of the board, then the other: stand on a dark tile, then cross
                const int iFirst = rand() % 2;
                tiles(cue(0, 2600, FX_TILE, ARG_SKILL_FLOOR, SND_FLOOR_WARN, SND_FLOOR_FIRE), 0, iFirst);
                tiles(cue(2000, 2200, FX_TILE, ARG_SKILL_FLOOR, 0, SND_FLOOR_FIRE), 0, 1 - iFirst);
            } else if (iPick == 1) {
                // a wave across the room, row by row or column by column
                const int iAxis = 1 + rand() % 2;
                const bool bBack = rand() % 2 != 0;
                for (int k = 0; k < TILES; k++)
                    tiles(cue(k * 700, 2300, FX_TILE, ARG_SKILL_FLOOR, k == 0 ? SND_FLOOR_WARN : 0, SND_FLOOR_FIRE),
                        iAxis,
                        bBack ? TILES - 1 - k : k);
            } else {
                // everything but the corners, then everything but the middle
                tiles(cue(0, 2600, FX_TILE, ARG_SKILL_FLOOR, SND_FLOOR_WARN, SND_FLOOR_FIRE), 3, 0);
                tiles(cue(2200, 2200, FX_TILE, ARG_SKILL_FLOOR, 0, SND_FLOOR_FIRE), 4, 0);
            }
            break;
        }
        case ARG_GRASP: {
            // three arms a third of a turn apart; later a second set comes down between them
            const int iTurn = rand() % 120;
            for (int iSet = 0; iSet < (iPhase == 0 ? 1 : 2); iSet++) {
                Cue& c = cue(iSet * 1500, iSet == 0 ? 2530 : 2000, 0, ARG_SKILL_GRASP,
                    iSet == 0 ? SND_GRASP_WARN : 0, SND_GRASP_FIRE);
                for (int k = 0; k < 3; k++) {
                    tagZONE_WARN s = {};
                    s.m_btShape = ZONE_WARN_CONE;
                    s.m_fX = ROOM_MID;
                    s.m_fY = ROOM_MID;
                    s.m_nDirDeg = (short)((iTurn + iSet * 60 + k * 120) % 360);
                    s.m_wLength = (WORD)ARG_GRASP_CM;
                    s.m_wWidth = (WORD)ARG_GRASP_DEG;
                    c.shapes.push_back(s);
                }
            }
            break;
        }
        case ARG_HOWL: {
            // close to it, then far from it (the other way round, now and then, at the end)
            const bool bFarFirst = iPhase >= 2 && rand() % 2 != 0;
            for (int k = 0; k < 2; k++) {
                const bool bNear = (k == 0) != bFarFirst;
                tagZONE_WARN s = {};
                s.m_btShape = ZONE_WARN_CIRCLE;
                s.m_fX = ROOM_MID;
                s.m_fY = ROOM_MID;
                s.m_wLength = (WORD)(bNear ? ARG_HOWL_CM : ARG_HOWL_FAR_CM);
                s.m_wInner = (WORD)(bNear ? 0 : ARG_HOWL_CM);
                cue(k * 2200, k == 0 ? 2600 : 2200, FX_SHOCKWAVE, ARG_SKILL_HOWL, k == 0 ? SND_HOWL_WARN : 0,
                    SND_HOWL_FIRE)
                    .shapes.push_back(s);
            }
            break;
        }
    }
    // from the first wave on, a seed under every player as well: nobody stands still
    if (iPhase >= 1)
        cue(1000, 2000, FX_ROCKS, ARG_SKILL_SEED, 0, SND_SEED_FIRE).iSeedRadius = ARG_SEED_CM;
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
        s += b.bUp ? fmt::format(" | {} up, {} wave(s) called", BOSSES[b.iKind].name, b.iWaves)
                   : b.bReturning ? fmt::format(" | boss returns in {}", Span((int)((long)(b.dwReturnAt - now) / 1000)))
                                  : std::string(" | boss down for this opening");
    }
    {
        const Crypt& c = m_Crypt[n.crypt == CRYPT_ZONES[0] ? 0 : 1];
        if (c.llDay >= 0)
            s += fmt::format(" | maze of day {}{}: seed {:016X}, hash {:016X}, {} walls",
                c.llDay,
                c.bForced ? " (a GM's seed)" : "",
                c.ullSeed,
                CatacombMaze::Hash(c.maze),
                c.nWalls);
    }
    return s;
}

std::string
CCatacombs::GmSeed(const std::string& arg) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    const int iSlot = n.crypt == CRYPT_ZONES[0] ? 0 : 1;
    if (arg.empty()) {
        m_iReqSeed[iSlot] = 2;
        return fmt::format("{} goes back to the maze of its day within a second.", CryptName(n.crypt));
    }
    char* pEnd = nullptr;
    const unsigned long long ullSeed = ::strtoull(arg.c_str(), &pEnd, 0); // 0x... or decimal
    if (!pEnd || *pEnd)
        return "Usage: catacomb seed <number or 0x hex> (no number: back to the day's own maze)";
    m_iReqSeed[iSlot] = 1;
    m_ullReqSeed[iSlot] = ullSeed;
    return fmt::format("{} is rebuilt from seed {:016X} within a second; `catacomb_maze.py --seed` prints it.",
        CryptName(n.crypt),
        ullSeed);
}

std::string
CCatacombs::GmMaze() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    const Crypt& c = m_Crypt[n.crypt == CRYPT_ZONES[0] ? 0 : 1];
    if (c.llDay < 0)
        return "No maze built yet.";
    return fmt::format("{}, day {}, seed {:016X}, hash {:016X}\n{}",
        CryptName(n.crypt),
        c.llDay,
        c.ullSeed,
        CatacombMaze::Hash(c.maze),
        CatacombMaze::Render(c.maze));
}

std::string
CCatacombs::GmBoss(const std::string& arg) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const Now n = this->Clock();
    if (!arg.empty()) {
        int iKind = arg == "day" ? -1 : BOSS_KINDS;
        for (int k = 0; k < BOSS_KINDS; k++) {
            if (arg == BOSSES[k].key)
                iKind = k;
        }
        if (iKind == BOSS_KINDS) {
            std::string s = "Usage: catacomb boss [day";
            for (int k = 0; k < BOSS_KINDS; k++)
                s += std::string("|") + BOSSES[k].key;
            return s + "]";
        }
        m_iForceKind = iKind;
    }
    if (n.state == State::Break)
        return "The crypt is sealed: no boss until it opens.";
    m_bReqBoss[n.crypt == CRYPT_ZONES[0] ? 0 : 1] = true;
    return fmt::format("{} rises in {} within a second.",
        BOSSES[m_iForceKind >= 0 ? m_iForceKind : (int)(CatacombMaze::SeedFor(7, n.day) % BOSS_KINDS)].name,
        CryptName(n.crypt));
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
