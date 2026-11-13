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
/// The Karkia crypts' map, drawn as it is walked (2026-11-13, the user: "something that
/// reveals itself as the player goes deeper, akin to Diablo"). A crypt has no minimap
/// image: its maze changes every day. But the client is sent the day's walls (zone object
/// groups 10-13, one 15 m wall per closed edge of a 10 x 10 grid of 15 m cells), so it
/// knows the maze; this keeps which cells the player has seen and hands the minimap
/// (RoseRmlMinimap) only those: the cell stood in and the ones it opens onto, and the
/// whole boss hall once any of it is seen. What was seen is kept for as long as the
/// walls are the same ones (leaving and coming back the same day keeps the map; a new
/// day's maze starts dark), in memory only. The grid's place and size are the server's
/// (catacomb_maze.h: SW_CM, CELL_CM, N; the boss hall is catacombs.cpp's ROOM_MIN /
/// ROOM_MAX): **keep them in step**.
class CMazeMap {
public:
    enum { N = 10 };
    /// World cm. A wall is a segment: x0 == x1 or y0 == y1.
    struct Rect {
        float x0, y0, x1, y1;
        bool bRoom; ///< a floor of the boss hall
    };

    static CMazeMap& Instance();

    /// From CZoneObjects::Apply: a group of walls went up or came down.
    void SetWalls(unsigned char btGroup, bool bOn, const std::vector<CZoneObjects::Spec>& specs);
    /// A zone change took the walls (the next zone sends its own). What was seen stays.
    void Forget();
    /// Per frame: the cells round the avatar are seen.
    void Update();

    /// In a crypt, with its walls known.
    bool Active() const;
    /// Changes whenever List would answer differently.
    unsigned int Version() const { return m_uVersion; }
    /// The seen floor as runs of cells, and the walls round it as segments.
    void List(std::vector<Rect>& floors, std::vector<Rect>& walls) const;
    /// The maze's square.
    static void Bounds(float& fWest, float& fSouth, float& fEast, float& fNorth);

private:
    CMazeMap();
    void Rebuild();
    bool Closed(int iX, int iY, int iDX, int iDY) const;
    void See(int iX, int iY);

    std::map<unsigned char, std::vector<CZoneObjects::Spec> > m_Groups;
    bool m_bWallV[N + 1][N]; ///< on the grid line west of column x, beside row y
    bool m_bWallH[N][N + 1]; ///< on the grid line south of row y, under column x
    bool m_bSeen[N][N];
    int m_nWalls;
    unsigned int m_uWallHash; ///< of the walls and the zone
    unsigned int m_uSeenHash; ///< the maze m_bSeen belongs to
    unsigned long m_dwWallsAt; ///< when the walls last changed: they arrive group by group
    unsigned int m_uVersion;
};

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
