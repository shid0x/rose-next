#include "stdafx.h"

#include "RoseRmlMinimap.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseUi2.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../IO_Terrain.h"
#include "../System/CGame.h"
#include "../GameData/CParty.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CToolTipMgr.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Dlgs/CMinimapDLG.h"
#include "tgamectrl/resourcemgr.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>
#include <stdarg.h>

namespace {

const char* kIniPath = ".\\rose-next.ini";

/// The square view's side, in dp, for small / medium / large.
const float kViewSizes[3] = {180.0f, 240.0f, 320.0f};

/// dp per map texture pixel. 1 is the classic scale.
const float kZoomSteps[5] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
const int kZoomDefault = 2;

/// The map texture has a one-map border ( CMinimapDLG, MINIMAP_RESOLUTION_PER_MAP ).
const float kMapBorderPx = 64.0f;

/// A mark's box ( dp ): marks are centred on their point.
const float kMarkHalf = 10.0f;

/// The state-icon sprite the classic minimap used for its indicators.
const int kIndicatorIcon = 4;
/// ... and for a player of another team.
const int kEnemyIcon = 73;

/// A drag shorter than this ( screen pixels ) is still a click.
const int kPanSlop = 3;

Rml::String
Printf(const char* pszFormat, ...) {
    char szBuf[96];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf(szBuf, sizeof(szBuf), pszFormat, args);
    va_end(args);
    szBuf[sizeof(szBuf) - 1] = '\0';
    return szBuf;
}

/// "3Ddata\Maps\..\minimap.DDS" -> forward slashes, which RmlUi's URL
/// handling expects; the loader finds the file on disk or in the VFS either way.
Rml::String
GamePath(const std::string& strPath) {
    Rml::String out(strPath);
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\\')
            out[i] = '/';
    }
    return out;
}

int
ReadIni(const char* pszKey, int iDefault) {
    return (int)GetPrivateProfileIntA("VIDEO", pszKey, iDefault, kIniPath);
}

} // namespace

RoseRmlMinimap::RoseRmlMinimap():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_pView(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_fCenterX(0.0f),
    m_fCenterY(0.0f),
    m_bExplore(false),
    m_fExploreAvatarX(0.0f),
    m_fExploreAvatarY(0.0f),
    m_bPanning(false),
    m_iPanX(0),
    m_iPanY(0),
    m_fViewSize(kViewSizes[0]),
    m_fZoom(kZoomSteps[kZoomDefault]),
    m_iSize(0),
    m_iZoom(kZoomDefault),
    m_bCollapsed(false),
    m_bHasMap(false),
    m_fMapX(0.0f),
    m_fMapY(0.0f),
    m_fMapW(0.0f),
    m_fMapH(0.0f),
    m_fViewDp(kViewSizes[0]),
    m_bArrow(false),
    m_fArrowX(0.0f),
    m_fArrowY(0.0f),
    m_fArrowDeg(0.0f),
    m_bExploring(false),
    m_pHoverLabel(NULL),
    m_iHoverTip(-2),
    m_iHoverPop(1) {}

bool
RoseRmlMinimap::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    m_iSize = ReadIni("UI_MINIMAP_SIZE", 0);
    if (m_iSize < 0 || m_iSize > 2)
        m_iSize = 0;
    m_iZoom = ReadIni("UI_MINIMAP_ZOOM", kZoomDefault);
    if (m_iZoom < 0 || m_iZoom > 4)
        m_iZoom = kZoomDefault;
    m_bCollapsed = ReadIni("UI_MINIMAP_COLLAPSED", 0) != 0;
    m_fViewSize = m_fViewDp = kViewSizes[m_iSize];
    m_fZoom = kZoomSteps[m_iZoom];

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("minimap");
    if (!constructor)
        return false;

    if (auto mark = constructor.RegisterStruct<MarkVM>()) {
        mark.RegisterMember("kind", &MarkVM::kind);
        mark.RegisterMember("x", &MarkVM::x);
        mark.RegisterMember("y", &MarkVM::y);
        mark.RegisterMember("src", &MarkVM::src);
        mark.RegisterMember("rect", &MarkVM::rect);
        mark.RegisterMember("tip", &MarkVM::tip);
    }
    constructor.RegisterArray<std::vector<MarkVM>>();

    constructor.Bind("map_size", &m_iSize); /// "size" is reserved ( array .size )
    constructor.Bind("zoom", &m_iZoom);
    constructor.Bind("collapsed", &m_bCollapsed);
    constructor.Bind("has_map", &m_bHasMap);
    constructor.Bind("zone", &m_strZone);
    constructor.Bind("coords", &m_strCoords);
    constructor.Bind("map_src", &m_strMapSrc);
    constructor.Bind("map_x", &m_fMapX);
    constructor.Bind("map_y", &m_fMapY);
    constructor.Bind("map_w", &m_fMapW);
    constructor.Bind("map_h", &m_fMapH);
    constructor.Bind("view", &m_fViewDp);
    constructor.Bind("marks", &m_Marks);
    constructor.Bind("arrow", &m_bArrow);
    constructor.Bind("arrow_x", &m_fArrowX);
    constructor.Bind("arrow_y", &m_fArrowY);
    constructor.Bind("arrow_deg", &m_fArrowDeg);
    constructor.Bind("arrow_src", &m_strArrowSrc);
    constructor.Bind("exploring", &m_bExploring);
    constructor.Bind("hover", &m_strHover);
    constructor.Bind("hover_role", &m_strHoverRole);
    constructor.Bind("hover_tip", &m_iHoverTip);
    constructor.Bind("hover_pop", &m_iHoverPop);

    constructor.BindEventCallback("zoom_in",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (m_iZoom < 4) {
                m_fZoom = kZoomSteps[++m_iZoom];
                m_Model.DirtyVariable("zoom");
                SaveSettings();
            }
        });
    constructor.BindEventCallback("zoom_out",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (m_iZoom > 0) {
                m_fZoom = kZoomSteps[--m_iZoom];
                m_Model.DirtyVariable("zoom");
                SaveSettings();
            }
        });
    constructor.BindEventCallback("cycle_size",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { CycleSize(); });
    constructor.BindEventCallback("toggle_collapsed",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { ToggleCollapsed(); });
    constructor.BindEventCallback("recenter",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            ev.StopPropagation(); /// not a pan start
            m_bExplore = false;
        });

    /// A press on the map starts a pan; UpdatePan follows the mouse.
    constructor.BindEventCallback("pan_start",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<int>("button", 0) != 0 || !m_bHasMap)
                return;
            m_bPanning = true;
            m_iPanX = ev.GetParameter<int>("mouse_x", 0);
            m_iPanY = ev.GetParameter<int>("mouse_y", 0);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "minimap.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load minimap document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("minimap");
    m_pView = m_pDocument->GetElementById("mapview");
    m_pHoverLabel = m_pDocument->GetElementById("npctip");
    RoseRmlLayout::Track(m_pPanel, "minimap");

    LOG_INFO("[rmlui] minimap document loaded");
    return true;
}

void
RoseRmlMinimap::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_pView = NULL;
    m_pHoverLabel = NULL;
    m_bVisible = false;
    m_bOpen = false;
    m_strLoadedMap.clear();
}

CMinimapDLG*
RoseRmlMinimap::MinimapDlg() const {
    return (CMinimapDLG*)g_itMGR.FindDlg(DLG_TYPE_MINIMAP);
}

void
RoseRmlMinimap::SetOpen(bool bOpen) {
    m_bOpen = bOpen;
    m_bPanning = false;
}

void
RoseRmlMinimap::ToggleCollapsed() {
    m_bCollapsed = !m_bCollapsed;
    m_bPanning = false;
    m_Model.DirtyVariable("collapsed");
    SaveSettings();
}

void
RoseRmlMinimap::CycleSize() {
    m_iSize = (m_iSize + 1) % 3;
    m_fViewSize = m_fViewDp = kViewSizes[m_iSize];
    m_Model.DirtyVariable("map_size");
    m_Model.DirtyVariable("view");
    SaveSettings();
}

void
RoseRmlMinimap::SaveSettings() {
    char szBuf[16];
    _snprintf(szBuf, sizeof(szBuf), "%d", m_iSize);
    WritePrivateProfileStringA("VIDEO", "UI_MINIMAP_SIZE", szBuf, kIniPath);
    _snprintf(szBuf, sizeof(szBuf), "%d", m_iZoom);
    WritePrivateProfileStringA("VIDEO", "UI_MINIMAP_ZOOM", szBuf, kIniPath);
    WritePrivateProfileStringA("VIDEO", "UI_MINIMAP_COLLAPSED", m_bCollapsed ? "1" : "0", kIniPath);
}

void
RoseRmlMinimap::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_bPanning = false;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlMinimap::WorldToView(float fWorldX, float fWorldY, float& fX, float& fY) const {
    CMinimapDLG* pDlg = MinimapDlg();
    const float fWpp = pDlg ? pDlg->GetWorldPerPixel() : 1.0f;
    fX = m_fViewSize * 0.5f + (fWorldX - m_fCenterX) / fWpp * m_fZoom;
    fY = m_fViewSize * 0.5f + (m_fCenterY - fWorldY) / fWpp * m_fZoom;
}

bool
RoseRmlMinimap::InView(float fX, float fY) const {
    return fX >= 0.0f && fY >= 0.0f && fX <= m_fViewSize && fY <= m_fViewSize;
}

void
RoseRmlMinimap::Sample() {
    CObjUSER* pAvatar = g_pAVATAR;
    CMinimapDLG* pDlg = MinimapDlg();
    if (pAvatar == NULL || pDlg == NULL || g_pTerrain == NULL)
        return;

    /// --- title bar ------------------------------------------------------------
    const Rml::String strZone = RoseRmlText::FromGame(ZONE_NAME(g_pTerrain->GetZoneNO()));
    if (strZone != m_strZone) {
        m_strZone = strZone;
        m_Model.DirtyVariable("zone");
    }
    const Rml::String strCoords = Printf("%d, %d",
        (int)pAvatar->Get_CurPOS().x / 100, (int)pAvatar->Get_CurPOS().y / 100);
    if (strCoords != m_strCoords) {
        m_strCoords = strCoords;
        m_Model.DirtyVariable("coords");
    }

    /// --- the map ------------------------------------------------------------------
    const bool bHasMap = pDlg->HasMinimap();
    if (bHasMap != m_bHasMap) {
        m_bHasMap = bHasMap;
        m_Model.DirtyVariable("has_map");
    }
    if (!bHasMap) {
        if (m_bArrow) {
            m_bArrow = false; /// the arrow sits on the map
            m_Model.DirtyVariable("arrow");
        }
        return;
    }

    /// A new zone: show its map and release the last one's texture ( each is a
    /// few MB decoded, and a 32-bit client should not keep every zone's ).
    const Rml::String strSrc = GamePath(pDlg->GetMinimapFile());
    if (strSrc != m_strMapSrc) {
        const Rml::String strOld = m_strMapSrc;
        m_strMapSrc = strSrc;
        m_Model.DirtyVariable("map_src");
        if (!strOld.empty())
            Rml::ReleaseTexture(strOld);
        m_bExplore = false;
    }
    const Rml::String strArrow = GamePath(pDlg->GetArrowFile());
    if (strArrow != m_strArrowSrc) {
        m_strArrowSrc = strArrow;
        m_Model.DirtyVariable("arrow_src");
    }

    /// Centre: the avatar, unless you dragged away ( until the avatar moves ).
    const float fAvatarX = pAvatar->Get_CurPOS().x;
    const float fAvatarY = pAvatar->Get_CurPOS().y;
    if (m_bExplore && (fAvatarX != m_fExploreAvatarX || fAvatarY != m_fExploreAvatarY))
        m_bExplore = false;
    if (!m_bExplore) {
        m_fCenterX = fAvatarX;
        m_fCenterY = fAvatarY;
    }
    if (m_bExplore != m_bExploring) {
        m_bExploring = m_bExplore;
        m_Model.DirtyVariable("exploring");
    }

    const float fWpp = pDlg->GetWorldPerPixel();
    int iTexW = 0, iTexH = 0;
    pDlg->GetMinimapTextureSize(iTexW, iTexH);

    /// The map image: the centre's texture pixel lands at the view's centre.
    /// Whole pixels, or the map resamples between texels and blurs.
    const float fRatio = RoseRmlLayout::GetScaleRatio();
    const float fTexX = (m_fCenterX - pDlg->GetMinimapWorldLeft()) / fWpp + kMapBorderPx;
    const float fTexY = (pDlg->GetMinimapWorldTop() - m_fCenterY) / fWpp + kMapBorderPx;
    const float fMapX = floorf((m_fViewSize * 0.5f - fTexX * m_fZoom) * fRatio) / fRatio;
    const float fMapY = floorf((m_fViewSize * 0.5f - fTexY * m_fZoom) * fRatio) / fRatio;
    const float fMapW = iTexW * m_fZoom;
    const float fMapH = iTexH * m_fZoom;
    if (fMapX != m_fMapX || fMapY != m_fMapY || fMapW != m_fMapW || fMapH != m_fMapH) {
        m_fMapX = fMapX;
        m_fMapY = fMapY;
        m_fMapW = fMapW;
        m_fMapH = fMapH;
        m_Model.DirtyVariable("map_x");
        m_Model.DirtyVariable("map_y");
        m_Model.DirtyVariable("map_w");
        m_Model.DirtyVariable("map_h");
    }

    /// --- marks ( in drawing order: players, then NPCs and indicators on top ) --
    static int s_iPlayerImage = -1, s_iPartyImage = -1;
    if (s_iPlayerImage < 0) {
        s_iPlayerImage = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "ID_OTHER_AVATAR");
        s_iPartyImage =
            CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "ID_MINIMAP_PARTYMEMBER");
    }

    std::vector<MarkVM> marks;
    std::vector<Rml::String> tips;
    MarkVM mark;
    mark.tip = -1;

    const short nViewObjects = g_pObjMGR->GetViewObjectCnt();
    for (short i = 0; i < nViewObjects; ++i) {
        CObjCHAR* pChar = g_pObjMGR->Get_CharOBJ(g_pObjMGR->GetViewObjectIndex(i), false);
        if (pChar == NULL || pChar->Get_TYPE() != OBJ_AVATAR)
            continue;
        float fX, fY;
        WorldToView(pChar->Get_CurXPOS(), pChar->Get_CurYPOS(), fX, fY);
        if (!InView(fX, fY))
            continue;

        bool bOk;
        if (pChar->Get_TeamNO() != pAvatar->Get_TeamNO()) {
            mark.kind = MARK_ENEMY;
            bOk = RoseRmlIcons::Resolve(IMAGE_RES_STATE_ICON, kEnemyIcon, mark.src, mark.rect);
        } else if (CParty::GetInstance().IsPartyMember(
                       g_pObjMGR->Get_ServerObjectIndex(pChar->Get_INDEX()))) {
            mark.kind = MARK_PARTY;
            bOk = RoseRmlIcons::Resolve(IMAGE_RES_UI, s_iPartyImage, mark.src, mark.rect);
        } else {
            mark.kind = MARK_PLAYER;
            bOk = RoseRmlIcons::Resolve(IMAGE_RES_UI, s_iPlayerImage, mark.src, mark.rect);
        }
        if (!bOk)
            continue;
        mark.x = floorf(fX - kMarkHalf);
        mark.y = floorf(fY - kMarkHalf);
        marks.push_back(mark);
    }

    /// NPCs placed in the map files ( the classic minimap's source too: a
    /// moving NPC shows where the map puts it ). No name, no mark.
    const std::list<NpcInfoInMap>& npcs = g_pObjMGR->GetNpcInfoList();
    for (std::list<NpcInfoInMap>::const_iterator it = npcs.begin(); it != npcs.end(); ++it) {
        const char* pszName = NPC_NAME(it->m_iNpcID);
        if (pszName == NULL || pszName[0] == '\0')
            continue;
        float fX, fY;
        WorldToView(it->m_Position.x, it->m_Position.y, fX, fY);
        if (!InView(fX, fY))
            continue;

        const bool bIndicated = pDlg->GetIndicatorNpc(it->m_iNpcID) != 0;
        mark.kind = bIndicated ? MARK_INDICATOR : MARK_NPC;
        if (!RoseRmlIcons::Resolve(IMAGE_RES_STATE_ICON,
                bIndicated ? kIndicatorIcon : NPC_MARK_NO(it->m_iNpcID), mark.src, mark.rect))
            continue;
        mark.x = floorf(fX - kMarkHalf);
        mark.y = floorf(fY - kMarkHalf);
        mark.tip = (int)tips.size();
        tips.push_back(RoseRmlText::FromGame(pszName));
        marks.push_back(mark);
        mark.tip = -1;
    }

    /// The scripts' coordinate indicators in this zone.
    std::vector<std::pair<float, float>> points;
    pDlg->CollectCoordinateIndicators(g_pTerrain->GetZoneNO(), points);
    for (size_t i = 0; i < points.size(); ++i) {
        float fX, fY;
        WorldToView(points[i].first, points[i].second, fX, fY);
        if (!InView(fX, fY))
            continue;
        mark.kind = MARK_INDICATOR;
        if (!RoseRmlIcons::Resolve(IMAGE_RES_STATE_ICON, kIndicatorIcon, mark.src, mark.rect))
            continue;
        mark.x = floorf(fX - kMarkHalf);
        mark.y = floorf(fY - kMarkHalf);
        marks.push_back(mark);
    }

    m_Tips.swap(tips);

    /// Grow-only: an entry past the live ones is hidden ( kind -1 ), never
    /// removed. Each mark also reads hover_tip ( its ring ), and when the
    /// list shrank in the same frame hover_tip changed, RmlUi re-evaluated a
    /// dying mark past the end: "Data array index out of bounds".
    MarkVM unused;
    unused.kind = -1;
    unused.x = 0.0f;
    unused.y = 0.0f;
    unused.tip = -1;
    while (marks.size() < m_Marks.size())
        marks.push_back(unused);

    if (marks != m_Marks) {
        m_Marks.swap(marks);
        m_Model.DirtyVariable("marks");
    }

    /// --- your arrow: the classic cursor texture, turned to your heading -------
    float fAX, fAY;
    WorldToView(fAvatarX, fAvatarY, fAX, fAY);
    const bool bArrow = InView(fAX, fAY);
    if (bArrow != m_bArrow) {
        m_bArrow = bArrow;
        m_Model.DirtyVariable("arrow");
    }
    /// CMinimapDLG turned it by -heading ( D3DXMatrixRotationZ on a y-down
    /// screen ); CSS rotate() turns clockwise, so the same sign.
    const float fDeg = -pAvatar->GetDirection();
    if (fAX != m_fArrowX || fAY != m_fArrowY || fDeg != m_fArrowDeg) {
        m_fArrowX = fAX;
        m_fArrowY = fAY;
        m_fArrowDeg = fDeg;
        m_Model.DirtyVariable("arrow_x");
        m_Model.DirtyVariable("arrow_y");
        m_Model.DirtyVariable("arrow_deg");
    }
}

void
RoseRmlMinimap::UpdatePan() {
    if (!m_bPanning)
        return;
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_bPanning = false;
        return;
    }
    CMinimapDLG* pDlg = MinimapDlg();
    if (pDlg == NULL)
        return;

    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iDX = ptMouse.x - m_iPanX;
    const int iDY = ptMouse.y - m_iPanY;
    if (!m_bExplore && abs(iDX) < kPanSlop && abs(iDY) < kPanSlop)
        return;

    if (!m_bExplore) {
        m_bExplore = true;
        m_fExploreAvatarX = g_pAVATAR->Get_CurPOS().x;
        m_fExploreAvatarY = g_pAVATAR->Get_CurPOS().y;
    }

    /// Screen pixels -> dp -> texture pixels -> world. Dragging right moves
    /// the map right, so the centre goes west ( and north for dragging down ).
    const float fWpp = pDlg->GetWorldPerPixel();
    const float fScale = fWpp / (RoseRmlLayout::GetScaleRatio() * m_fZoom);
    float fX = m_fCenterX - iDX * fScale;
    float fY = m_fCenterY + iDY * fScale;

    /// Inside the map, as the classic explore mode.
    fX = max(pDlg->GetMinimapWorldLeft(), min(fX, pDlg->GetMinimapWorldRight()));
    fY = max(pDlg->GetMinimapWorldBottom(), min(fY, pDlg->GetMinimapWorldTop()));
    m_fCenterX = fX;
    m_fCenterY = fY;
    m_iPanX = ptMouse.x;
    m_iPanY = ptMouse.y;
}

void
RoseRmlMinimap::UpdateTooltip() {
    /// The hovered mark's NPC name, as a label above the mark.
    int iTip = -1;
    if (!m_bPanning && !CDragNDropMgr::GetInstance().IsDraging()) {
        for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
             pEl = pEl->GetParentNode()) {
            if (pEl->HasAttribute("tip")) {
                iTip = pEl->GetAttribute<int>("tip", -1);
                break;
            }
        }
    }

    Rml::String strName, strRole;
    int iHoverTip = -2;
    const MarkVM* pMark = NULL;
    if (iTip >= 0 && iTip < (int)m_Tips.size()) {
        for (size_t i = 0; i < m_Marks.size(); ++i) {
            if (m_Marks[i].tip == iTip) {
                pMark = &m_Marks[i];
                break;
            }
        }
    }
    if (pMark != NULL) {
        iHoverTip = iTip;

        /// "[Clan Owner] Burtland": the role over the name, as the target frame.
        strName = m_Tips[iTip];
        const size_t iClose = strName.find(']');
        if (!strName.empty() && strName[0] == '[' && iClose != Rml::String::npos) {
            strRole = strName.substr(1, iClose - 1);
            strName = strName.substr(iClose + 1);
            while (!strName.empty() && strName[0] == ' ')
                strName.erase(0, 1);
        }
    }

    /// A different NPC replays the pop ( alternating two identical animations,
    /// as the character window's flash ).
    if (iHoverTip != m_iHoverTip) {
        if (iHoverTip >= 0) {
            m_iHoverPop = (m_iHoverPop == 1) ? 2 : 1;
            m_Model.DirtyVariable("hover_pop");
        }
        m_iHoverTip = iHoverTip;
        m_Model.DirtyVariable("hover_tip");
    }
    if (strName != m_strHover || strRole != m_strHoverRole) {
        m_strHover = strName;
        m_strHoverRole = strRole;
        m_Model.DirtyVariable("hover");
        m_Model.DirtyVariable("hover_role");
    }

    /// Centred above the mark, on WHOLE pixels: a translate(-50%) centring put
    /// an odd-width label on a half pixel and blurred every glyph. The label is
    /// a child of the window ( left / top from its padding edge ), measured as
    /// last laid out -- a new name is one frame off, inside the fade-in.
    if (pMark != NULL && m_pHoverLabel != NULL && m_pView != NULL && m_pPanel != NULL) {
        const float fRatio = RoseRmlLayout::GetScaleRatio();
        const Rml::Vector2f view = m_pView->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f panel = m_pPanel->GetAbsoluteOffset(Rml::BoxArea::Padding);
        const Rml::Vector2f size = m_pHoverLabel->GetBox().GetSize(Rml::BoxArea::Border);
        const float fCentreX = (view.x - panel.x) + (pMark->x + kMarkHalf) * fRatio;
        const float fMarkTop = (view.y - panel.y) + pMark->y * fRatio;
        const float fLeft = floorf(fCentreX - size.x * 0.5f);
        const float fTop = floorf(fMarkTop - size.y - 2.0f * fRatio);
        m_pHoverLabel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
        m_pHoverLabel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
    }
}

void
RoseRmlMinimap::PlaceDefault() {
    /// Top right, as the classic minimap -- only while no position is set
    /// ( dragged, saved or reset ).
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet
    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fGap = 3.0f * RoseRmlLayout::GetScaleRatio();
    m_pPanel->SetProperty(Rml::PropertyId::Left,
        Rml::Property(floorf((float)view.x - size.x - fGap), Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(floorf(fGap), Rml::Unit::PX));
}

void
RoseRmlMinimap::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;

    SetVisible(m_bOpen && bInWorld);
    if (!m_bVisible)
        return;

    UpdatePan();
    Sample();
    PlaceDefault();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateTooltip();
}
