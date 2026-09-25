#include "stdafx.h"

#include "RoseRmlPartyOptions.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../CClientStorage.h"
#include "../System/CGame.h"
#include "../GameData/CParty.h"

#include "rose/common/log.h"

#include <math.h>

namespace {

/// Gap between the party frames and this window, in dp.
const float kAnchorGap = 4.0f;

} // namespace

RoseRmlPartyOptions::RoseRmlPartyOptions():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_pAnchor(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iPlaceFrames(0),
    m_bLeader(false),
    m_bExpByLevel(false),
    m_bLootInTurn(false),
    m_bHpGauge(false) {}

bool
RoseRmlPartyOptions::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("partyoptions");
    if (!constructor)
        return false;

    constructor.Bind("leader", &m_bLeader);
    constructor.Bind("exp_by_level", &m_bExpByLevel);
    constructor.Bind("loot_in_turn", &m_bLootInTurn);
    constructor.Bind("hp_gauge", &m_bHpGauge);

    /// set_exp(by_level) / set_loot(in_turn): the leader's rules, sent as they
    /// are clicked. CParty::SendChangePartyRule sends only on a change.
    constructor.BindEventCallback("set_exp",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            CParty& Party = CParty::GetInstance();
            if (args.empty() || !Party.IsPartyLeader())
                return;
            BYTE btRule = Party.GetPartyRule() & ~BIT_PARTY_RULE_EXP_PER_PLAYER;
            if (args[0].Get<bool>())
                btRule |= BIT_PARTY_RULE_EXP_PER_PLAYER;
            Party.SendChangePartyRule(btRule);
            Sample();
        });

    constructor.BindEventCallback("set_loot",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            CParty& Party = CParty::GetInstance();
            if (args.empty() || !Party.IsPartyLeader())
                return;
            BYTE btRule = Party.GetPartyRule() & ~BIT_PARTY_RULE_ITEM_TO_ORDER;
            if (args[0].Get<bool>())
                btRule |= BIT_PARTY_RULE_ITEM_TO_ORDER;
            Party.SendChangePartyRule(btRule);
            Sample();
        });

    constructor.BindEventCallback("toggle_hp",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_ClientStorage.SetShowPartyMemberHpGuage(!g_ClientStorage.IsShowPartyMemberHpGuage());
            Sample();
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { SetOpen(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "partyoptions.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load party options document '{}'", strDoc.c_str());
        return false;
    }

    /// Not tracked: it opens beside the party frames every time, like the
    /// legacy dialog did beside its window. It can still be dragged away.
    m_pPanel = m_pDocument->GetElementById("partyoptions");

    LOG_INFO("[rmlui] party options document loaded");
    return true;
}

void
RoseRmlPartyOptions::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_pAnchor = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

void
RoseRmlPartyOptions::SetOpen(bool bOpen) {
    const bool bOpening = bOpen && !m_bOpen;
    m_bOpen = bOpen;
    if (bOpening) {
        Sample();
        m_iPlaceFrames = 2;
    }
}

void
RoseRmlPartyOptions::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlPartyOptions::PlaceBesideAnchor() {
    if (m_pAnchor == NULL || m_pPanel == NULL || m_pContext == NULL)
        return;

    const Rml::Vector2f anchorPos = m_pAnchor->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f anchorSize = m_pAnchor->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fGap = kAnchorGap * RoseRmlLayout::GetScaleRatio();

    /// To the right of the frames; to the left when that runs off screen.
    float x = anchorPos.x + anchorSize.x + fGap;
    if (x + size.x > (float)view.x)
        x = anchorPos.x - size.x - fGap;
    const float y = anchorPos.y;

    /// Whole pixels, or the text blurs ( see RoseRmlLayout's SetPosition ).
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(floorf(x), Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(floorf(y), Rml::Unit::PX));
}

void
RoseRmlPartyOptions::Sample() {
    CParty& Party = CParty::GetInstance();
    const BYTE btRule = Party.GetPartyRule();

    const bool bLeader = Party.IsPartyLeader();
    const bool bExpByLevel = (btRule & BIT_PARTY_RULE_EXP_PER_PLAYER) != 0;
    const bool bLootInTurn = (btRule & BIT_PARTY_RULE_ITEM_TO_ORDER) != 0;
    const bool bHpGauge = g_ClientStorage.IsShowPartyMemberHpGuage();

    if (bLeader != m_bLeader) {
        m_bLeader = bLeader;
        m_Model.DirtyVariable("leader");
    }
    if (bExpByLevel != m_bExpByLevel) {
        m_bExpByLevel = bExpByLevel;
        m_Model.DirtyVariable("exp_by_level");
    }
    if (bLootInTurn != m_bLootInTurn) {
        m_bLootInTurn = bLootInTurn;
        m_Model.DirtyVariable("loot_in_turn");
    }
    if (bHpGauge != m_bHpGauge) {
        m_bHpGauge = bHpGauge;
        m_Model.DirtyVariable("hp_gauge");
    }
}

void
RoseRmlPartyOptions::Update() {
    if (m_pDocument == NULL)
        return;

    /// Nothing to set outside a party: closed, not just hidden.
    const bool bWant = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN
        && CParty::GetInstance().HasParty();
    if (!bWant)
        m_bOpen = false;

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    Sample();

    /// Layout runs after this update, so the first frame places with a stale
    /// size and the second corrects it.
    if (m_iPlaceFrames > 0) {
        PlaceBesideAnchor();
        --m_iPlaceFrames;
    }

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
