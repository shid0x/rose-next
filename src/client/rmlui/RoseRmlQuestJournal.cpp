#include "stdafx.h"

#include "RoseRmlQuestJournal.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CToolTipMgr.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/TypeResource.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Command/UICommand.h"
#include "../interface/Dlgs/CMsgBox.h"
#include "io_quest.h"

#include "rose/common/log.h"

#include <stdio.h>
#include <stdarg.h>

namespace {

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

} // namespace

RoseRmlQuestJournal::RoseRmlQuestJournal():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iSelectedID(0),
    m_iQuestCount(0),
    m_iQuestMax(QUEST_PER_PLAYER),
    m_bHasSelection(false),
    m_bTimed(false),
    m_bTimedOut(false),
    m_bHasItems(false),
    m_bCanAbandon(true) {}

bool
RoseRmlQuestJournal::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("quests");
    if (!constructor)
        return false;

    if (auto quest = constructor.RegisterStruct<QuestVM>()) {
        quest.RegisterMember("slot", &QuestVM::slot);
        quest.RegisterMember("name", &QuestVM::name);
        quest.RegisterMember("has_icon", &QuestVM::has_icon);
        quest.RegisterMember("src", &QuestVM::src);
        quest.RegisterMember("rect", &QuestVM::rect);
        quest.RegisterMember("timed", &QuestVM::timed);
        quest.RegisterMember("selected", &QuestVM::selected);
    }
    constructor.RegisterArray<std::vector<QuestVM>>();

    if (auto item = constructor.RegisterStruct<ItemVM>()) {
        item.RegisterMember("index", &ItemVM::index);
        item.RegisterMember("filled", &ItemVM::filled);
        item.RegisterMember("src", &ItemVM::src);
        item.RegisterMember("rect", &ItemVM::rect);
        item.RegisterMember("count", &ItemVM::count);
    }
    constructor.RegisterArray<std::vector<ItemVM>>();

    constructor.Bind("quests", &m_Quests);
    constructor.Bind("count", &m_iQuestCount);
    constructor.Bind("max", &m_iQuestMax);
    constructor.Bind("has_sel", &m_bHasSelection);
    constructor.Bind("name", &m_strName);
    constructor.Bind("desc", &m_strDesc);
    constructor.Bind("timed", &m_bTimed);
    constructor.Bind("timed_out", &m_bTimedOut);
    constructor.Bind("timer", &m_strTimer);
    constructor.Bind("items", &m_Items);
    constructor.Bind("has_items", &m_bHasItems);
    constructor.Bind("can_abandon", &m_bCanAbandon);

    constructor.BindEventCallback("select",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty() || g_pAVATAR == NULL)
                return;
            const int iSlot = args[0].Get<int>();
            if (iSlot < 0 || iSlot >= QUEST_PER_PLAYER)
                return;
            const int iID = g_pAVATAR->m_Quests.m_QUEST[iSlot].GetID();
            if (iID > 0 && iID != m_iSelectedID) {
                m_iSelectedID = iID;
                Sample();
            }
        });

    /// Abandon the selected quest: the classic confirmation and command
    /// ( CQuestDlg IID_BTN_ABANDON ), asked through the UI2 message box.
    constructor.BindEventCallback("abandon",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            const int iSlot = SelectedSlot();
            if (iSlot < 0 || !m_bCanAbandon)
                return;
            const int iQuestID = g_pAVATAR->m_Quests.m_QUEST[iSlot].GetID();
            if (iQuestID < 1)
                return;

            CTCmdAbandonQuest* pOkCmd = new CTCmdAbandonQuest(iSlot, iQuestID);
            const Rml::String strText = "Abandon \"" + RoseRmlText::FromGame(QUEST_NAME(iQuestID))
                + "\"? Its progress and its quest items will be lost.";
            if (!RoseRmlUi::ConfirmBox(
                    "Abandon quest", strText.c_str(), "Abandon", "Keep", pOkCmd, NULL))
                g_itMGR.OpenMsgBox(CStr::Printf(F_STR_QUERY_ABANDON_QUEST, QUEST_NAME(iQuestID)),
                    CMsgBox::BT_OK | CMsgBox::BT_CANCEL,
                    true,
                    0,
                    pOkCmd,
                    NULL);
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { SetOpen(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "quests.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load quest journal document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("quests");
    RoseRmlLayout::Track(m_pPanel, "quests");

    LOG_INFO("[rmlui] quest journal document loaded");
    return true;
}

void
RoseRmlQuestJournal::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

void
RoseRmlQuestJournal::SetOpen(bool bOpen) {
    if (bOpen != m_bOpen)
        RoseUi2::PlayWindowSound(DLG_TYPE_QUEST, bOpen);
    m_bOpen = bOpen;
    if (bOpen)
        Sample(); /// no stale list on the first frame
}

void
RoseRmlQuestJournal::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

int
RoseRmlQuestJournal::SelectedSlot() const {
    if (g_pAVATAR == NULL || m_iSelectedID <= 0)
        return -1;
    for (int i = 0; i < QUEST_PER_PLAYER; ++i) {
        if (g_pAVATAR->m_Quests.m_QUEST[i].GetID() == m_iSelectedID)
            return i;
    }
    return -1;
}

void
RoseRmlQuestJournal::Sample() {
    CObjUSER* pAvatar = g_pAVATAR;
    if (pAvatar == NULL)
        return;

    /// The selection survives a quest being added or removed. When it is gone
    /// ( completed, abandoned ), the last quest in the list is selected, as the
    /// classic dialog did.
    if (SelectedSlot() < 0) {
        m_iSelectedID = 0;
        for (int i = 0; i < QUEST_PER_PLAYER; ++i) {
            if (pAvatar->m_Quests.m_QUEST[i].GetID() > 0)
                m_iSelectedID = pAvatar->m_Quests.m_QUEST[i].GetID();
        }
    }

    /// --- the list ---------------------------------------------------------------
    std::vector<QuestVM> quests;
    for (int i = 0; i < QUEST_PER_PLAYER; ++i) {
        CQUEST& Quest = pAvatar->m_Quests.m_QUEST[i];
        const int iID = Quest.GetID();
        if (iID <= 0)
            continue;

        QuestVM vm;
        vm.slot = i;
        vm.name = RoseRmlText::FromGame(QUEST_NAME(iID));
        const int iIcon = QUEST_ICON(iID);
        vm.has_icon = iIcon > 0 && RoseRmlIcons::Resolve(IMAGE_RES_STATE_ICON, iIcon, vm.src, vm.rect);
        vm.timed = Quest.GetExpirationTIME() != 0;
        vm.selected = (iID == m_iSelectedID);
        quests.push_back(vm);
    }
    if (quests != m_Quests) {
        m_Quests.swap(quests);
        m_Model.DirtyVariable("quests");
    }
    if ((int)m_Quests.size() != m_iQuestCount) {
        m_iQuestCount = (int)m_Quests.size();
        m_Model.DirtyVariable("count");
    }

    /// --- the selected quest -------------------------------------------------------
    const int iSlot = SelectedSlot();
    const bool bHasSel = iSlot >= 0;
    if (bHasSel != m_bHasSelection) {
        m_bHasSelection = bHasSel;
        m_Model.DirtyVariable("has_sel");
    }

    Rml::String strName, strDesc, strTimer;
    bool bTimed = false, bTimedOut = false;
    std::vector<ItemVM> items;
    if (bHasSel) {
        CQUEST& Quest = pAvatar->m_Quests.m_QUEST[iSlot];
        strName = RoseRmlText::FromGame(QUEST_NAME(m_iSelectedID));
        strDesc = RoseRmlText::FromGame(QUEST_DESC(m_iSelectedID));

        /// Remaining time counts world ticks of 10 s ( CQuestDlg::DrawItems ).
        if (Quest.GetExpirationTIME()) {
            bTimed = true;
            const DWORD dwRemain = Quest.GetRemainTIME();
            if (dwRemain > 0)
                strTimer = Printf("%um %02us", dwRemain / 6, dwRemain % 6 * 10);
            else
                bTimedOut = true;
        }

        for (int i = 0; i < QUEST_ITEM_PER_QUEST; ++i) {
            ItemVM vm;
            vm.index = i;
            vm.filled = false;
            vm.count = 0;
            tagBaseITEM* pItem = Quest.GetSlotITEM(i);
            if (pItem && !pItem->IsEmpty()
                && RoseRmlIcons::Resolve(IMAGE_RES_ITEM,
                    ITEM_ICON_NO(pItem->GetTYPE(), pItem->GetItemNO()), vm.src, vm.rect)) {
                vm.filled = true;
                if (pItem->IsEnableDupCNT())
                    vm.count = pItem->GetQuantity();
            }
            items.push_back(vm);
        }
    }

    bool bHasItems = false;
    for (size_t i = 0; i < items.size(); ++i)
        bHasItems = bHasItems || items[i].filled;

    if (strName != m_strName) {
        m_strName.swap(strName);
        m_Model.DirtyVariable("name");
    }
    if (strDesc != m_strDesc) {
        m_strDesc.swap(strDesc);
        m_Model.DirtyVariable("desc");
    }
    if (bTimed != m_bTimed) {
        m_bTimed = bTimed;
        m_Model.DirtyVariable("timed");
    }
    if (bTimedOut != m_bTimedOut) {
        m_bTimedOut = bTimedOut;
        m_Model.DirtyVariable("timed_out");
    }
    if (strTimer != m_strTimer) {
        m_strTimer.swap(strTimer);
        m_Model.DirtyVariable("timer");
    }
    if (items != m_Items) {
        m_Items.swap(items);
        m_Model.DirtyVariable("items");
    }
    if (bHasItems != m_bHasItems) {
        m_bHasItems = bHasItems;
        m_Model.DirtyVariable("has_items");
    }

    /// No abandoning while an NPC dialog is open ( the classic dialog
    /// disabled its button for the same reason: the dialog may be about to
    /// act on the quest ).
    const bool bCanAbandon = !g_itMGR.IsDlgOpened(DLG_TYPE_DIALOG);
    if (bCanAbandon != m_bCanAbandon) {
        m_bCanAbandon = bCanAbandon;
        m_Model.DirtyVariable("can_abandon");
    }
}

void
RoseRmlQuestJournal::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iIndex = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("qitem")) {
            iIndex = pEl->GetAttribute<int>("qitem", -1);
            break;
        }
    }
    const int iSlot = SelectedSlot();
    if (iIndex < 0 || iSlot < 0)
        return;

    tagBaseITEM* pItem = g_pAVATAR->m_Quests.m_QUEST[iSlot].GetSlotITEM(iIndex);
    if (pItem == NULL || pItem->IsEmpty())
        return;

    /// The classic quest item tooltip: name, then the description wrapped.
    CInfo ToolTip;
    ToolTip.Clear();
    ToolTip.AddString(ITEM_NAME(pItem->GetTYPE(), pItem->GetItemNO()));
    ToolTip.AddWrappedString(ITEM_DESC(pItem->GetTYPE(), pItem->GetItemNO()));

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlQuestJournal::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
    if (!bInWorld)
        m_bOpen = false;

    SetVisible(m_bOpen && bInWorld);
    if (!m_bVisible)
        return;

    Sample();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateTooltip();
}
