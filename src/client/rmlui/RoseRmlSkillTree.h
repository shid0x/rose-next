#ifndef _ROSE_RML_SKILL_TREE_H_
#define _ROSE_RML_SKILL_TREE_H_

/**
 * UI2 skill tree: replaces CSkillTreeDlg ( DLG_TYPE_SKILLTREE, opened from the
 * skill window ).
 *
 * **Generated from LIST_SKILL, nothing hand-placed.** The classic tree was a
 * per-class XML of icon coordinates over painted DDS pages holding the boxes
 * and connectors, so a new skill meant repainting art ( doc/skill-tree-art.md )
 * -- and 15 of the classes' skills were never painted in ( every Cleric buff,
 * Vanish, two Dealer crafts ). Here:
 *  - the skills are the rank-1 rows whose class set ( col 35 -> LIST_CLASS )
 *    names a job of your family; class-less ones ( emotes, Monster Inspector )
 *    stay out;
 *  - the connectors are the prerequisites ( cols 39-44, skill + rank ), which
 *    hold every line the classic art drew plus the second prerequisites it
 *    left out;
 *  - a skill with no prerequisite that others need starts a lane ( Weapon
 *    Mastery, Meditation ... ); lone ones share a last lane;
 *  - a skill's column is its prerequisite depth, its lane its first
 *    prerequisite's; rows are laid out as a tidy tree, and the connectors are
 *    square elbows drawn as thin boxes.
 * A new skill row therefore shows up in the right place with its lines.
 *
 * Each node: learned ( rank x / y ), ready ( requirements met -- the book is
 * yours to find ), locked, and dimmed when it belongs to the other second job
 * than the filter shows ( the filter starts on your own second job ). A line
 * lights when its requirement is met. Click a skill for its details and what
 * it needs; hover for the skill tooltip; double-click a learned skill to use
 * it, drag it to the skill bar. Ranks are raised in the skill window, as
 * before.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class CDragItem;

class RoseRmlSkillTree {
public:
    RoseRmlSkillTree();
    ~RoseRmlSkillTree();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    struct NodeVM {
        int index; ///< into m_Nodes
        float x;
        float y;
        Rml::String src;
        Rml::String rect;
        Rml::String name;
        Rml::String rank; ///< "3/10", "" when not learned
        Rml::String tag; ///< the second job it belongs to, "" = the whole class
        bool learned;
        bool ready;
        bool locked;
        bool dim;
        bool on; ///< selected

        bool operator==(const NodeVM& o) const {
            return index == o.index && x == o.x && y == o.y && src == o.src && rect == o.rect
                && name == o.name && rank == o.rank && tag == o.tag && learned == o.learned
                && ready == o.ready && locked == o.locked && dim == o.dim && on == o.on;
        }
        bool operator!=(const NodeVM& o) const { return !(*this == o); }
    };

    struct SegVM {
        float x;
        float y;
        float w;
        float h;
        bool lit;
        bool dim;

        bool operator==(const SegVM& o) const {
            return x == o.x && y == o.y && w == o.w && h == o.h && lit == o.lit && dim == o.dim;
        }
        bool operator!=(const SegVM& o) const { return !(*this == o); }
    };

    struct LaneVM {
        float y;
        float h;
        bool odd;

        bool operator==(const LaneVM& o) const { return y == o.y && h == o.h && odd == o.odd; }
        bool operator!=(const LaneVM& o) const { return !(*this == o); }
    };

    struct JobVM {
        int job; ///< 0 = all
        Rml::String name;
        bool on;

        bool operator==(const JobVM& o) const { return job == o.job && name == o.name && on == o.on; }
        bool operator!=(const JobVM& o) const { return !(*this == o); }
    };

    struct ReqVM {
        Rml::String text;
        bool ok;

        bool operator==(const ReqVM& o) const { return text == o.text && ok == o.ok; }
        bool operator!=(const ReqVM& o) const { return !(*this == o); }
    };

private:
    /// One skill line of the tree ( built once per job ).
    struct Node {
        int iBase; ///< its rank-1 row
        int iMaxRank;
        int iJobMask; ///< second jobs it is for: bit 0 = X21, bit 1 = X22
        bool bPassive;
        std::vector<int> Needs; ///< prerequisite nodes ( indices into m_Nodes )
        std::vector<int> NeedRanks;
        std::vector<int> Children; ///< nodes whose FIRST prerequisite this is
        int iCol;
        int iRow; ///< global row
        int iLane;
    };
    struct Seg {
        float x, y, w, h;
        int iFrom; ///< the prerequisite node
        int iRank; ///< the rank it asks for
        int iTo;
    };

    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void Build();
    void Layout();
    int DepthOf(int iNode, std::vector<int>& State);
    int PlaceRows(int iNode, int& iNextRow);
    int LearnedRank(int iBase, int* piSlot = NULL) const;
    bool JobShown(const Node& node) const;
    void Sample();
    void SampleDetails();
    void SetFilter(int iJob);
    void UpdateDragStart();
    void UpdateTooltip();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    int m_iBuiltJob; ///< the job the tree was built for, -1 = none

    std::vector<Node> m_Nodes;
    std::vector<Seg> m_Segs;
    CDragItem* m_pDragItem;
    int m_iPressNode; ///< -1 = none
    int m_iPressX;
    int m_iPressY;
    int m_iSelected; ///< node, -1 = none

    /// --- bound to skilltree.rml ----------------------------------------------
    bool m_bHasTree;
    Rml::String m_strTitle;
    float m_fWidth; ///< the canvas
    float m_fHeight;
    std::vector<NodeVM> m_NodeVMs;
    std::vector<SegVM> m_SegVMs;
    std::vector<LaneVM> m_LaneVMs;
    std::vector<JobVM> m_Jobs;
    int m_iFilter; ///< 0 = all, else a second job id

    bool m_bHasSel;
    Rml::String m_strSelName;
    Rml::String m_strSelRank;
    Rml::String m_strSelTag;
    Rml::String m_strSelDesc; ///< RML
    Rml::String m_strSelNote;
    Rml::String m_strReqHead; ///< "To learn" / "Next rank" / "Mastered"
    std::vector<ReqVM> m_Reqs;
};

#endif /// _ROSE_RML_SKILL_TREE_H_
