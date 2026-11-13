#pragma once

#include <map>
#include <string>
#include <vector>

class CGameOBJ;

/// Dynamic zone objects: map decorations the server puts up and takes down
/// mid-game (GSV_ZONE_OBJECTS; first use: the Cerberus Lair's ice wall,
/// doc/cerberus-ice-wall.md).
///
/// A piece is an ordinary decoration of the zone's own LIST_DECO ZSC, built
/// through CObjectMANAGER::Add_GndTREE and inserted into the scene directly,
/// never into a terrain chunk: in the scene with the ZSC's collision level, the
/// avatar's body collision stops against it like any map wall. The server
/// enforces nothing (no wall in the game is server-side). A group that goes
/// down fades over FADE_MS, then its pieces are deleted; it keeps blocking
/// while it fades. A zone change deletes every object (CObjectMANAGER::Clear),
/// which tells this to forget them.
class CZoneObjects {
public:
    struct Spec {
        unsigned short wObjID; // kind 0: LIST_DECO object of the current zone
        unsigned char btKind; // 0 = decoration; 1 = invisible collision panel (Add_CollisionBox)
        float fX, fY, fZ; // world cm
        unsigned short wRotDeg; // about Z
        unsigned short wScaleX, wScaleY, wScaleZ; // percent, 100 = 1.0
    };

    static CZoneObjects& Instance();

    /// From Recv_gsv_ZONE_OBJECTS. Up is idempotent (a group that stands is
    /// left alone); down starts the fade.
    void Apply(unsigned char btGroup, bool bOn, const std::vector<Spec>& specs);
    /// Per frame (CObjectMANAGER::ProcOBJECT): the fades.
    void Update();
    /// CObjectMANAGER::Clear deleted every object: drop the bookkeeping.
    void Forget();

private:
    struct Piece {
        short nObjIdx;
        CGameOBJ* pObj; // what the slot held when we made it
    };
    struct Group {
        std::vector<Piece> pieces;
        bool bFading;
        unsigned long dwFadeStart;
    };

    CZoneObjects() {}
    bool Valid(const Piece& piece) const;
    void Destroy(Group& group);

    std::map<unsigned char, Group> m_Groups;
};

/// Zone timers: labelled countdowns the server shows the players of a zone
/// (GSV_ZONE_TIMER; first use: the Karkia catacombs' "the crypt seals in").
///
/// The server sends a timer's seconds left when it sets it and on every zone
/// entry; this counts down from the moment the packet arrived. A zone change
/// forgets them all (CObjectMANAGER::Clear), and the new zone sends its own.
/// UI2 draws them (RoseRmlZoneTimers); the classic interface has no panel and
/// relies on the zone's announcements.
/// Boss alerts (GSV_ZONE_ALERT): a few words above the middle of the screen telling the
/// player what to do about a mechanic, with a bar that runs down for the time they have.
/// The server's own boss mod: it knows what is coming, so it says so. Drawn like the
/// STUNNED alert and above it, in the classic interface and UI2 alike, newest on top,
/// three at most; green for a shape to stand in, amber for the rest. A zone change
/// forgets them.
class CZoneAlerts {
public:
    static CZoneAlerts& Instance();

    /// From Recv_gsv_ZONE_ALERT.
    void Add(const char* szText, unsigned long dwMS, unsigned char btStyle, short nSound);
    /// Inside the HUD sprite block (CGameStateMain::Render_GameMENU).
    void Draw();
    void Forget();

private:
    struct Alert {
        std::string text;
        unsigned long dwStart;
        unsigned long dwMS;
        unsigned char btStyle;
    };

    CZoneAlerts() {}

    std::vector<Alert> m_Alerts;
};

class CZoneTimers {
public:
    struct Entry {
        unsigned char btID;
        std::string label;
        int iSecondsLeft;
    };

    static CZoneTimers& Instance();

    /// From Recv_gsv_ZONE_TIMER: iSeconds <= 0 removes the timer.
    void Set(unsigned char btID, const char* szLabel, int iSeconds);
    /// The timers still running, by id. One that ran out is dropped.
    void List(std::vector<Entry>& out);
    void Forget();

private:
    struct Timer {
        std::string label;
        unsigned long dwEndTick;
    };

    CZoneTimers() {}

    std::map<unsigned char, Timer> m_Timers;
};
