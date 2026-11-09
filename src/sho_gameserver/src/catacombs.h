#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CZoneTHREAD;
class classUSER;

/// The Karkia catacombs (doc/karkia-catacombs-implementation-brief.md, data in
/// scripts/import-catacombs.py): two crypt zones, Reminiscence (90) and Sorrow (91),
/// one open per day, reached through the keeper in the Desolate Cemetery (87).
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
/// in a crypt zone whose record is not the current opening's is sent out; GMs may
/// stay.
///
/// Threads. ProcZone runs in every zone thread and acts in three:
///   * each crypt zone's thread sweeps its own players and, once per break, kills
///     its monsters and removes its ground items;
///   * the Cemetery's thread keeps the keeper's event value 0 (1 = open), which the
///     enter trigger (COND_011) and the dialog read, and makes the announcements.
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

    /// GM: `/catacomb status|close|open|daily|pass <name>|clock`.
    std::string GmStatus();
    std::string GmClose(); // jump to one minute before the break
    std::string GmOpen(); // jump to the start of the next opening
    std::string GmDaily(); // jump one day ahead: the other crypt, every pass spent
    std::string GmPass(const std::string& name); // clear a character's record
    std::string GmClock(); // drop the skew: back to the real clock

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
    };

    CCatacombs();

    Now Clock() const; // under m_Mutex
    void ProcCrypt(CZoneTHREAD* pZone, int iSlot);
    void ProcCemetery(CZoneTHREAD* pZone);
    void Evict(classUSER* pUSER);
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
    unsigned long m_dwCemeteryTick;
    long long m_llAnnounced; // the last (opening key * 2 + sealed) announced at the crypt mouth
    unsigned long m_EnterHash;
};
