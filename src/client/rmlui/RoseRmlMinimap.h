#ifndef _ROSE_RML_MINIMAP_H_
#define _ROSE_RML_MINIMAP_H_

/**
 * UI2 minimap: replaces CMinimapDLG ( DLG_TYPE_MINIMAP ).
 *
 * A VIEW over the hidden CMinimapDLG, which stays the minimap's model: it
 * loads each zone's map ( SetMinimap on zone entry / warp ), works out the
 * map's world bounds, and keeps the indicators scripts place ( GF_* ). This
 * window draws the map image and, over it:
 *
 * - the NPCs placed in the map files, with their mark ( hover for the name ),
 *   or the pulsing indicator when a script points at one;
 * - the scripts' coordinate indicators in this zone;
 * - other players ( party members and the other team in their own marks );
 * - your heading arrow ( a CSS rotate: RoseRmlRenderer::SetTransform ).
 *
 * Drag the map to look around; moving, or Re-center, snaps it back. Zoom
 * and three sizes on top of the classic's two; the M key collapses it to the
 * title bar and L cycles the size ( CMinimapDLG hands both keys over ).
 * Zone name and coordinates sit in the title bar. The window opens and closes
 * through IT_MGR, so clan zones hide it as they hid the classic one.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CMinimapDLG;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlMinimap {
public:
    RoseRmlMinimap();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// The game's intent ( OpenDialog / CloseDialog: on in field zones, off
    /// in clan zones ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void ToggleCollapsed(); ///< the M key
    void CycleSize(); ///< the L key

    void Update();

    enum MarkKind {
        MARK_NPC = 0,
        MARK_INDICATOR = 1, ///< a script's pointer ( NPC or coordinates )
        MARK_PARTY = 2,
        MARK_PLAYER = 3,
        MARK_ENEMY = 4, ///< another team ( clan wars, PvP zones )
    };

    struct MarkVM {
        int kind;
        float x; ///< dp from the view's left, the mark's centre
        float y;
        Rml::String src;
        Rml::String rect;
        int tip; ///< index into m_Tips ( NPC names ), -1 for none

        bool operator==(const MarkVM& o) const {
            return kind == o.kind && x == o.x && y == o.y && src == o.src && rect == o.rect
                && tip == o.tip;
        }
        bool operator!=(const MarkVM& o) const { return !(*this == o); }
    };

private:
    CMinimapDLG* MinimapDlg() const;
    void SetVisible(bool bVisible);
    void Sample();
    void UpdatePan();
    void UpdateTooltip();
    void PlaceDefault();
    void SaveSettings();

    /// World position -> dp from the view's top-left.
    void WorldToView(float fWorldX, float fWorldY, float& fX, float& fY) const;
    bool InView(float fX, float fY) const;

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::Element* m_pView;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    /// The map texture currently shown, to release it when the zone changes.
    std::string m_strLoadedMap;

    /// --- view state ----------------------------------------------------------
    float m_fCenterX; ///< world position at the view's centre
    float m_fCenterY;
    bool m_bExplore; ///< dragged away from the avatar
    float m_fExploreAvatarX; ///< where the avatar stood when exploring began
    float m_fExploreAvatarY;
    bool m_bPanning;
    int m_iPanX; ///< last mouse position while panning ( screen pixels )
    int m_iPanY;
    float m_fViewSize; ///< dp, the square view's side
    float m_fZoom; ///< dp per texture pixel

    /// NPC names ( UTF-8 ) for the hover label, by MarkVM::tip.
    std::vector<Rml::String> m_Tips;

    /// --- bound to minimap.rml ------------------------------------------------
    int m_iSize; ///< 0 small, 1 medium, 2 large
    int m_iZoom; ///< index into the zoom steps
    bool m_bCollapsed;
    bool m_bHasMap;
    Rml::String m_strZone;
    Rml::String m_strCoords;
    Rml::String m_strMapSrc;
    float m_fMapX; ///< dp
    float m_fMapY;
    float m_fMapW;
    float m_fMapH;
    float m_fViewDp; ///< bound copy of m_fViewSize
    std::vector<MarkVM> m_Marks;
    bool m_bArrow;
    float m_fArrowX;
    float m_fArrowY;
    float m_fArrowDeg;
    Rml::String m_strArrowSrc;
    bool m_bExploring; ///< bound: shows Re-center

    /// The hovered NPC's name, drawn by RmlUi above its mark: a classic
    /// tooltip draws before the RmlUi pass and so ended up behind the map.
    /// The label is a child of the WINDOW, not the clipped view, so it can
    /// overhang the map's edge.
    Rml::String m_strHover; ///< the name
    Rml::String m_strHoverRole; ///< "Clan Owner" from "[Clan Owner] Burtland"
    Rml::Element* m_pHoverLabel; ///< placed from C++ on whole pixels
    int m_iHoverTip; ///< the hovered mark's tip, -2 for none ( rings it )
    int m_iHoverPop; ///< 1 / 2, alternated per new NPC to replay the pop
};

#endif /// _ROSE_RML_MINIMAP_H_
