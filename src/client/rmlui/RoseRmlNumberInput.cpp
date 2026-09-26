#include "stdafx.h"

#include "RoseRmlNumberInput.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../Game.h"
#include "../CObjUSER.h"
#include "../System/CGame.h"
#include "../misc/gameutil.h"
#include "../gamecommon/item.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Dlgs/CNumberInputDlg.h"

#include "rose/common/log.h"
#include "rose/io/stb.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

/// Enough digits for any stack or purse; more is a typo.
const size_t kMaxDigits = 12;

Rml::String
Money(__int64 iMoney) {
    char szBuf[64];
    CGameUtil::ConvertMoney2String(iMoney, szBuf, sizeof(szBuf));
    return szBuf;
}

} // namespace

RoseRmlNumberInput::RoseRmlNumberInput():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bPlace(false),
    m_SwallowChar(0),
    m_iMax(0),
    m_strTitle("How many?"),
    m_bHasItem(false),
    m_strValue("1"),
    m_bFresh(true) {
    m_ptOpen.x = 0;
    m_ptOpen.y = 0;
}

bool
RoseRmlNumberInput::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("numinput");
    if (!constructor)
        return false;

    constructor.Bind("title", &m_strTitle);
    constructor.Bind("has_item", &m_bHasItem);
    constructor.Bind("item_name", &m_strItemName);
    constructor.Bind("item_src", &m_strItemSrc);
    constructor.Bind("item_rect", &m_strItemRect);
    constructor.Bind("max", &m_strMax);
    constructor.Bind("value", &m_strValue);
    constructor.Bind("fresh", &m_bFresh);

    /// digit(n)
    constructor.BindEventCallback("digit",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                Type(args[0].Get<int>());
        });
    constructor.BindEventCallback("erase",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Erase(); });
    constructor.BindEventCallback("ok",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(true, false); });
    constructor.BindEventCallback("max",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(true, true); });
    constructor.BindEventCallback("cancel",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(false, false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "numinput.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load number input document '{}'", strDoc.c_str());
        return false;
    }

    /// Not tracked: it opens where it is needed each time.
    m_pPanel = m_pDocument->GetElementById("numinput");

    LOG_INFO("[rmlui] number input document loaded");
    return true;
}

void
RoseRmlNumberInput::Shutdown() {
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CNumberInputDlg*
RoseRmlNumberInput::Dlg() const {
    return (CNumberInputDlg*)g_itMGR.FindDlg(DLG_TYPE_N_INPUT);
}

void
RoseRmlNumberInput::SetOpen(bool bOpen) {
    CNumberInputDlg* pDlg = Dlg();

    if (!bOpen) {
        if (!m_bOpen)
            return;
        m_bOpen = false;
        RoseUi2::PlayWindowSound(DLG_TYPE_N_INPUT, false);
        if (pDlg)
            pDlg->Cancel();
        return;
    }

    if (pDlg == NULL || !pDlg->HasCommand())
        return; /// nothing asked

    if (!m_bOpen)
        RoseUi2::PlayWindowSound(DLG_TYPE_N_INPUT, true);
    m_bOpen = true;

    m_iMax = pDlg->GetMaxNumber();
    Describe();
    SetValue("1");
    m_bFresh = true;
    m_Model.DirtyVariable("fresh");

    CGame::GetInstance().Get_MousePos(m_ptOpen);
    m_bPlace = true;
}

/// What is being counted: the item the command is for, or money.
void
RoseRmlNumberInput::Describe() {
    CNumberInputDlg* pDlg = Dlg();
    CTObject* pParam = pDlg ? pDlg->GetCommandParam() : NULL;

    bool bHasItem = false;
    Rml::String strName, strSrc, strRect;
    if (pParam != NULL && strcmp(pParam->toString(), "CItem") == 0) {
        CItem* pItem = (CItem*)pParam;
        tagITEM& Item = pItem->GetItem();
        if (!Item.IsEmpty()) {
            bHasItem = true;
            strName = RoseRmlText::FromGame(pItem->GetName());
            RoseRmlIcons::Resolve(
                IMAGE_RES_ITEM, ITEM_ICON_NO(Item.GetTYPE(), Item.GetItemNO()), strSrc, strRect);
        }
    }

    m_bHasItem = bHasItem;
    m_strItemName = strName;
    m_strItemSrc = strSrc;
    m_strItemRect = strRect;
    m_strTitle = bHasItem ? "How many?" : "How much?";
    m_strMax = (m_iMax >= 0) ? Money(m_iMax) : Rml::String();

    m_Model.DirtyVariable("has_item");
    m_Model.DirtyVariable("item_name");
    m_Model.DirtyVariable("item_src");
    m_Model.DirtyVariable("item_rect");
    m_Model.DirtyVariable("title");
    m_Model.DirtyVariable("max");
}

void
RoseRmlNumberInput::SetValue(const Rml::String& strValue) {
    if (strValue != m_strValue) {
        m_strValue = strValue;
        m_Model.DirtyVariable("value");
    }
}

__int64
RoseRmlNumberInput::Value() const {
    return m_strValue.empty() ? 0 : _atoi64(m_strValue.c_str());
}

void
RoseRmlNumberInput::Type(int iDigit) {
    if (iDigit < 0 || iDigit > 9)
        return;

    Rml::String strValue = m_bFresh ? Rml::String() : m_strValue;
    if (m_bFresh) {
        m_bFresh = false;
        m_Model.DirtyVariable("fresh");
    }
    if (strValue == "0")
        strValue.clear();
    if (strValue.size() >= kMaxDigits)
        return;
    strValue += (char)('0' + iDigit);

    /// Past the maximum snaps to it, so what is shown is what will be done.
    if (m_iMax >= 0 && _atoi64(strValue.c_str()) > m_iMax) {
        char szMax[32];
        _i64toa(m_iMax, szMax, 10);
        strValue = szMax;
    }
    SetValue(strValue);
}

void
RoseRmlNumberInput::Erase() {
    if (m_bFresh) {
        m_bFresh = false;
        m_Model.DirtyVariable("fresh");
        SetValue(Rml::String());
        return;
    }
    if (!m_strValue.empty())
        SetValue(m_strValue.substr(0, m_strValue.size() - 1));
}

void
RoseRmlNumberInput::Answer(bool bOk, bool bMax) {
    if (!m_bOpen)
        return;

    CNumberInputDlg* pDlg = Dlg();
    m_bOpen = false;
    RoseUi2::PlayWindowSound(DLG_TYPE_N_INPUT, false);
    if (pDlg == NULL)
        return;

    if (!bOk) {
        pDlg->Cancel();
        return;
    }

    /// The command may ask again ( a message box, another question ): the
    /// window is closed first so that is a fresh open, not this one's.
    pDlg->Submit(bMax ? m_iMax : Value());
}

bool
RoseRmlNumberInput::ProcessKey(UINT uiMsg, WPARAM wParam) {
    if (uiMsg == WM_CHAR) {
        /// The character of an Enter / Escape answered on its key-down: the
        /// window is already shut, but it would open the chat.
        if (m_SwallowChar != 0 && wParam == m_SwallowChar) {
            m_SwallowChar = 0;
            return true;
        }
        if (!m_bOpen)
            return false;
        /// Everything typeable is ours while the question is up: the digits
        /// are handled on key-down, the rest must not reach the chat.
        return wParam >= 0x20 || wParam == VK_BACK || wParam == VK_RETURN || wParam == VK_ESCAPE;
    }

    if (uiMsg != WM_KEYDOWN || !m_bOpen)
        return false;

    m_SwallowChar = 0;
    if (wParam >= '0' && wParam <= '9') {
        Type((int)(wParam - '0'));
        return true;
    }
    if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) {
        Type((int)(wParam - VK_NUMPAD0));
        return true;
    }
    switch (wParam) {
        case VK_BACK:
        case VK_DELETE:
            Erase();
            return true;
        case VK_RETURN:
            m_SwallowChar = VK_RETURN;
            Answer(true, false);
            return true;
        case VK_ESCAPE:
            m_SwallowChar = VK_ESCAPE;
            Answer(false, false);
            return true;
        default:
            return false;
    }
}

void
RoseRmlNumberInput::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlNumberInput::PlaceAtMouse() {
    /// Centred over where it was asked ( the drop, the double-click ), a bit
    /// above it, on whole pixels.
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet
    m_bPlace = false;

    const float fLeft = floorf((float)m_ptOpen.x - size.x * 0.5f);
    const float fTop = floorf((float)m_ptOpen.y - size.y * 0.35f);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlNumberInput::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
    if (m_bOpen && !bInWorld)
        Answer(false, false);

    /// The question lost its command elsewhere ( a classic path hid it ).
    if (m_bOpen) {
        CNumberInputDlg* pDlg = Dlg();
        if (pDlg == NULL || !pDlg->HasCommand()) {
            m_bOpen = false;
        }
    }

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    if (m_bPlace)
        PlaceAtMouse();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
