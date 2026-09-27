#ifndef _ROSE_RML_CLAN_H_
#define _ROSE_RML_CLAN_H_

/**
 * UI2 clan window: replaces CClanDlg ( DLG_TYPE_CLAN ) and the notice box
 * CClanRegistNotice ( DLG_TYPE_CLAN_NOTICE ).
 *
 * Four tabs, as the classic window:
 *  - Info: the emblem, level, points, members, zuly, slogan, ally, your rank
 *    and contribution; the custom mark ( Mark.bmp in the game folder: Preview
 *    for anyone, Register for the master ).
 *  - Members: every member with rank, level, job and contribution in columns
 *    ( classic showed them only in a tooltip, or after a click ). Promote,
 *    Demote, Kick and Hand over for the master, Invite ( your target ) from
 *    sub-master up, Leave -- now behind a question -- for everyone else.
 *  - Skills: the clan skills, double-click to use, drag onto the skill bar.
 *  - Notice: the clan notice; the master edits it in place ( the classic
 *    notice box, whose Delete button did nothing, is gone ).
 * Not in a clan: says so, and how to found one.
 *
 * **The member list is the hidden CClanDlg's**: CClan keeps only names and
 * ranks, while contribution, channel, level and job live on the classic
 * list's items, which the clan events keep up whether it shows or not ( and
 * which still write the members' log-in / log-out chat lines ). The actions
 * are CClanDlg's own ( RequestPromote & co ), so the checks and questions are
 * the classic ones. Clan skills come straight from CClan's skill slots.
 *
 * The emblem is the two sprite sheets ( CLANBACK / CLANCENTER ) or, when the
 * clan registered its own mark, the bitmap the client keeps in clanmark\ --
 * loaded from disk by full path and released when it changes.
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

class CClanDlg;
class CDragItem;

class RoseRmlClan {
public:
    RoseRmlClan();
    ~RoseRmlClan();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    /// DLG_TYPE_CLAN_NOTICE: the Notice tab, editing.
    void EditNotice();

    void Update();

    struct MemberVM {
        int index;
        bool used; ///< rows are never removed, only hidden
        bool on; ///< selected
        bool online;
        bool me;
        bool penalty; ///< rank 0
        Rml::String name;
        Rml::String rank;
        Rml::String level; ///< "-" while offline
        Rml::String job;
        Rml::String contrib;

        bool operator==(const MemberVM& o) const {
            return index == o.index && used == o.used && on == o.on && online == o.online
                && me == o.me && penalty == o.penalty && name == o.name && rank == o.rank
                && level == o.level && job == o.job && contrib == o.contrib;
        }
        bool operator!=(const MemberVM& o) const { return !(*this == o); }
    };

    struct SkillVM {
        int slot;
        bool used;
        Rml::String src;
        Rml::String rect;
        Rml::String name;
        int level;
        Rml::String expires; ///< "" = does not expire
        bool passive;
        float cd; ///< cooldown, whole percent ( the curtain's scaleY )

        bool operator==(const SkillVM& o) const {
            return slot == o.slot && used == o.used && src == o.src && rect == o.rect
                && name == o.name && level == o.level && expires == o.expires
                && passive == o.passive && cd == o.cd;
        }
        bool operator!=(const SkillVM& o) const { return !(*this == o); }
    };

private:
    CClanDlg* Dlg() const;
    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void SetTab(int iTab);
    void PlaceDefault();

    void Sample();
    void SampleInfo();
    void SampleMembers();
    void SampleSkills();
    void SampleNotice();

    void OnAction(int iAction);
    void OnPreviewMark();
    void OnEditNotice();
    void OnSaveNotice();
    void OnCancelNotice();
    void UpdateDragStart();
    void UpdateTooltip();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    bool m_bFocusNotice; ///< put the caret in the notice once shown
    std::vector<std::string> m_RowNames; ///< game-text name of each shown row
    std::string m_strSelected; ///< game-text name, "" = none
    std::string m_strCustomSrc; ///< the custom emblem's file ( loaded texture )
    unsigned __int64 m_ullCustomStamp; ///< its last write time, to reload it
    std::string m_strPreviewSrc; ///< Mark.bmp, once previewed

    CDragItem* m_pDragItem; ///< clan skill -> skill bar
    int m_iPressSlot; ///< skill pressed, -1 = none
    int m_iPressX;
    int m_iPressY;

    /// --- bound to clan.rml ---------------------------------------------------
    int m_iTab; ///< 0 info, 1 members, 2 skills, 3 notice
    int m_iShown; ///< the tab shown, -1 while not in a clan
    bool m_bInClan;
    bool m_bMaster;
    bool m_bCanInvite;
    Rml::String m_strName;
    Rml::String m_strSlogan;
    int m_iLevel;
    Rml::String m_strPoints;
    Rml::String m_strMoney;
    Rml::String m_strMemberCount; ///< "12 / 20"
    Rml::String m_strAlly;
    Rml::String m_strMyRank;
    bool m_bMyPenalty;
    Rml::String m_strMyContrib;
    Rml::String m_strBadgeSrc; ///< your rank's badge
    Rml::String m_strBadgeRect;
    int m_iMarkKind; ///< 0 none, 1 sprites, 2 custom
    Rml::String m_strBackSrc;
    Rml::String m_strBackRect;
    Rml::String m_strCenterSrc;
    Rml::String m_strCenterRect;
    Rml::String m_strCustomBound; ///< m_strCustomSrc, bound
    bool m_bHasPreview;
    Rml::String m_strPreviewBound;

    std::vector<MemberVM> m_Members;
    int m_iOnline;
    int m_iTotal;
    bool m_bHasSelection;
    bool m_bCanUp; ///< the selected member, by the classic rules
    bool m_bCanDown;
    bool m_bCanKick;
    bool m_bCanEntrust;

    std::vector<SkillVM> m_Skills;
    int m_iSkillCount;

    Rml::String m_strNoticeRml; ///< escaped, line breaks as <br/>
    bool m_bHasNotice;
    bool m_bEditing;
    bool m_bCanEdit; ///< master, not editing ( the Edit button )
};

#endif /// _ROSE_RML_CLAN_H_
