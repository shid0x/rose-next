#include "stdafx.h"

#include "RoseRmlClan.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../CObjUSER.h"
#include "../Game.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../GameCommon/Skill.h"
#include "../GameCommon/StringManager.h"
#include "../GameData/CClan.h"
#include "../Network/CNetwork.h"
#include "../interface/CClanMarkUserDefined.h"
#include "../interface/ClanMarkTransfer.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/Icon/CIconSkillClan.h"
#include "../interface/SlotContainer/ClanSkillSlot.h"
#include "../interface/TypeResource.h"
#include "../interface/command/dragcommand.h"
#include "../interface/Dlgs/CClanDlg.h"
#include "../interface/Dlgs/subclass/ClanMemberItem.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../util/classTime.h"
#include "tgamectrl/resourcemgr.h"
#include "tgamectrl/teditbox.h"

#include "rose/common/log.h"

#include <algorithm>
#include <math.h>

namespace {

enum {
    TAB_INFO = 0,
    TAB_MEMBERS = 1,
    TAB_SKILLS = 2,
    TAB_NOTICE = 3,
};

/// The member tab's buttons ( clan.rml passes these ).
enum {
    ACT_PROMOTE = 0,
    ACT_DEMOTE = 1,
    ACT_KICK = 2,
    ACT_ENTRUST = 3,
    ACT_INVITE = 4,
    ACT_LEAVE = 5,
    ACT_REGISTER_MARK = 6,
};

/// CClanMemberItem's "offline" channel.
const int kOffline = 0xff;

/// "12,345,678".
Rml::String
Thousands(__int64 iValue) {
    char szRaw[32];
    _snprintf(szRaw, sizeof(szRaw), "%I64d", iValue < 0 ? -iValue : iValue);
    szRaw[sizeof(szRaw) - 1] = '\0';

    const int iLen = (int)strlen(szRaw);
    Rml::String out = iValue < 0 ? "-" : "";
    for (int i = 0; i < iLen; ++i) {
        if (i > 0 && ((iLen - i) % 3) == 0)
            out += ',';
        out += szRaw[i];
    }
    return out;
}

/// UTF-8 ( a field's value ) -> the game's code page, for the server.
std::string
ToGame(const Rml::String& strText) {
    if (strText.empty())
        return std::string();
    const int iWide = MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), -1, NULL, 0);
    if (iWide <= 0)
        return strText;
    std::wstring wide((size_t)iWide, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), -1, &wide[0], iWide);
    const int iAnsi = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, NULL, 0, NULL, NULL);
    if (iAnsi <= 0)
        return strText;
    std::string out((size_t)iAnsi, '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, &out[0], iAnsi, NULL, NULL);
    out.resize((size_t)iAnsi - 1);
    return out;
}

/// A file the game wrote ( a clan mark ), by full path, and when it was last
/// written; false when it is not there.
bool
DiskFile(const std::string& strRelative, std::string& strFull, unsigned __int64& ullStamp) {
    char szFull[MAX_PATH];
    const DWORD dwLen = GetFullPathNameA(strRelative.c_str(), MAX_PATH, szFull, NULL);
    if (dwLen == 0 || dwLen >= MAX_PATH)
        return false;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(szFull, GetFileExInfoStandard, &data))
        return false;
    strFull = szFull;
    ullStamp = ((unsigned __int64)data.ftLastWriteTime.dwHighDateTime << 32)
        | data.ftLastWriteTime.dwLowDateTime;
    return true;
}

bool
IsMe(const char* pszName) {
    return g_pAVATAR != NULL && pszName != NULL && strcmpi(pszName, g_pAVATAR->Get_NAME()) == 0;
}

struct MemberRow {
    std::string strName;
    int iClass;
    int iPoint;
    int iChannel;
    int iLevel;
    int iJob;
};

/// Highest rank first ( as the classic list ), then who is on, then by name.
bool
MemberOrder(const MemberRow& a, const MemberRow& b) {
    if (a.iClass != b.iClass)
        return a.iClass > b.iClass;
    const bool bA = a.iChannel != kOffline, bB = b.iChannel != kOffline;
    if (bA != bB)
        return bA;
    return _stricmp(a.strName.c_str(), b.strName.c_str()) < 0;
}

} // namespace

RoseRmlClan::RoseRmlClan():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bFocusNotice(false),
    m_ullCustomStamp(0),
    m_pDragItem(NULL),
    m_iPressSlot(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iTab(TAB_INFO),
    m_iShown(-1),
    m_bInClan(false),
    m_bMaster(false),
    m_bCanInvite(false),
    m_iLevel(0),
    m_bMyPenalty(false),
    m_iMarkKind(0),
    m_bHasPreview(false),
    m_iOnline(0),
    m_iTotal(0),
    m_bHasSelection(false),
    m_bCanUp(false),
    m_bCanDown(false),
    m_bCanKick(false),
    m_bCanEntrust(false),
    m_iSkillCount(0),
    m_bHasNotice(false),
    m_bEditing(false),
    m_bCanEdit(false) {}

RoseRmlClan::~RoseRmlClan() {
    delete m_pDragItem; /// deletes its commands too
}

bool
RoseRmlClan::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// CClanSkillListItem's drag, onto either skill bar row.
    m_pDragItem = new CDragItem;
    m_pDragItem->AddTarget(DLG_TYPE_QUICKBAR, new CTCmdDragClanSkill2QuickBar(DLG_TYPE_QUICKBAR));
    m_pDragItem->AddTarget(
        DLG_TYPE_QUICKBAR_EXT, new CTCmdDragClanSkill2QuickBar(DLG_TYPE_QUICKBAR_EXT));

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("clan");
    if (!constructor)
        return false;

    if (auto row = constructor.RegisterStruct<MemberVM>()) {
        row.RegisterMember("index", &MemberVM::index);
        row.RegisterMember("used", &MemberVM::used);
        row.RegisterMember("on", &MemberVM::on);
        row.RegisterMember("online", &MemberVM::online);
        row.RegisterMember("me", &MemberVM::me);
        row.RegisterMember("penalty", &MemberVM::penalty);
        row.RegisterMember("name", &MemberVM::name);
        row.RegisterMember("rank", &MemberVM::rank);
        row.RegisterMember("level", &MemberVM::level);
        row.RegisterMember("job", &MemberVM::job);
        row.RegisterMember("contrib", &MemberVM::contrib);
    }
    constructor.RegisterArray<std::vector<MemberVM>>();
    if (auto skill = constructor.RegisterStruct<SkillVM>()) {
        skill.RegisterMember("slot", &SkillVM::slot);
        skill.RegisterMember("used", &SkillVM::used);
        skill.RegisterMember("src", &SkillVM::src);
        skill.RegisterMember("rect", &SkillVM::rect);
        skill.RegisterMember("name", &SkillVM::name);
        skill.RegisterMember("level", &SkillVM::level);
        skill.RegisterMember("expires", &SkillVM::expires);
        skill.RegisterMember("passive", &SkillVM::passive);
        skill.RegisterMember("cd", &SkillVM::cd);
    }
    constructor.RegisterArray<std::vector<SkillVM>>();

    constructor.Bind("tab", &m_iTab);
    constructor.Bind("shown", &m_iShown);
    constructor.Bind("in_clan", &m_bInClan);
    constructor.Bind("master", &m_bMaster);
    constructor.Bind("can_invite", &m_bCanInvite);
    constructor.Bind("name", &m_strName);
    constructor.Bind("slogan", &m_strSlogan);
    constructor.Bind("level", &m_iLevel);
    constructor.Bind("points", &m_strPoints);
    constructor.Bind("money", &m_strMoney);
    constructor.Bind("member_count", &m_strMemberCount);
    constructor.Bind("ally", &m_strAlly);
    constructor.Bind("my_rank", &m_strMyRank);
    constructor.Bind("my_penalty", &m_bMyPenalty);
    constructor.Bind("my_contrib", &m_strMyContrib);
    constructor.Bind("badge_src", &m_strBadgeSrc);
    constructor.Bind("badge_rect", &m_strBadgeRect);
    constructor.Bind("mark_kind", &m_iMarkKind);
    constructor.Bind("back_src", &m_strBackSrc);
    constructor.Bind("back_rect", &m_strBackRect);
    constructor.Bind("center_src", &m_strCenterSrc);
    constructor.Bind("center_rect", &m_strCenterRect);
    constructor.Bind("custom_src", &m_strCustomBound);
    constructor.Bind("has_preview", &m_bHasPreview);
    constructor.Bind("preview_src", &m_strPreviewBound);

    constructor.Bind("members", &m_Members);
    constructor.Bind("online", &m_iOnline);
    constructor.Bind("total", &m_iTotal);
    constructor.Bind("has_selection", &m_bHasSelection);
    constructor.Bind("can_up", &m_bCanUp);
    constructor.Bind("can_down", &m_bCanDown);
    constructor.Bind("can_kick", &m_bCanKick);
    constructor.Bind("can_entrust", &m_bCanEntrust);

    constructor.Bind("skills", &m_Skills);
    constructor.Bind("skill_count", &m_iSkillCount);

    constructor.Bind("notice_rml", &m_strNoticeRml);
    constructor.Bind("has_notice", &m_bHasNotice);
    constructor.Bind("editing", &m_bEditing);
    constructor.Bind("can_edit", &m_bCanEdit);

    constructor.BindEventCallback("set_tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                SetTab(args[0].Get<int>());
        });
    constructor.BindEventCallback("select",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int i = args[0].Get<int>();
            if (i >= 0 && i < (int)m_RowNames.size())
                m_strSelected = m_RowNames[i];
        });
    constructor.BindEventCallback("act",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                OnAction(args[0].Get<int>());
        });
    constructor.BindEventCallback("preview_mark",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnPreviewMark(); });
    constructor.BindEventCallback("edit_notice",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnEditNotice(); });
    constructor.BindEventCallback("save_notice",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnSaveNotice(); });
    constructor.BindEventCallback("cancel_notice",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnCancelNotice(); });

    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.empty() || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressSlot = args[0].Get<int>();
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
        });
    /// Double-click uses the skill, as the classic slot.
    constructor.BindEventCallback("use",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty() || g_pAVATAR == NULL)
                return;
            CIconSkillClan icon(args[0].Get<int>());
            if (icon.GetSkill() != NULL)
                icon.ExecuteCommand();
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_CLAN);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "clan.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load clan document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("clan");
    RoseRmlLayout::Track(m_pPanel, "clan");

    LOG_INFO("[rmlui] clan document loaded");
    return true;
}

void
RoseRmlClan::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CClanDlg*
RoseRmlClan::Dlg() const {
    return (CClanDlg*)g_itMGR.FindDlg(DLG_TYPE_CLAN);
}

bool
RoseRmlClan::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlClan::SetOpen(bool bOpen) {
    if (bOpen == m_bOpen)
        return;
    RoseUi2::PlayWindowSound(DLG_TYPE_CLAN, bOpen);
    m_bOpen = bOpen;
    m_iPressSlot = -1;
    if (bOpen) {
        Sample();
    } else if (m_bEditing) {
        m_bEditing = false; /// an unsaved notice is dropped, as the classic box's Cancel
        m_Model.DirtyVariable("editing");
    }
}

void
RoseRmlClan::EditNotice() {
    SetOpen(true);
    SetTab(TAB_NOTICE);
    OnEditNotice();
}

void
RoseRmlClan::SetTab(int iTab) {
    if (iTab < TAB_INFO || iTab > TAB_NOTICE || iTab == m_iTab)
        return;
    m_iTab = iTab;
    m_Model.DirtyVariable("tab");
}

void
RoseRmlClan::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressSlot = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlClan::Sample() {
    SampleInfo();
    SampleMembers();
    SampleSkills();
    SampleNotice();

    const int iShown = m_bInClan ? m_iTab : -1;
    if (iShown != m_iShown) {
        m_iShown = iShown;
        m_Model.DirtyVariable("shown");
    }
    const bool bCanEdit = m_bMaster && !m_bEditing;
    if (bCanEdit != m_bCanEdit) {
        m_bCanEdit = bCanEdit;
        m_Model.DirtyVariable("can_edit");
    }
}

void
RoseRmlClan::SampleInfo() {
    CClan& Clan = CClan::GetInstance();
    const bool bInClan = Clan.GetClanNo() != 0;
    if (bInClan != m_bInClan) {
        m_bInClan = bInClan;
        m_Model.DirtyVariable("in_clan");
    }

    const int iClass = bInClan ? Clan.GetClass() : 0;
    const bool bMaster = bInClan && iClass >= CClan::CLAN_MASTER;
    if (bMaster != m_bMaster) {
        m_bMaster = bMaster;
        m_Model.DirtyVariable("master");
    }
    const bool bCanInvite = bInClan && iClass >= CClan::CLAN_SUB_MASTER;
    if (bCanInvite != m_bCanInvite) {
        m_bCanInvite = bCanInvite;
        m_Model.DirtyVariable("can_invite");
    }

    Rml::String strName, strSlogan, strPoints, strMoney, strCount, strAlly, strRank, strContrib;
    int iLevel = 0;
    if (bInClan) {
        strName = RoseRmlText::FromGame(Clan.GetName());
        strSlogan = RoseRmlText::FromGame(Clan.GetSlogan());
        iLevel = Clan.GetLevel();
        strPoints = Thousands(Clan.GetPoint());
        strMoney = Thousands(Clan.GetMoney());
        strCount = CStr::Printf("%d / %d", Clan.GetMemberCount(), Clan.GetMemberMaxCount());
        const char* pszAlly = Clan.GetAllyName(0);
        strAlly = (pszAlly && pszAlly[0]) ? RoseRmlText::FromGame(pszAlly) : Rml::String("None");
        strRank = RoseRmlText::FromGame(CStringManager::GetSingleton().GetClanClass(iClass));
        strContrib = Thousands(Clan.GetMyClanPoint());
    }
    struct { Rml::String* pDst; const Rml::String* pSrc; const char* pszVar; } strs[] = {
        {&m_strName, &strName, "name"},
        {&m_strSlogan, &strSlogan, "slogan"},
        {&m_strPoints, &strPoints, "points"},
        {&m_strMoney, &strMoney, "money"},
        {&m_strMemberCount, &strCount, "member_count"},
        {&m_strAlly, &strAlly, "ally"},
        {&m_strMyRank, &strRank, "my_rank"},
        {&m_strMyContrib, &strContrib, "my_contrib"},
    };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        if (*strs[i].pDst != *strs[i].pSrc) {
            *strs[i].pDst = *strs[i].pSrc;
            m_Model.DirtyVariable(strs[i].pszVar);
        }
    }
    if (iLevel != m_iLevel) {
        m_iLevel = iLevel;
        m_Model.DirtyVariable("level");
    }
    const bool bPenalty = bInClan && iClass <= CClan::CLAN_PENALTY;
    if (bPenalty != m_bMyPenalty) {
        m_bMyPenalty = bPenalty;
        m_Model.DirtyVariable("my_penalty");
    }

    /// Your rank's badge ( CClanDlg::Create's table: junior's for ranks 0-1 ).
    static const char* kBadges[7] = {"CLAN01_MARK_JUNIOR", "CLAN01_MARK_JUNIOR",
        "CLAN01_MARK_SENIOR", "CLAN01_MARK_LANDER", "CLAN01_MARK_COMMANDER",
        "CLAN01_MARK_SUBMASTER", "CLAN01_MARK_MASTER"};
    Rml::String strBadgeSrc, strBadgeRect;
    if (bInClan && iClass >= 0 && iClass <= 6) {
        const int iBadge = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, kBadges[iClass]);
        RoseRmlIcons::Resolve(IMAGE_RES_UI, iBadge, strBadgeSrc, strBadgeRect);
    }
    if (strBadgeSrc != m_strBadgeSrc || strBadgeRect != m_strBadgeRect) {
        m_strBadgeSrc = strBadgeSrc;
        m_strBadgeRect = strBadgeRect;
        m_Model.DirtyVariable("badge_src");
        m_Model.DirtyVariable("badge_rect");
    }

    /// The emblem: the two sprites, or the clan's own bitmap ( CClanMarkView ).
    int iKind = 0;
    Rml::String strBackSrc, strBackRect, strCenterSrc, strCenterRect;
    std::string strCustom;
    unsigned __int64 ullStamp = 0;
    if (bInClan && g_pAVATAR->GetClanID()) {
        if (g_pAVATAR->GetClanMarkBack() != 0) {
            if (RoseRmlIcons::Resolve(IMAGE_RES_CLANBACK, g_pAVATAR->GetClanMarkBack(), strBackSrc, strBackRect)
                && RoseRmlIcons::Resolve(IMAGE_RES_CLANCENTER, g_pAVATAR->GetClanMarkCenter(), strCenterSrc, strCenterRect))
                iKind = 1;
        } else {
            /// Fetched from the server by whoever draws the avatar's name tag
            /// first; it shows once the file is there.
            std::string strFile;
            CClanMarkUserDefined::GetClanMarkFileName(
                CGame::GetInstance().GetSelectedServerID(), g_pAVATAR->GetClanID(), strFile);
            if (DiskFile(strFile, strCustom, ullStamp))
                iKind = 2;
        }
    }
    /// A new or rewritten bitmap: RmlUi caches textures by source, so the old
    /// one is released and reloads from disk.
    if (iKind == 2 && (strCustom != m_strCustomSrc || ullStamp != m_ullCustomStamp)) {
        /// The previous file, or this one before it was rewritten.
        if (!m_strCustomSrc.empty())
            Rml::ReleaseTexture(m_strCustomSrc);
        m_strCustomSrc = strCustom;
        m_ullCustomStamp = ullStamp;
    }
    const Rml::String strCustomBound = (iKind == 2) ? Rml::String(m_strCustomSrc) : Rml::String();
    if (iKind != m_iMarkKind) {
        m_iMarkKind = iKind;
        m_Model.DirtyVariable("mark_kind");
    }
    if (strBackSrc != m_strBackSrc || strBackRect != m_strBackRect || strCenterSrc != m_strCenterSrc
        || strCenterRect != m_strCenterRect) {
        m_strBackSrc = strBackSrc;
        m_strBackRect = strBackRect;
        m_strCenterSrc = strCenterSrc;
        m_strCenterRect = strCenterRect;
        m_Model.DirtyVariable("back_src");
        m_Model.DirtyVariable("back_rect");
        m_Model.DirtyVariable("center_src");
        m_Model.DirtyVariable("center_rect");
    }
    if (strCustomBound != m_strCustomBound) {
        m_strCustomBound = strCustomBound;
        m_Model.DirtyVariable("custom_src");
    }
}

void
RoseRmlClan::SampleMembers() {
    CClanDlg* pDlg = Dlg();
    std::vector<MemberRow> rows;
    if (pDlg != NULL && m_bInClan) {
        const int iCount = pDlg->GetMemberCount();
        rows.reserve(iCount);
        for (int i = 0; i < iCount; ++i) {
            CClanMemberItem* pItem = pDlg->GetMemberAt(i);
            if (pItem == NULL)
                continue;
            MemberRow row;
            row.strName = pItem->GetName();
            row.iClass = pItem->GetClass();
            row.iPoint = pItem->GetClanPoint();
            row.iChannel = pItem->GetChannelNo();
            /// Level and job are only set for a member who was on when the
            /// list came ( the item leaves them unset otherwise ).
            row.iLevel = (row.iChannel != kOffline) ? pItem->GetLevel() : 0;
            row.iJob = (row.iChannel != kOffline) ? pItem->GetJob() : -1;
            rows.push_back(row);
        }
    }
    std::sort(rows.begin(), rows.end(), MemberOrder);

    bool bSelectedFound = false;
    int iOnline = 0;
    int iSelectedClass = -1;
    m_RowNames.clear();
    std::vector<MemberVM> members = m_Members;
    if (members.size() < rows.size())
        members.resize(rows.size());
    for (size_t i = 0; i < members.size(); ++i) {
        MemberVM& vm = members[i];
        vm.index = (int)i;
        vm.used = i < rows.size();
        if (!vm.used) {
            vm.on = vm.online = vm.me = vm.penalty = false;
            vm.name.clear();
            vm.rank.clear();
            vm.level.clear();
            vm.job.clear();
            vm.contrib.clear();
            continue;
        }
        const MemberRow& row = rows[i];
        m_RowNames.push_back(row.strName);
        vm.online = row.iChannel != kOffline;
        vm.me = IsMe(row.strName.c_str());
        vm.penalty = row.iClass <= CClan::CLAN_PENALTY;
        vm.on = !m_strSelected.empty() && strcmpi(row.strName.c_str(), m_strSelected.c_str()) == 0;
        vm.name = RoseRmlText::FromGame(row.strName.c_str());
        vm.rank = RoseRmlText::FromGame(CStringManager::GetSingleton().GetClanClass(row.iClass));
        vm.level = row.iLevel > 0 ? Rml::String(CStr::Printf("%d", row.iLevel)) : Rml::String("-");
        const char* pszJob = row.iJob >= 0 ? CStringManager::GetSingleton().GetJobName(row.iJob) : NULL;
        vm.job = (vm.online && pszJob) ? RoseRmlText::FromGame(pszJob) : Rml::String("-");
        vm.contrib = Thousands(row.iPoint);
        if (vm.online)
            ++iOnline;
        if (vm.on) {
            bSelectedFound = true;
            iSelectedClass = row.iClass;
        }
    }
    /// A member who left cannot stay selected.
    if (!bSelectedFound)
        m_strSelected.clear();
    if (members != m_Members) {
        m_Members.swap(members);
        m_Model.DirtyVariable("members");
    }
    if (iOnline != m_iOnline) {
        m_iOnline = iOnline;
        m_Model.DirtyVariable("online");
    }
    if ((int)rows.size() != m_iTotal) {
        m_iTotal = (int)rows.size();
        m_Model.DirtyVariable("total");
    }

    /// The buttons light by the classic rules ( CClanDlg::Request* check them
    /// again and explain a refusal ).
    const bool bSel = bSelectedFound;
    const bool bOther = bSel && !IsMe(m_strSelected.c_str());
    const bool bUp = m_bMaster && bOther && CClan::GetInstance().IsValidClassUp(iSelectedClass);
    const bool bDown = m_bMaster && bOther && iSelectedClass > CClan::CLAN_PENALTY;
    const bool bKick = m_bMaster && bOther;
    const bool bEntrust = m_bMaster && bOther && iSelectedClass >= CClan::CLAN_COMMANDER;
    struct { bool* pDst; bool bSrc; const char* pszVar; } flags[] = {
        {&m_bHasSelection, bSel, "has_selection"},
        {&m_bCanUp, bUp, "can_up"},
        {&m_bCanDown, bDown, "can_down"},
        {&m_bCanKick, bKick, "can_kick"},
        {&m_bCanEntrust, bEntrust, "can_entrust"},
    };
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        if (*flags[i].pDst != flags[i].bSrc) {
            *flags[i].pDst = flags[i].bSrc;
            m_Model.DirtyVariable(flags[i].pszVar);
        }
    }
}

void
RoseRmlClan::SampleSkills() {
    CClanSkillSlot* pSlots = CClan::GetInstance().GetClanSkillSlot();
    std::vector<SkillVM> skills = m_Skills;
    for (size_t i = 0; i < skills.size(); ++i)
        skills[i].used = false;

    int iCount = 0;
    if (pSlots != NULL && m_bInClan) {
        for (int iSlot = 0; iSlot < MAX_CLAN_SKILL_SLOT; ++iSlot) {
            CSkill* pSkill = pSlots->GetSkill((short)iSlot);
            if (pSkill == NULL)
                continue;
            if ((int)skills.size() <= iCount)
                skills.resize(iCount + 1);
            SkillVM& vm = skills[iCount++];
            const int iIdx = pSkill->GetSkillIndex();
            vm.slot = iSlot;
            vm.used = true;
            vm.src.clear();
            vm.rect.clear();
            RoseRmlIcons::Resolve(IMAGE_RES_SKILL_ICON, SKILL_ICON_NO(iIdx), vm.src, vm.rect);
            const char* pszName = SKILL_NAME(iIdx);
            vm.name = pszName ? RoseRmlText::FromGame(pszName) : Rml::String();
            vm.level = pSkill->GetSkillLevel();
            vm.passive = SKILL_TYPE(iIdx) == SKILL_TYPE_PASSIVE;
            vm.expires.clear();
            if (pSkill->HasExpiredTime()) {
                SYSTEMTIME st;
                classTIME::AbsSecondToSystem(pSkill->GetExpiredTime(), st);
                vm.expires = CStr::Printf("Until %d/%02d/%02d %d:%02d",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            }
            CIconSkillClan icon(iSlot);
            vm.cd = floorf(icon.GetCooldown(NULL) * 100.0f + 0.5f);
        }
    }
    /// Grow-only rows: the unused tail is hidden, never removed.
    for (size_t i = iCount; i < skills.size(); ++i) {
        SkillVM& vm = skills[i];
        vm.slot = -1;
        vm.used = false;
        vm.src.clear();
        vm.rect.clear();
        vm.name.clear();
        vm.level = 0;
        vm.expires.clear();
        vm.passive = false;
        vm.cd = 0.0f;
    }
    if (skills != m_Skills) {
        m_Skills.swap(skills);
        m_Model.DirtyVariable("skills");
    }
    if (iCount != m_iSkillCount) {
        m_iSkillCount = iCount;
        m_Model.DirtyVariable("skill_count");
    }
}

void
RoseRmlClan::SampleNotice() {
    Rml::String strRml;
    if (m_bInClan) {
        const Rml::String strText = RoseRmlText::Escape(RoseRmlText::FromGame(CClan::GetInstance().GetNotice()));
        strRml.reserve(strText.size() + 16);
        for (size_t i = 0; i < strText.size(); ++i) {
            if (strText[i] == '\r')
                continue;
            if (strText[i] == '\n')
                strRml += "<br/>";
            else
                strRml += strText[i];
        }
    }
    if (strRml != m_strNoticeRml) {
        m_strNoticeRml = strRml;
        m_Model.DirtyVariable("notice_rml");
    }
    const bool bHas = !strRml.empty();
    if (bHas != m_bHasNotice) {
        m_bHasNotice = bHas;
        m_Model.DirtyVariable("has_notice");
    }
    /// Only the master edits; a rank change mid-edit ends it.
    if (m_bEditing && !m_bMaster) {
        m_bEditing = false;
        m_Model.DirtyVariable("editing");
    }
}

/// --- actions ----------------------------------------------------------------------

void
RoseRmlClan::OnAction(int iAction) {
    CClanDlg* pDlg = Dlg();
    CClanMemberItem* pMember =
        (pDlg && !m_strSelected.empty()) ? pDlg->FindMember(m_strSelected.c_str()) : NULL;

    switch (iAction) {
        case ACT_PROMOTE:
            CClanDlg::RequestPromote(pMember);
            break;
        case ACT_DEMOTE:
            CClanDlg::RequestDemote(pMember);
            break;
        case ACT_KICK:
            CClanDlg::RequestBan(pMember);
            break;
        case ACT_ENTRUST:
            CClanDlg::RequestEntrust(pMember);
            break;
        case ACT_INVITE:
            CClanDlg::RequestInviteTarget();
            break;
        case ACT_LEAVE:
            CClanDlg::RequestLeave();
            break;
        case ACT_REGISTER_MARK:
            CClanDlg::RequestRegisterMark();
            break;
        default:
            break;
    }
}

void
RoseRmlClan::OnPreviewMark() {
    /// CClanDlg's Preview: Mark.bmp from the game folder, checked the way
    /// registering checks it ( the check explains a refusal ).
    const std::string& strFile = CClanMarkUserDefined::NewClanMarkFileName;
    HANDLE hBmpFile = CreateFileA(strFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
    if (hBmpFile == INVALID_HANDLE_VALUE) {
        g_itMGR.OpenMsgBox(CStr::Printf("%s %s", STR_CLANMARK_FILE_NOTFOUND, STR_CLANMARK_HELP_HOMEPAGE));
        return;
    }
    const bool bOk =
        CClanMarkTransfer::GetSingleton().Check_BmpFile(hBmpFile, CClanMarkUserDefined::ClanMarkSize);
    CloseHandle(hBmpFile);
    if (!bOk)
        return;

    std::string strFull;
    unsigned __int64 ullStamp = 0;
    if (!DiskFile(strFile, strFull, ullStamp))
        return;
    /// The file may have been edited since the last preview.
    if (!m_strPreviewSrc.empty())
        Rml::ReleaseTexture(m_strPreviewSrc);
    m_strPreviewSrc = strFull;
    if (m_strPreviewBound != m_strPreviewSrc) {
        m_strPreviewBound = m_strPreviewSrc;
        m_Model.DirtyVariable("preview_src");
    }
    if (!m_bHasPreview) {
        m_bHasPreview = true;
        m_Model.DirtyVariable("has_preview");
    }
}

void
RoseRmlClan::OnEditNotice() {
    if (!m_bInClan || !m_bMaster) {
        if (m_bInClan)
            g_itMGR.OpenMsgBox(STR_CLAN_NO_RIGHT);
        return;
    }
    if (Rml::Element* pField = m_pDocument ? m_pDocument->GetElementById("noticeedit") : NULL)
        pField->SetAttribute("value", RoseRmlText::FromGame(CClan::GetInstance().GetNotice()));
    if (!m_bEditing) {
        m_bEditing = true;
        m_Model.DirtyVariable("editing");
    }
    m_bFocusNotice = true;
}

void
RoseRmlClan::OnSaveNotice() {
    Rml::Element* pField = m_pDocument ? m_pDocument->GetElementById("noticeedit") : NULL;
    if (pField == NULL || !m_bMaster)
        return;
    const std::string strText = ToGame(pField->GetAttribute<Rml::String>("value", ""));
    if (strText.empty()) {
        g_itMGR.OpenMsgBox("Write the notice first.");
        return;
    }
    /// CClanRegistNotice's Confirm; the server sends the notice back to all.
    g_pNet->Send_cli_CLAN_COMMAND(GCMD_MOTD, (char*)strText.c_str());
    pField->Blur();
    m_bEditing = false;
    m_Model.DirtyVariable("editing");
}

void
RoseRmlClan::OnCancelNotice() {
    if (Rml::Element* pField = m_pDocument ? m_pDocument->GetElementById("noticeedit") : NULL)
        pField->Blur();
    if (m_bEditing) {
        m_bEditing = false;
        m_Model.DirtyVariable("editing");
    }
}

void
RoseRmlClan::UpdateDragStart() {
    if (m_iPressSlot < 0)
        return;

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_iPressSlot = -1;
        return;
    }

    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iSlopX = max(3, g_pCApp->GetWIDTH() / 200);
    const int iSlopY = max(3, g_pCApp->GetHEIGHT() / 200);
    if (abs(ptMouse.x - m_iPressX) < iSlopX && abs(ptMouse.y - m_iPressY) < iSlopY)
        return;

    const int iSlot = m_iPressSlot;
    m_iPressSlot = -1;

    CIconSkillClan icon(iSlot);
    CSkill* pSkill = icon.GetSkill();
    /// Passive skills cannot go on a bar ( CClanSkillListItem::SetIcon ).
    if (pSkill == NULL || m_pDragItem == NULL
        || SKILL_TYPE(pSkill->GetSkillIndex()) == SKILL_TYPE_PASSIVE)
        return;
    m_pDragItem->SetIcon(&icon); /// stores a clone
    CDragNDropMgr::GetInstance().DragStart(m_pDragItem);
}

void
RoseRmlClan::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging() || m_iTab != TAB_SKILLS)
        return;

    int iSlot = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("clanskill")) {
            iSlot = pEl->GetAttribute<int>("clanskill", -1);
            break;
        }
    }
    if (iSlot < 0)
        return;

    CIconSkillClan icon(iSlot);
    if (icon.GetSkill() == NULL)
        return;

    CInfo ToolTip;
    ToolTip.Clear();
    icon.GetToolTip(ToolTip, DLG_TYPE_CLAN, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlClan::PlaceDefault() {
    /// Left of the middle, a little high -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fLeft = floorf((float)view.x * 0.5f - size.x - 20.0f);
    const float fTop = floorf(((float)view.y - size.y) * 0.35f);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(max(0.0f, fLeft), Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(max(0.0f, fTop), Rml::Unit::PX));
}

void
RoseRmlClan::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen && !IsInWorld())
        SetOpen(false);

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    Sample();
    PlaceDefault();

    if (m_bFocusNotice) {
        m_bFocusNotice = false;
        if (Rml::Element* pField = m_pDocument->GetElementById("noticeedit")) {
            pField->Focus();
            if (CTEditBox::s_pFocusEdit != NULL)
                CTEditBox::s_pFocusEdit->SetFocus(false);
        }
    }

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}
