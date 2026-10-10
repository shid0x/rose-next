#pragma once

#include <map>
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
