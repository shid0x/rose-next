#include "stdafx.h"

#include "RoseRmlSkillTree.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../CObjUSER.h"
#include "../Game.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../GameCommon/Skill.h"
#include "../GameCommon/StringManager.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/Icon/CIconSkill.h"
#include "../interface/Icon/CIconSkillDummy.h"
#include "../interface/SlotContainer/CSkillSlot.h"
#include "../interface/command/dragcommand.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"

#include "rose/common/log.h"

#include <algorithm>
#include <map>
#include <math.h>

namespace {

/// The canvas, in dp. A node is the icon with its name to the right; the
/// gap between columns holds the connectors' elbows.
const float kPad = 10.0f;
const float kIcon = 42.0f;
const float kNodeW = 118.0f;
const float kColPitch = 144.0f;
const float kRowPitch = 50.0f;
const float kLaneGap = 14.0f;
const float kLine = 2.0f;
/// Skills with no prerequisite that nothing needs share the last lane.
const int kLoneColumns = 4;

/// The highest rank of the line starting at iBase: consecutive rows sharing
/// SKILL_1LEV_INDEX, one level apart ( as the skill window counts ).
int
MaxRankOf(int iBase) {
    int iLevel = SKILL_LEVEL(iBase);
    const int iRows = g_SkillList.Get_SkillCNT();
    for (int i = iBase; i + 1 < iRows; ++i) {
        if (SKILL_1LEV_INDEX(i + 1) != SKILL_1LEV_INDEX(i) || SKILL_LEVEL(i + 1) != SKILL_LEVEL(i) + 1)
            break;
        ++iLevel;
    }
    return iLevel;
}

/// A prerequisite may name any rank's row; the tree is keyed by rank 1.
int
BaseOf(int iRow) {
    const int iBase = SKILL_1LEV_INDEX(iRow);
    return iBase > 0 ? iBase : iRow;
}

/// The jobs a LIST_CLASS row lets in; empty = everyone ( class-less ).
void
JobsOf(int iClassSet, std::vector<int>& Jobs) {
    Jobs.clear();
    if (iClassSet <= 0 || CLASS_INCLUDE_JOB(iClassSet, 0) == 0)
        return;
    for (int i = 0; i < CLASS_INCLUDE_JOB_CNT; ++i) {
        const int iJob = CLASS_INCLUDE_JOB(iClassSet, i);
        if (iJob == 0)
            break;
        Jobs.push_back(iJob);
    }
}

bool
Has(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

Rml::String
GameName(int iRow) {
    const char* psz = SKILL_NAME(iRow);
    return psz ? RoseRmlText::FromGame(psz) : Rml::String();
}

Rml::String
JobName(int iJob) {
    const char* psz = CStringManager::GetSingleton().GetJobName(iJob);
    return (psz && psz[0]) ? RoseRmlText::FromGame(psz) : Rml::String(CStr::Printf("Job %d", iJob));
}

/// Game text for a data-rml block: escaped, line breaks kept.
Rml::String
ToRml(const char* pszGame) {
    const Rml::String strText = RoseRmlText::Escape(RoseRmlText::FromGame(pszGame ? pszGame : ""));
    Rml::String out;
    out.reserve(strText.size() + 16);
    for (size_t i = 0; i < strText.size(); ++i) {
        if (strText[i] == '\r')
            continue;
        if (strText[i] == '\n')
            out += "<br/>";
        else
            out += strText[i];
    }
    return out;
}

} // namespace

RoseRmlSkillTree::RoseRmlSkillTree():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iBuiltJob(-1),
    m_pDragItem(NULL),
    m_iPressNode(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iSelected(-1),
    m_bHasTree(false),
    m_fWidth(0.0f),
    m_fHeight(0.0f),
    m_iFilter(0),
    m_bHasSel(false) {}

RoseRmlSkillTree::~RoseRmlSkillTree() {
    delete m_pDragItem; /// deletes its commands too
}

bool
RoseRmlSkillTree::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// The skill window's drag, onto either skill bar row.
    m_pDragItem = new CDragItem;
    m_pDragItem->AddTarget(DLG_TYPE_QUICKBAR, new CTCmdDragSkill2QuickBar);
    m_pDragItem->AddTarget(DLG_TYPE_QUICKBAR_EXT, new CTCmdDragSkill2QuickBar(DLG_TYPE_QUICKBAR_EXT));

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("skilltree");
    if (!constructor)
        return false;

    if (auto node = constructor.RegisterStruct<NodeVM>()) {
        node.RegisterMember("index", &NodeVM::index);
        node.RegisterMember("x", &NodeVM::x);
        node.RegisterMember("y", &NodeVM::y);
        node.RegisterMember("src", &NodeVM::src);
        node.RegisterMember("rect", &NodeVM::rect);
        node.RegisterMember("name", &NodeVM::name);
        node.RegisterMember("rank", &NodeVM::rank);
        node.RegisterMember("tag", &NodeVM::tag);
        node.RegisterMember("learned", &NodeVM::learned);
        node.RegisterMember("ready", &NodeVM::ready);
        node.RegisterMember("locked", &NodeVM::locked);
        node.RegisterMember("dim", &NodeVM::dim);
        node.RegisterMember("on", &NodeVM::on);
    }
    constructor.RegisterArray<std::vector<NodeVM>>();
    if (auto seg = constructor.RegisterStruct<SegVM>()) {
        seg.RegisterMember("x", &SegVM::x);
        seg.RegisterMember("y", &SegVM::y);
        seg.RegisterMember("w", &SegVM::w);
        seg.RegisterMember("h", &SegVM::h);
        seg.RegisterMember("lit", &SegVM::lit);
        seg.RegisterMember("dim", &SegVM::dim);
    }
    constructor.RegisterArray<std::vector<SegVM>>();
    if (auto lane = constructor.RegisterStruct<LaneVM>()) {
        lane.RegisterMember("y", &LaneVM::y);
        lane.RegisterMember("h", &LaneVM::h);
        lane.RegisterMember("odd", &LaneVM::odd);
    }
    constructor.RegisterArray<std::vector<LaneVM>>();
    if (auto job = constructor.RegisterStruct<JobVM>()) {
        job.RegisterMember("job", &JobVM::job);
        job.RegisterMember("name", &JobVM::name);
        job.RegisterMember("on", &JobVM::on);
    }
    constructor.RegisterArray<std::vector<JobVM>>();
    if (auto req = constructor.RegisterStruct<ReqVM>()) {
        req.RegisterMember("text", &ReqVM::text);
        req.RegisterMember("ok", &ReqVM::ok);
    }
    constructor.RegisterArray<std::vector<ReqVM>>();

    constructor.Bind("has_tree", &m_bHasTree);
    constructor.Bind("title", &m_strTitle);
    constructor.Bind("width", &m_fWidth);
    constructor.Bind("height", &m_fHeight);
    constructor.Bind("nodes", &m_NodeVMs);
    constructor.Bind("segs", &m_SegVMs);
    constructor.Bind("lanes", &m_LaneVMs);
    constructor.Bind("jobs", &m_Jobs);
    constructor.Bind("has_sel", &m_bHasSel);
    constructor.Bind("sel_name", &m_strSelName);
    constructor.Bind("sel_rank", &m_strSelRank);
    constructor.Bind("sel_tag", &m_strSelTag);
    constructor.Bind("sel_desc", &m_strSelDesc);
    constructor.Bind("sel_note", &m_strSelNote);
    constructor.Bind("req_head", &m_strReqHead);
    constructor.Bind("reqs", &m_Reqs);

    constructor.BindEventCallback("filter",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                SetFilter(args[0].Get<int>());
        });
    constructor.BindEventCallback("select",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int i = args[0].Get<int>();
            if (i >= 0 && i < (int)m_Nodes.size())
                m_iSelected = i;
        });
    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.empty() || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressNode = args[0].Get<int>();
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
        });
    /// Double-click uses a learned skill, as in the skill window.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty() || g_pAVATAR == NULL)
                return;
            const int i = args[0].Get<int>();
            if (i < 0 || i >= (int)m_Nodes.size())
                return;
            int iSlot = -1;
            if (LearnedRank(m_Nodes[i].iBase, &iSlot) <= 0 || iSlot < 0)
                return;
            CIconSkill icon(iSlot);
            if (icon.GetSkill() != NULL)
                icon.ExecuteCommand();
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_SKILLTREE);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "skilltree.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load skill tree document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("skilltree");
    RoseRmlLayout::Track(m_pPanel, "skilltree");

    LOG_INFO("[rmlui] skill tree document loaded");
    return true;
}

void
RoseRmlSkillTree::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

bool
RoseRmlSkillTree::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlSkillTree::SetOpen(bool bOpen) {
    if (bOpen == m_bOpen)
        return;
    RoseUi2::PlayWindowSound(DLG_TYPE_SKILLTREE, bOpen);
    m_bOpen = bOpen;
    m_iPressNode = -1;
    if (bOpen) {
        if (g_pAVATAR != NULL && g_pAVATAR->Get_JOB() != m_iBuiltJob)
            Build();
        Sample();
    }
}

void
RoseRmlSkillTree::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressNode = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- building ---------------------------------------------------------------------

void
RoseRmlSkillTree::Build() {
    m_Nodes.clear();
    m_Segs.clear();
    m_iSelected = -1;
    m_iBuiltJob = g_pAVATAR ? g_pAVATAR->Get_JOB() : -1;

    const int iJob = m_iBuiltJob > 0 ? m_iBuiltJob : 0;
    const int iFamily = iJob / 100;
    const int iFirst = iFamily * 100 + 11;
    const int iSecondA = iFamily * 100 + 21, iSecondB = iFamily * 100 + 22;
    const int iThirdA = iFamily * 100 + 31, iThirdB = iFamily * 100 + 32;

    /// The skills: rank-1 rows with a name and an icon, for a job of this
    /// family.
    std::map<int, int> ByBase;
    std::vector<int> Jobs;
    const int iRows = g_SkillList.Get_SkillCNT();
    for (int r = 1; iFamily > 0 && r < iRows; ++r) {
        if (SKILL_LEVEL(r) != 1 || (SKILL_1LEV_INDEX(r) != 0 && SKILL_1LEV_INDEX(r) != r))
            continue;
        if (SKILL_ICON_NO(r) <= 0)
            continue;
        const char* pszName = SKILL_NAME(r);
        if (pszName == NULL || pszName[0] == '\0')
            continue;
        JobsOf(SKILL_AVAILBLE_CLASS_SET(r), Jobs);
        bool bFamily = false;
        for (size_t j = 0; j < Jobs.size(); ++j)
            bFamily = bFamily || Jobs[j] / 100 == iFamily;
        if (!bFamily)
            continue; /// another class's, or class-less ( emotes and such )

        Node node;
        node.iBase = r;
        node.iMaxRank = max(1, MaxRankOf(r));
        node.bPassive = SKILL_TYPE(r) == SKILL_TYPE_PASSIVE;
        if (Has(Jobs, iFirst)) {
            node.iJobMask = 3;
        } else {
            node.iJobMask = ((Has(Jobs, iSecondA) || Has(Jobs, iThirdA)) ? 1 : 0)
                | ((Has(Jobs, iSecondB) || Has(Jobs, iThirdB)) ? 2 : 0);
            if (node.iJobMask == 0)
                node.iJobMask = 3;
        }
        node.iCol = node.iRow = node.iLane = 0;
        ByBase[r] = (int)m_Nodes.size();
        m_Nodes.push_back(node);
    }

    /// The prerequisites that are in this tree ( rank 1's columns ).
    for (size_t n = 0; n < m_Nodes.size(); ++n) {
        Node& node = m_Nodes[n];
        for (int t = 0; t < SKILL_NEED_SKILL_CNT; ++t) {
            const int iNeed = SKILL_NEED_SKILL_INDEX(node.iBase, t);
            if (iNeed <= 0)
                continue;
            std::map<int, int>::iterator it = ByBase.find(BaseOf(iNeed));
            if (it == ByBase.end() || it->second == (int)n || Has(node.Needs, it->second))
                continue;
            node.Needs.push_back(it->second);
            node.NeedRanks.push_back(max(1, SKILL_NEDD_SKILL_LEVEL(node.iBase, t)));
        }
    }

    Layout();

    /// The filter: the whole class, or one second job ( yours, once chosen ).
    m_Jobs.clear();
    if (iFamily > 0) {
        JobVM all = {0, "All", false};
        JobVM a = {iSecondA, JobName(iSecondA), false};
        JobVM b = {iSecondB, JobName(iSecondB), false};
        m_Jobs.push_back(all);
        m_Jobs.push_back(a);
        m_Jobs.push_back(b);
    }
    int iFilter = 0;
    if (iJob % 100 >= 21)
        iFilter = iFamily * 100 + 20 + (iJob % 10);
    m_iFilter = -1;
    SetFilter(iFilter);

    m_bHasTree = !m_Nodes.empty();
    m_strTitle = iFamily > 0 ? JobName(iFirst) : Rml::String();
    m_Model.DirtyVariable("jobs");
    m_Model.DirtyVariable("has_tree");
    m_Model.DirtyVariable("title");
}

int
RoseRmlSkillTree::DepthOf(int iNode, std::vector<int>& State) {
    if (State[iNode] == 2)
        return m_Nodes[iNode].iCol;
    if (State[iNode] == 1)
        return 0; /// a cycle in the data: stop here
    State[iNode] = 1;
    int iCol = 0;
    for (size_t i = 0; i < m_Nodes[iNode].Needs.size(); ++i)
        iCol = max(iCol, DepthOf(m_Nodes[iNode].Needs[i], State) + 1);
    m_Nodes[iNode].iCol = iCol;
    State[iNode] = 2;
    return iCol;
}

/// A tidy tree: a leaf takes the next row, a parent sits on its first
/// child's row, so a chain reads left to right on one line.
int
RoseRmlSkillTree::PlaceRows(int iNode, int& iNextRow) {
    Node& node = m_Nodes[iNode];
    if (node.Children.empty()) {
        node.iRow = iNextRow++;
        return node.iRow;
    }
    int iFirstRow = -1;
    for (size_t i = 0; i < node.Children.size(); ++i) {
        const int iRow = PlaceRows(node.Children[i], iNextRow);
        if (iFirstRow < 0)
            iFirstRow = iRow;
    }
    m_Nodes[iNode].iRow = iFirstRow;
    return iFirstRow;
}

void
RoseRmlSkillTree::Layout() {
    const int iCount = (int)m_Nodes.size();

    std::vector<int> State(iCount, 0);
    for (int i = 0; i < iCount; ++i)
        DepthOf(i, State);

    /// A node hangs under its first prerequisite ( its lane ).
    for (int i = 0; i < iCount; ++i)
        m_Nodes[i].Children.clear();
    for (int i = 0; i < iCount; ++i) {
        if (!m_Nodes[i].Needs.empty())
            m_Nodes[m_Nodes[i].Needs[0]].Children.push_back(i);
    }

    /// Lanes: each root others hang from, in table order; then the lone ones.
    std::vector<int> Roots, Lone;
    for (int i = 0; i < iCount; ++i) {
        if (!m_Nodes[i].Needs.empty())
            continue;
        if (m_Nodes[i].Children.empty())
            Lone.push_back(i);
        else
            Roots.push_back(i);
    }

    m_LaneVMs.clear();
    int iNextRow = 0;
    int iLane = 0;
    float fBottom = kPad;
    for (size_t r = 0; r < Roots.size(); ++r, ++iLane) {
        const int iFirstRow = iNextRow;
        PlaceRows(Roots[r], iNextRow);
        /// Everything placed under this root is in this lane.
        std::vector<int> Stack(1, Roots[r]);
        while (!Stack.empty()) {
            const int n = Stack.back();
            Stack.pop_back();
            m_Nodes[n].iLane = iLane;
            for (size_t c = 0; c < m_Nodes[n].Children.size(); ++c)
                Stack.push_back(m_Nodes[n].Children[c]);
        }
        LaneVM lane;
        lane.y = kPad + iFirstRow * kRowPitch + iLane * kLaneGap - kLaneGap * 0.5f;
        lane.h = (iNextRow - iFirstRow) * kRowPitch - (kRowPitch - kIcon) + kLaneGap;
        lane.odd = (iLane % 2) == 1;
        m_LaneVMs.push_back(lane);
        fBottom = lane.y + lane.h;
    }
    if (!Lone.empty()) {
        const int iFirstRow = iNextRow;
        for (size_t i = 0; i < Lone.size(); ++i) {
            Node& node = m_Nodes[Lone[i]];
            node.iLane = iLane;
            node.iCol = (int)i % kLoneColumns;
            node.iRow = iFirstRow + (int)i / kLoneColumns;
        }
        iNextRow = iFirstRow + ((int)Lone.size() + kLoneColumns - 1) / kLoneColumns;
        LaneVM lane;
        lane.y = kPad + iFirstRow * kRowPitch + iLane * kLaneGap - kLaneGap * 0.5f;
        lane.h = (iNextRow - iFirstRow) * kRowPitch - (kRowPitch - kIcon) + kLaneGap;
        lane.odd = (iLane % 2) == 1;
        m_LaneVMs.push_back(lane);
        fBottom = lane.y + lane.h;
        ++iLane;
    }

    /// Positions.
    int iMaxCol = 0;
    m_NodeVMs.clear();
    for (int i = 0; i < iCount; ++i) {
        const Node& node = m_Nodes[i];
        iMaxCol = max(iMaxCol, node.iCol);
        NodeVM vm;
        vm.index = i;
        vm.x = kPad + node.iCol * kColPitch;
        vm.y = kPad + node.iRow * kRowPitch + node.iLane * kLaneGap;
        vm.learned = vm.ready = vm.locked = vm.dim = vm.on = false;
        m_NodeVMs.push_back(vm);
    }

    /// Connectors: square elbows, the upright in the gap right of the
    /// prerequisite, then along the skill's own row ( which holds nothing
    /// left of it in its subtree ).
    for (int i = 0; i < iCount; ++i) {
        const Node& node = m_Nodes[i];
        const NodeVM& to = m_NodeVMs[i];
        const float fToY = to.y + kIcon * 0.5f;
        for (size_t k = 0; k < node.Needs.size(); ++k) {
            const NodeVM& from = m_NodeVMs[node.Needs[k]];
            const float fFromX = from.x + kNodeW;
            const float fFromY = from.y + kIcon * 0.5f;
            Seg seg;
            seg.iFrom = node.Needs[k];
            seg.iRank = node.NeedRanks[k];
            seg.iTo = i;
            if (fabsf(fFromY - fToY) < 0.5f) {
                seg.x = fFromX;
                seg.y = fToY - kLine * 0.5f;
                seg.w = max(kLine, to.x - fFromX);
                seg.h = kLine;
                m_Segs.push_back(seg);
                continue;
            }
            const float fUpX = fFromX + floorf((kColPitch - kNodeW) * 0.5f);
            seg.x = fFromX;
            seg.y = fFromY - kLine * 0.5f;
            seg.w = fUpX - fFromX + kLine;
            seg.h = kLine;
            m_Segs.push_back(seg);
            seg.x = fUpX;
            seg.y = min(fFromY, fToY) - kLine * 0.5f;
            seg.w = kLine;
            seg.h = fabsf(fToY - fFromY) + kLine;
            m_Segs.push_back(seg);
            seg.x = fUpX;
            seg.y = fToY - kLine * 0.5f;
            seg.w = max(kLine, to.x - fUpX);
            seg.h = kLine;
            m_Segs.push_back(seg);
        }
    }
    m_SegVMs.clear();
    for (size_t s = 0; s < m_Segs.size(); ++s) {
        SegVM vm = {m_Segs[s].x, m_Segs[s].y, m_Segs[s].w, m_Segs[s].h, false, false};
        m_SegVMs.push_back(vm);
    }

    m_fWidth = kPad * 2.0f + iMaxCol * kColPitch + kNodeW;
    m_fHeight = max(fBottom, kPad + iNextRow * kRowPitch + iLane * kLaneGap) + kPad;
    m_Model.DirtyVariable("nodes");
    m_Model.DirtyVariable("segs");
    m_Model.DirtyVariable("lanes");
    m_Model.DirtyVariable("width");
    m_Model.DirtyVariable("height");
}

/// --- state ------------------------------------------------------------------------

int
RoseRmlSkillTree::LearnedRank(int iBase, int* piSlot) const {
    if (piSlot)
        *piSlot = -1;
    if (g_pAVATAR == NULL)
        return 0;
    CSkillSlot* pSlots = g_pAVATAR->GetSkillSlot();
    CSkill* pSkill = pSlots ? pSlots->GetSkillByBaseSkillIDX(iBase) : NULL;
    if (pSkill == NULL)
        return 0;
    if (piSlot)
        *piSlot = pSkill->GetSkillSlot();
    return pSkill->GetSkillLevel();
}

bool
RoseRmlSkillTree::JobShown(const Node& node) const {
    if (m_iFilter <= 0)
        return true;
    const int iBit = (m_iFilter % 10 == 1) ? 1 : 2;
    return (node.iJobMask & iBit) != 0;
}

void
RoseRmlSkillTree::SetFilter(int iJob) {
    if (iJob == m_iFilter)
        return;
    m_iFilter = iJob;
    for (size_t i = 0; i < m_Jobs.size(); ++i)
        m_Jobs[i].on = m_Jobs[i].job == iJob;
    m_Model.DirtyVariable("jobs");
}

void
RoseRmlSkillTree::Sample() {
    if (g_pAVATAR == NULL)
        return;

    const int iFamily = m_iBuiltJob / 100;
    std::vector<int> Ranks(m_Nodes.size(), 0);
    for (size_t i = 0; i < m_Nodes.size(); ++i)
        Ranks[i] = LearnedRank(m_Nodes[i].iBase);

    std::vector<NodeVM> nodes = m_NodeVMs;
    for (size_t i = 0; i < m_Nodes.size() && i < nodes.size(); ++i) {
        const Node& node = m_Nodes[i];
        NodeVM& vm = nodes[i];
        const int iRank = Ranks[i];
        /// The rank you have names the node ( Twin Shot becomes Triple Shot ).
        const int iRow = iRank > 0 ? node.iBase + min(iRank, node.iMaxRank) - 1 : node.iBase;
        vm.src.clear();
        vm.rect.clear();
        RoseRmlIcons::Resolve(IMAGE_RES_SKILL_ICON, SKILL_ICON_NO(iRow), vm.src, vm.rect);
        vm.name = GameName(iRow);
        vm.rank = iRank > 0 ? Rml::String(CStr::Printf("%d/%d", iRank, node.iMaxRank)) : Rml::String();
        vm.tag.clear();
        if (node.iJobMask == 1)
            vm.tag = JobName(iFamily * 100 + 21);
        else if (node.iJobMask == 2)
            vm.tag = JobName(iFamily * 100 + 22);

        vm.learned = iRank > 0;
        bool bMet = g_pAVATAR->Check_JobCollection((short)SKILL_AVAILBLE_CLASS_SET(node.iBase));
        for (size_t k = 0; k < node.Needs.size() && bMet; ++k)
            bMet = Ranks[node.Needs[k]] >= node.NeedRanks[k];
        for (int t = 0; t < SKILL_NEED_ABILITY_TYPE_CNT && bMet; ++t) {
            const int iType = SKILL_NEED_ABILITY_TYPE(node.iBase, t);
            if (iType)
                bMet = g_pAVATAR->Get_AbilityValue(iType) >= SKILL_NEED_ABILITY_VALUE(node.iBase, t);
        }
        vm.ready = !vm.learned && bMet;
        vm.locked = !vm.learned && !bMet;
        vm.dim = !JobShown(node);
        vm.on = (int)i == m_iSelected;
    }
    if (nodes != m_NodeVMs) {
        m_NodeVMs.swap(nodes);
        m_Model.DirtyVariable("nodes");
    }

    std::vector<SegVM> segs = m_SegVMs;
    for (size_t s = 0; s < m_Segs.size() && s < segs.size(); ++s) {
        segs[s].lit = Ranks[m_Segs[s].iFrom] >= m_Segs[s].iRank;
        segs[s].dim = !JobShown(m_Nodes[m_Segs[s].iTo]);
    }
    if (segs != m_SegVMs) {
        m_SegVMs.swap(segs);
        m_Model.DirtyVariable("segs");
    }

    SampleDetails();
}

void
RoseRmlSkillTree::SampleDetails() {
    const bool bSel = m_iSelected >= 0 && m_iSelected < (int)m_Nodes.size();
    if (bSel != m_bHasSel) {
        m_bHasSel = bSel;
        m_Model.DirtyVariable("has_sel");
    }
    if (!bSel)
        return;

    const Node& node = m_Nodes[m_iSelected];
    const int iFamily = m_iBuiltJob / 100;
    const int iRank = LearnedRank(node.iBase);
    const int iShown = iRank > 0 ? node.iBase + min(iRank, node.iMaxRank) - 1 : node.iBase;

    const Rml::String strName = GameName(iShown);
    const Rml::String strRank = iRank > 0
        ? Rml::String(CStr::Printf("Rank %d / %d", iRank, node.iMaxRank))
        : Rml::String(CStr::Printf("Not learned  -  %d ranks", node.iMaxRank));
    Rml::String strTag;
    if (node.iJobMask == 1)
        strTag = JobName(iFamily * 100 + 21) + " skill";
    else if (node.iJobMask == 2)
        strTag = JobName(iFamily * 100 + 22) + " skill";
    const Rml::String strDesc = ToRml(CStringManager::GetSingleton().GetSkillDesc(iShown));

    /// A rank that renames the skill ( the classic art drew it as two boxes ).
    Rml::String strNote;
    for (int r = 2; r <= node.iMaxRank; ++r) {
        const Rml::String strPrev = GameName(node.iBase + r - 2);
        const Rml::String strThis = GameName(node.iBase + r - 1);
        if (!strThis.empty() && strThis != strPrev) {
            strNote = CStr::Printf("Becomes %s at rank %d.", strThis.c_str(), r);
            break;
        }
    }

    /// What the next rank asks ( rank 1 when not learned ): the row's own
    /// prerequisites, stats and jobs.
    std::vector<ReqVM> reqs;
    Rml::String strHead;
    if (iRank >= node.iMaxRank) {
        strHead = "Mastered.";
    } else {
        const int iRow = node.iBase + iRank; /// the next rank's row
        strHead = iRank > 0 ? Rml::String(CStr::Printf("Rank %d needs", iRank + 1)) : Rml::String("To learn it you need");
        for (int t = 0; t < SKILL_NEED_SKILL_CNT; ++t) {
            const int iNeed = SKILL_NEED_SKILL_INDEX(iRow, t);
            if (iNeed <= 0)
                continue;
            const int iNeedBase = BaseOf(iNeed);
            const int iNeedRank = max(1, SKILL_NEDD_SKILL_LEVEL(iRow, t));
            ReqVM req;
            req.text = GameName(iNeedBase) + Rml::String(CStr::Printf(" rank %d", iNeedRank));
            req.ok = LearnedRank(iNeedBase) >= iNeedRank;
            reqs.push_back(req);
        }
        for (int t = 0; t < SKILL_NEED_ABILITY_TYPE_CNT; ++t) {
            const int iType = SKILL_NEED_ABILITY_TYPE(iRow, t);
            if (!iType)
                continue;
            const int iValue = SKILL_NEED_ABILITY_VALUE(iRow, t);
            const char* pszAbility = CStringManager::GetSingleton().GetAbility(iType);
            ReqVM req;
            req.text = Rml::String(CStr::Printf("%s %d", pszAbility ? pszAbility : "?", iValue));
            req.ok = g_pAVATAR->Get_AbilityValue(iType) >= iValue;
            reqs.push_back(req);
        }
        /// The jobs, when not the whole class ( ranks can narrow them ).
        std::vector<int> Jobs;
        JobsOf(SKILL_AVAILBLE_CLASS_SET(iRow), Jobs);
        if (!Jobs.empty() && !Has(Jobs, iFamily * 100 + 11)) {
            Rml::String strJobs;
            for (size_t j = 0; j < Jobs.size(); ++j) {
                const int iJob = Jobs[j];
                if (iJob / 100 != iFamily || iJob % 100 >= 30)
                    continue; /// third jobs: not in this game
                if (!strJobs.empty())
                    strJobs += " or ";
                strJobs += JobName(iJob);
            }
            if (!strJobs.empty()) {
                ReqVM req;
                req.text = "Job: " + strJobs;
                req.ok = g_pAVATAR->Check_JobCollection((short)SKILL_AVAILBLE_CLASS_SET(iRow));
                reqs.push_back(req);
            }
        }
        if (reqs.empty()) {
            ReqVM req = {"Nothing else", true};
            reqs.push_back(req);
        }
    }

    struct { Rml::String* pDst; const Rml::String* pSrc; const char* pszVar; } strs[] = {
        {&m_strSelName, &strName, "sel_name"},
        {&m_strSelRank, &strRank, "sel_rank"},
        {&m_strSelTag, &strTag, "sel_tag"},
        {&m_strSelDesc, &strDesc, "sel_desc"},
        {&m_strSelNote, &strNote, "sel_note"},
        {&m_strReqHead, &strHead, "req_head"},
    };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        if (*strs[i].pDst != *strs[i].pSrc) {
            *strs[i].pDst = *strs[i].pSrc;
            m_Model.DirtyVariable(strs[i].pszVar);
        }
    }
    if (reqs != m_Reqs) {
        m_Reqs.swap(reqs);
        m_Model.DirtyVariable("reqs");
    }
}

/// --- input --------------------------------------------------------------------------

void
RoseRmlSkillTree::UpdateDragStart() {
    if (m_iPressNode < 0)
        return;

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_iPressNode = -1;
        return;
    }

    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iSlopX = max(3, g_pCApp->GetWIDTH() / 200);
    const int iSlopY = max(3, g_pCApp->GetHEIGHT() / 200);
    if (abs(ptMouse.x - m_iPressX) < iSlopX && abs(ptMouse.y - m_iPressY) < iSlopY)
        return;

    const int iNode = m_iPressNode;
    m_iPressNode = -1;
    if (iNode < 0 || iNode >= (int)m_Nodes.size() || m_Nodes[iNode].bPassive || m_pDragItem == NULL)
        return;

    /// Only a learned skill goes on a bar.
    int iSlot = -1;
    if (LearnedRank(m_Nodes[iNode].iBase, &iSlot) <= 0 || iSlot < 0)
        return;
    CIconSkill icon(iSlot);
    if (icon.GetSkill() == NULL)
        return;
    m_pDragItem->SetIcon(&icon); /// stores a clone
    CDragNDropMgr::GetInstance().DragStart(m_pDragItem);
}

void
RoseRmlSkillTree::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iNode = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("tnode")) {
            iNode = pEl->GetAttribute<int>("tnode", -1);
            break;
        }
    }
    if (iNode < 0 || iNode >= (int)m_Nodes.size())
        return;

    /// The classic tree's tooltip: the learned skill, or the rank-1 row.
    CInfo ToolTip;
    ToolTip.Clear();
    int iSlot = -1;
    if (LearnedRank(m_Nodes[iNode].iBase, &iSlot) > 0 && iSlot >= 0) {
        CIconSkill icon(iSlot);
        icon.GetToolTip(ToolTip, DLG_TYPE_SKILLTREE, INFO_STATUS_DETAIL);
    } else {
        CIconSkillDummy icon(m_Nodes[iNode].iBase);
        icon.GetToolTip(ToolTip, DLG_TYPE_SKILLTREE, INFO_STATUS_DETAIL);
    }
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlSkillTree::PlaceDefault() {
    /// Centred, a little high -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fLeft = floorf(max(0.0f, ((float)view.x - size.x) * 0.5f));
    const float fTop = floorf(max(0.0f, ((float)view.y - size.y) * 0.35f));
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlSkillTree::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen && !IsInWorld())
        SetOpen(false);

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    /// A job change ( the second-job quest ) rebuilds the tree.
    if (g_pAVATAR->Get_JOB() != m_iBuiltJob)
        Build();

    Sample();
    PlaceDefault();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}
