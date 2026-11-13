#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "catacomb_maze.h"

class CObjCHAR;
class CZoneTHREAD;
class classUSER;

/// The Karkia catacombs (doc/karkia-catacombs-implementation-brief.md, data in
/// scripts/import-catacombs.py): two crypt zones, Reminiscence (90) and Sorrow (91),
/// one open per day, reached through the keepers in the Desolate Cemetery (87): one
/// in front of each of its four Columbariums, all leading to the crypt of the day.
///
/// The clock. An opening starts on every even hour, server local time: 1 h 40 open,
/// 10 min of warnings, then a 10 min break during which the keeper refuses and
/// everyone inside is sent back to the crypt mouth. The crypt of the day alternates
/// with the calendar date. All of it is computed from the wall clock at any moment,
/// so a server started mid-opening simply finds it open; nothing is latched on an
/// edge. GM commands shift that clock (a skew), so tests run the real code.
///
/// Registration. The toll is payable once a day and buys the opening it is paid in,
/// with free re-entry until that opening ends. A character's record is (day,
/// opening); it lives in the database (table catacomb_entry, written at the payment
/// by the SQL thread, read at login) and in m_Records while the server runs. Anyone
/// in a crypt zone whose record is not the current opening's is sent out, GMs
/// included.
///
/// The maze (catacomb_maze.*, the generator scripts/catacomb_maze.py mirrors). Each crypt
/// has the maze of its day, a function of the crypt and the date alone: the map files hold
/// only the outer wall, the pillars and the rooms. The crypt's thread builds it at the
/// first tick and again when the day's last break starts (23:50), for the day to come:
/// the walls as zone objects (GSV_ZONE_OBJECTS, in groups of 50), the same walls as a
/// block mask on the zone's walk grid, and a router that walks a monster from doorway
/// to doorway, since a way across the maze is longer than the path search looks.
///
/// The boss (scripts/catacomb_boss.py). The crypt of the day has one in its far room,
/// and the controller owns it, not a regen point: it raises it when an opening starts
/// and 15 minutes after each kill (a countdown on screen, zone timer 2), orders its
/// two attacks on a clock -- the pace must not depend on how many players hit it --
/// calls the dead to it at two HP thresholds, and replaces it with a fresh one when
/// the room has stood empty for a minute (monsters do not heal). Its AI file only
/// finds a target and answers a hit.
///
/// The pool. The far room's boss is one of several bodies (BOSSES in catacombs.cpp),
/// drawn by the day and shared by both crypts; a GM can ask for one by name. A body
/// joins the draw when its fight is written.
///
/// The floor. A fight that is more than one monster's casts is run from here: the
/// controller draws warning shapes on the room's floor (CZoneTHREAD::SendZoneWarn) and,
/// when a shape's time is up, hits whoever still stands in it, as the boss, by one of
/// its skill rows (ProcFloor, every frame of the crypt's thread: a dodge window is two
/// seconds, so half-second ticks are too coarse). Argentia, who never walks, is built
/// on it: the room's sixteen tiles erupt in patterns, its arms come down in three
/// cones, its howl is safe only close to it and then only far from it. Ferdinando, who
/// walks and fights with a sword, is the same layer from wherever he stands: lines of
/// cuts in a fan, a zig-zag of dashes he really makes (CObjCHAR::Blink_POSITION as each
/// line fires), cuts opening under every player, a ring of blades that is safe only
/// against him, a rising cut in front, and after each wave of the dead his Severance:
/// lines through the players while a storm builds round him. Gladiomurus's shapes stay: a
/// hazard may linger after it fires and go on hitting (her walls of swords), a cue may
/// aim itself when it comes due (a thrust from her at one player, her leap onto the
/// farthest), and her moves are dodged in a room her walls have just cut up. The
/// Assassin's floor says where to be: a soak shape (drawn green) must have somebody in
/// it as it lands -- those in it take its hit, and an empty one hits the whole room --
/// and his Execution is one such mark whose blow is shared by whoever stands in it.
///
/// Threads. ProcZone runs in every zone thread and acts in three:
///   * each crypt zone's thread sweeps its own players and, once per break, kills
///     its monsters and removes its ground items;
///   * the Cemetery's thread keeps every keeper's event value 0 (1 = open), which
///     the enter trigger (COND_011) and the dialog read, and makes the announcements.
/// OnEnter runs in the thread of the player who talked to the keeper (the
/// Cemetery's). OnLoaded runs in the SQL thread. One mutex covers the records and
/// the skew.
class CCatacombs {
public:
    static CCatacombs& Instance();

    /// Called by every zone thread once per frame (CZoneTHREAD::Execute).
    void ProcZone(CZoneTHREAD* pZone);

    /// `Catacomb-Enter` passed for this player (classUSER::Do_QuestTRIGGER): the
    /// trigger only checks that the keeper is open. The toll, the once-a-day rule
    /// and the warp are here.
    bool IsEnterTrigger(unsigned long hash) const;
    void OnEnter(classUSER* pUSER);

    /// The SQL thread read a character's record at login (day < 0: none).
    void OnLoaded(unsigned long dbid, int iDay, int iOpening);

    /// A crypt zone: for callers that must treat both alike.
    static bool IsCrypt(short nZoneNO);

    /// Whether a maze wall stands between two characters of a crypt: pFrom could not walk
    /// straight to pTo (the walk grid's walls are the only walls the server knows). Someone
    /// against a wall stands on a cell the grid calls blocked, so a line that ends within
    /// 2 m of pTo counts as clear; a wall is 3.5 m thick. False outside the crypts. The
    /// one rule behind what a crypt monster may notice (CZoneTHREAD::FindInBand) and cast
    /// at (CObjMOB::SetCMD_Skill2OBJ, CObjCHAR::Nav_Chase).
    static bool WallBetween(CObjCHAR* pFrom, CObjCHAR* pTo);

    /// GM: `/catacomb status|close|open|daily|pass <name>|clock`.
    std::string GmStatus();
    std::string GmClose(); // jump to one minute before the break
    std::string GmOpen(); // jump to the start of the next opening
    std::string GmDaily(); // jump one day ahead: the other crypt, every pass spent
    std::string GmPass(const std::string& name); // clear a character's record
    std::string GmClock(); // drop the skew: back to the real clock
    /// A fresh boss in today's crypt, now. With a name ("necromancer", "argentia") that
    /// body, kept until "day" gives the choice back to the calendar.
    std::string GmBoss(const std::string& arg);
    std::string GmSeed(const std::string& arg); // today's crypt rebuilt from a seed ("" = the day's own)
    std::string GmMaze(); // today's maze as text

private:
    enum class State { Open, Warning, Break };

    /// Where the (skewed) clock stands.
    struct Now {
        int day; // local calendar day number
        int opening; // 0..11 within the day
        int phase; // seconds into the 2-hour period
        State state;
        short crypt; // today's crypt zone
        int toSeal; // seconds until the break (0 in it)
        int toOpen; // seconds until the next opening (0 unless in the break)
    };
    struct Record {
        int day;
        int opening;
        int entrance; // the keeper used last: where the crypt casts the player out (memory only)
    };

    /// One crypt's boss, as its zone thread left it on the last tick.
    struct Boss {
        long long llOpening = -1; // the opening this state belongs to
        bool bUp = false; // raised, and not seen dead since
        unsigned long dwRaisedAt = 0; // it joins the zone's lists a pass later
        bool bReturning = false; // killed: it rises again at dwReturnAt
        unsigned long dwReturnAt = 0;
        int iReturnStep = 0; // return announcements made
        unsigned long dwRetryAt = 0; // a spawn that failed is tried again
        int iWaves = 0; // add waves this boss has called
        bool bEngaged = false;
        unsigned long dwNextCast = 0;
        int iRotation = 0;
        bool bPair = false; // the second attack of a pair is due
        unsigned long dwEmptySince = 0; // 0: someone is in the room
        int iKind = 0; // which body (BOSSES)
        int iObjIdx = 0; // its object, for the per-frame floor (checked against its row)
        int iUlt = 0; // waves answered with the strongest attack (Ferdinando's Severance)
        float fSpotX = 0.f, fSpotY = 0.f; // where a leap lands, for the cues after it (0: nowhere)
        int iPick = 0; // which player a move's first aimed cue takes; the next ones follow round
        unsigned long dwCagedUntil = 0; // Gladiomurus's cage stands until then (0: none)
        // a floor mechanic waiting for its animation's cast to start (0: none)
        int iArmed = 0;
        short nArmedSkill = 0;
        unsigned long dwArmedAt = 0;
    };
    /// A shape on the floor that hits when its time is up, and a batch of them due later.
    struct Hazard {
        tagZONE_WARN shape;
        unsigned long dwFireAt;
        short nSkill; // the boss's skill row the hit is computed and shown with
        bool bBlink = false; // as it fires, the boss is at (fBlinkX, fBlinkY)
        float fBlinkX = 0.f, fBlinkY = 0.f;
        short nFaceDeg = -1; // and faces this way (-1: as it did)
        // lingering: once fired it stands until dwUntil. Looked at every frame: whoever is
        // in it, or has gone from one side of it to the other since the last frame (a
        // runner is through a wall in a quarter of a second), is hit, at most once per
        // dwTickMS each
        unsigned long dwUntil = 0, dwTickMS = 0;
        bool bFired = false;
        std::unordered_map<int, unsigned long> hitAt; // by player object: the last hit
        std::unordered_map<int, float> side; // by player object: cm to one side (+) or the other
        // a soak (a circle): the players in it take nSkill and, with fewer than iSoakNeed of
        // them (never asked for more than are in the room), everybody takes nFailSkill.
        // bSplit: no failure, the blow is shared instead -- each takes the row's damage
        // x (those asked for / iSoakNeed) / (those in it)
        int iSoakNeed = 0;
        short nFailSkill = 0;
        bool bSplit = false;
    };
    struct Cue {
        unsigned long dwAt = 0;
        std::vector<tagZONE_WARN> shapes;
        WORD wWarnMS = 0;
        short nEffect = 0;
        short nSkill = 0;
        short nWarnSound = 0, nFireSound = 0;
        int iSeedRadius = 0; // > 0: a circle of this radius under every player in the room instead
        // > 0: a strip this long and iLineWidth wide through one player of the room, any way round
        int iLineLength = 0, iLineWidth = 0;
        bool bBlink = false; // the boss goes to (fBlinkX, fBlinkY) as the cue's shapes fire
        float fBlinkX = 0.f, fBlinkY = 0.f;
        short nFaceDeg = -1; // facing that way
        unsigned long dwLingerMS = 0, dwTickMS = 0; // its shapes stay this long after firing
        // > 0: a strip this long from the boss at one player (the iThrustPick-th from the
        // move's first), with one more each side iThrustFan degrees off; the boss turns to it
        int iThrustLength = 0, iThrustWidth = 0, iThrustFan = 0, iThrustPick = 0;
        int iDiveRadius = 0; // > 0: a circle on the farthest player, remembered as the boss's spot
        bool bAtSpot = false; // the shapes are centred on that spot
        bool bBlinkToSpot = false; // the boss is at that spot now (a cue with no shape)
        int iSoakNeed = 0; // > 0: its shapes are soaks (see Hazard), sent as ZONE_WARN_SOAK
        short nFailSkill = 0;
        bool bSplit = false;
        // > 0: a circle on one player (the iThrustPick-th from the move's first) and the boss
        // there as it fires; with iEchoLength, two cuts crossed on that spot dwEchoMS later
        int iStrikeRadius = 0, iEchoLength = 0, iEchoWidth = 0;
        unsigned long dwEchoMS = 0;
        int iFanLength = 0, iFanDeg = 0; // > 0: a cone from the boss at every player
    };

    /// One crypt's maze, built and read by its zone thread.
    struct Crypt {
        CatacombMaze::Maze maze;
        long long llDay = -1; // the day it was built for
        unsigned long long ullSeed = 0;
        bool bForced = false; // a GM's seed: kept until the day changes
        int nWalls = 0;
    };

    CCatacombs();

    Now Clock() const; // under m_Mutex
    void ProcCrypt(CZoneTHREAD* pZone, int iSlot);
    void BuildMaze(CZoneTHREAD* pZone,
        int iSlot,
        unsigned long long ullSeed,
        long long llDay,
        const std::vector<CObjCHAR*>& mobs);
    void ProcBoss(CZoneTHREAD* pZone,
        int iSlot,
        unsigned long now,
        const Now& n,
        bool bOpenHere,
        long long llOpening,
        const std::vector<classUSER*>& users,
        const std::vector<CObjCHAR*>& mobs);
    void ProcFloor(CZoneTHREAD* pZone, int iSlot);
    void FightNecromancer(int iSlot, unsigned long now, CObjCHAR* pBoss, const std::vector<classUSER*>& inRoom);
    void FightArgentia(int iSlot, unsigned long now, CObjCHAR* pBoss);
    void ArgentiaMechanic(int iSlot, int iMechanic, unsigned long now);
    bool FloorBusy(int iSlot) const; // a cue or a shape that has not fired yet
    void FightFerdinando(int iSlot, unsigned long now, CObjCHAR* pBoss);
    void FightGladiomurus(int iSlot, unsigned long now, CObjCHAR* pBoss);
    void FightAssassin(int iSlot, unsigned long now, CObjCHAR* pBoss);
    void AssassinMechanic(int iSlot,
        int iMechanic,
        unsigned long now,
        CObjCHAR* pBoss,
        const std::vector<classUSER*>& inRoom);
    void GladiomurusMechanic(int iSlot,
        int iMechanic,
        unsigned long now,
        CObjCHAR* pBoss,
        const std::vector<classUSER*>& inRoom);
    void FerdinandoMechanic(int iSlot,
        int iMechanic,
        unsigned long now,
        CObjCHAR* pBoss,
        const std::vector<classUSER*>& inRoom);
    void ProcCemetery(CZoneTHREAD* pZone);
    void Evict(classUSER* pUSER, int iEntrance);
    void Save(unsigned long dbid, const Record& r);
    static const char* StateName(State s);
    static const char* CryptName(short nZoneNO);

    mutable std::mutex m_Mutex;
    std::unordered_map<unsigned long, Record> m_Records;
    long long m_llSkew; // seconds added to the wall clock (GM)

    // one zone thread each: [0], [1] the crypts', the rest the Cemetery's
    unsigned long m_dwCryptTick[2];
    unsigned long m_dwCryptSweep[2];
    long long m_llCleaned[2]; // the opening (day * 12 + opening) this crypt was last cleared in
    long long m_llWarned[2]; // the last (opening key * 4 + warning step) announced inside
    Boss m_Boss[2];
    bool m_bReqBoss[2]; // GM: a fresh boss at the next tick
    int m_iForceKind; // GM: the body to raise (-1: the day's draw)
    std::vector<Hazard> m_Hazards[2]; // each crypt's own thread only
    std::vector<Cue> m_Cues[2];
    Crypt m_Crypt[2];
    int m_iReqSeed[2]; // GM: 1 = rebuild from m_ullReqSeed, 2 = back to the day's own
    unsigned long long m_ullReqSeed[2];
    unsigned long m_dwCemeteryTick;
    long long m_llAnnounced; // the last (opening key * 2 + sealed) announced at the crypt mouth
    unsigned long m_EnterHash;
};
