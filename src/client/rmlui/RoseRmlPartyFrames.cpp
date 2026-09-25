#include "stdafx.h"

#include "RoseRmlPartyFrames.h"
#include "RoseRmlLayout.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../JCommandState.h"
#include "../System/CGame.h"
#include "../GameData/CParty.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/TypeResource.h"
#include "../interface/Command/CTCmdHotExec.h"
#include "../interface/Dlgs/CMsgBox.h"
#include "tgamectrl/tdialog.h"

#include "rose/common/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace {

/// CPartyMember's "too far" line: beyond this ( cm ) a member's frame dims.
const int kTooFar = 40 * 100;

/// Below this share of max HP the bar switches to its warning look.
const float kLowHpPct = 25.0f;

enum { STATE_NEAR, STATE_FAR, STATE_ELSEWHERE, STATE_OFFLINE };

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

/// "12,345".
Rml::String
Thousands(int iValue) {
    char szRaw[16];
    _snprintf(szRaw, sizeof(szRaw), "%d", iValue < 0 ? -iValue : iValue);
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

/// The member's avatar, if it is in your zone and in sight; NULL otherwise.
CObjAVT*
MemberAvatar(const PartyMember& member) {
    if (member.m_Info.m_wObjectIDX == 0)
        return NULL;
    return g_pObjMGR->Get_CharAVT(g_pObjMGR->Get_ClientObjectIndex(member.m_Info.m_wObjectIDX),
        false);
}

/// Looks a card's member up again at click time: the list may have changed
/// since the frame was drawn.
bool
FindMember(int iTag, PartyMember& member) {
    return iTag != 0 && CParty::GetInstance().GetMemberInfoByTag((DWORD)iTag, member);
}

} // namespace

RoseRmlPartyFrames::RoseRmlPartyFrames():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_iLevel(1),
    m_strExpPct("0.0%"), ///< Sample only rewrites it when the width moves
    m_fExpWidth(0.0f),
    m_bLeader(false) {}

bool
RoseRmlPartyFrames::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("party");
    if (!constructor)
        return false;

    RoseRmlBuffBar::RegisterBuffStruct(constructor);

    if (auto member = constructor.RegisterStruct<MemberVM>()) {
        member.RegisterMember("tag", &MemberVM::tag);
        member.RegisterMember("name", &MemberVM::name);
        member.RegisterMember("level", &MemberVM::level);
        member.RegisterMember("leader", &MemberVM::leader);
        member.RegisterMember("state", &MemberVM::state);
        member.RegisterMember("state_text", &MemberVM::state_text);
        member.RegisterMember("hp", &MemberVM::hp);
        member.RegisterMember("hp_pct", &MemberVM::hp_pct);
        member.RegisterMember("hp_low", &MemberVM::hp_low);
        member.RegisterMember("buffs", &MemberVM::buffs);
    }
    constructor.RegisterArray<std::vector<MemberVM>>();

    constructor.Bind("level", &m_iLevel);
    constructor.Bind("exp_pct", &m_strExpPct);
    constructor.Bind("exp_width", &m_fExpWidth);
    constructor.Bind("is_leader", &m_bLeader);
    constructor.Bind("members", &m_Members);

    /// Click a card: target that member, as CPartyMember did -- only while
    /// they are in sight, since there is nothing to target otherwise.
    constructor.BindEventCallback("target",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            PartyMember member;
            if (args.empty() || !FindMember(args[0].Get<int>(), member))
                return;
            const short nClientIdx = g_pObjMGR->Get_ClientObjectIndex(member.m_Info.m_wObjectIDX);
            if (member.m_Info.m_wObjectIDX != 0 && g_pObjMGR->Get_CharAVT(nClientIdx, false))
                g_UserInputSystem.SetCurrentTarget(nClientIdx);
        });

    /// Hand the lead over: CPartyDlg's Entrust, with its confirmation.
    constructor.BindEventCallback("lead",
        [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            ev.StopPropagation();
            PartyMember member;
            if (args.empty() || !FindMember(args[0].Get<int>(), member)
                || !CParty::GetInstance().IsPartyLeader()
                || member.m_Info.m_dwUserTAG == g_pAVATAR->GetUniqueTag()
                || member.m_Info.m_wObjectIDX == 0)
                return;
            CTCmdSendPacketPartyReq* pCmd =
                new CTCmdSendPacketPartyReq(PARTY_REQ_CHANGE_OWNER, member.m_Info.m_wObjectIDX);
            const std::string strText =
                "Make " + member.m_strName + " the party leader? You will lose Lead and Kick.";
            if (!RoseRmlUi::ConfirmBox("Hand over the lead", strText.c_str(), "Hand over", "Cancel", pCmd, NULL)) {
                sprintf(g_MsgBuf, FORMAT_STR_PARTY_QUERY_ENTRUST, member.m_strName.c_str());
                g_itMGR.OpenMsgBox(g_MsgBuf, CMsgBox::BT_OK | CMsgBox::BT_CANCEL, true, 0, pCmd, NULL);
            }
        });

    /// Remove a member: CPartyDlg's Ban, with its confirmation.
    constructor.BindEventCallback("kick",
        [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            ev.StopPropagation();
            PartyMember member;
            if (args.empty() || !FindMember(args[0].Get<int>(), member)
                || !CParty::GetInstance().IsPartyLeader()
                || member.m_Info.m_dwUserTAG == g_pAVATAR->GetUniqueTag())
                return;
            CTCmdSendPacketPartyReq* pCmd =
                new CTCmdSendPacketPartyReq(PARTY_REQ_BAN, member.m_Info.m_dwUserTAG);
            const std::string strText = "Remove " + member.m_strName + " from the party?";
            if (!RoseRmlUi::ConfirmBox("Kick", strText.c_str(), "Kick", "Cancel", pCmd, NULL)) {
                sprintf(g_MsgBuf, FORMAT_STR_PARTY_QUERY_BAN, member.m_strName.c_str());
                g_itMGR.OpenMsgBox(g_MsgBuf, CMsgBox::BT_OK | CMsgBox::BT_CANCEL, true, 0, pCmd, NULL);
            }
        });

    /// Party options ( EXP / loot sharing ): a toggle, routed through IT_MGR
    /// to the UI2 window, which opens beside these frames.
    constructor.BindEventCallback("options",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (g_itMGR.IsDlgOpened(DLG_TYPE_PARTYOPTION))
                g_itMGR.CloseDialog(DLG_TYPE_PARTYOPTION);
            else
                g_itMGR.OpenDialog(DLG_TYPE_PARTYOPTION);
        });

    /// Leave: CPartyDlg's Leave, confirmation and all.
    constructor.BindEventCallback("leave",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            CTMacroCommand* pMacroCmd = new CTMacroCommand;
            pMacroCmd->AddSubCommand(new CTCmdSendPacketPartyReq(PARTY_REQ_LEFT,
                g_pObjMGR->Get_ServerObjectIndex(g_pAVATAR->Get_INDEX())));
            pMacroCmd->AddSubCommand(new CTCmdLeaveParty);
            if (!RoseRmlUi::ConfirmBox("Leave party",
                    "Are you sure you want to leave the party?",
                    "Leave",
                    "Stay",
                    pMacroCmd,
                    NULL))
                g_itMGR.OpenMsgBox(STR_PARTY_QUERY_LEAVE,
                    CMsgBox::BT_OK | CMsgBox::BT_CANCEL,
                    true,
                    0,
                    pMacroCmd,
                    NULL);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "party.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load party frames document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("party");
    RoseRmlLayout::Track(m_pPanel, "party");

    LOG_INFO("[rmlui] party frames document loaded");
    return true;
}

void
RoseRmlPartyFrames::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

void
RoseRmlPartyFrames::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlPartyFrames::Sample() {
    CParty& Party = CParty::GetInstance();

    /// --- party level and EXP ------------------------------------------------
    const int iLevel = Party.GetLevel();
    if (iLevel != m_iLevel) {
        m_iLevel = iLevel;
        m_Model.DirtyVariable("level");
    }

    const int iMaxExp = max(1, Party.GetMaxExp());
    const int iExp = min(iMaxExp, max(0, Party.GetCurrExp()));
    const float fExp = floorf(1000.0f * (float)iExp / (float)iMaxExp) / 10.0f;
    if (fExp != m_fExpWidth) {
        m_fExpWidth = fExp;
        m_strExpPct = Printf("%.1f%%", fExp);
        m_Model.DirtyVariable("exp_width");
        m_Model.DirtyVariable("exp_pct");
    }

    const bool bLeader = Party.IsPartyLeader();
    if (bLeader != m_bLeader) {
        m_bLeader = bLeader;
        m_Model.DirtyVariable("is_leader");
    }

    /// --- members: the leader first, then join order; not you -----------------
    const DWORD dwSelf = g_pAVATAR->GetUniqueTag();
    const DWORD dwLeader = Party.GetLeaderObjectTAG();

    std::vector<MemberVM> members;
    const std::list<PartyMember>& list = Party.GetMembers();
    for (int iPass = 0; iPass < 2; ++iPass) {
        for (std::list<PartyMember>::const_iterator it = list.begin(); it != list.end(); ++it) {
            const DWORD dwTag = it->m_Info.m_dwUserTAG;
            if (dwTag == dwSelf || (dwTag == dwLeader) != (iPass == 0))
                continue;

            MemberVM vm;
            vm.tag = (int)dwTag;
            vm.name = it->m_strName;
            vm.leader = (dwTag == dwLeader);
            vm.hp_pct = 0.0f;
            vm.hp_low = false;

            CObjAVT* pAvt = MemberAvatar(*it);
            if (it->m_Info.m_wObjectIDX == 0) {
                vm.state = STATE_OFFLINE;
                vm.state_text = "Offline";
            } else if (pAvt == NULL) {
                vm.state = STATE_ELSEWHERE;
                vm.state_text = "Elsewhere";
            } else if (g_pAVATAR->Get_DISTANCE(pAvt) >= kTooFar) {
                vm.state = STATE_FAR;
                vm.state_text = "Far";
            } else {
                vm.state = STATE_NEAR;
            }

            if (pAvt != NULL) {
                vm.level = Printf("%d", pAvt->Get_LEVEL());
                const int iHp = max(0, pAvt->Get_HP());
                const int iMaxHp = pAvt->Get_MaxHP();
                if (iMaxHp > 0) {
                    vm.hp_pct = floorf(100.0f * (float)min(iHp, iMaxHp) / (float)iMaxHp);
                    vm.hp_low = vm.hp_pct < kLowHpPct;
                }
                vm.hp = Thousands(iHp) + " / " + Thousands(iMaxHp);
                RoseRmlBuffBar::CollectBuffs(pAvt, vm.buffs);
            }
            members.push_back(vm);
        }
    }

    if (members != m_Members) {
        m_Members.swap(members);
        m_Model.DirtyVariable("members");
    }
}

void
RoseRmlPartyFrames::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bWant = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN
        && CParty::GetInstance().HasParty();
    SetVisible(bWant);
    if (!m_bVisible)
        return;

    Sample();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
