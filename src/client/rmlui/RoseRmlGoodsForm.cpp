#include "stdafx.h"

#include "RoseRmlGoodsForm.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>

#include "../Game.h"
#include "../CObjUSER.h"
#include "../System/CGame.h"
#include "../misc/gameutil.h"
#include "../gamecommon/item.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/Dlgs/CGoodsDlg.h"
#include "tgamectrl/teditbox.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

Rml::String
Money(__int64 iMoney) {
    char szBuf[64];
    CGameUtil::ConvertMoney2String(iMoney, szBuf, sizeof(szBuf));
    return szBuf;
}

Rml::String
Number(__int64 i) {
    char szBuf[32];
    _i64toa(i, szBuf, 10);
    return szBuf;
}

/// A field's text as a number ( digits only: the field is numeric ).
__int64
FieldValue(Rml::Element* pField) {
    if (pField == NULL)
        return 0;
    const Rml::String str = pField->GetAttribute<Rml::String>("value", "");
    return str.empty() ? 0 : _atoi64(str.c_str());
}

} // namespace

RoseRmlGoodsForm::RoseRmlGoodsForm():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bPlace(false),
    m_bFocus(false),
    m_iMaxQuantity(0),
    m_strTitle("Sell"),
    m_bSelling(true),
    m_bStack(false),
    m_strTotal("0"),
    m_bValid(false),
    m_bShort(false) {
    m_ptOpen.x = 0;
    m_ptOpen.y = 0;
}

bool
RoseRmlGoodsForm::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("goods");
    if (!constructor)
        return false;

    constructor.Bind("title", &m_strTitle);
    constructor.Bind("selling", &m_bSelling);
    constructor.Bind("item_name", &m_strItemName);
    constructor.Bind("item_src", &m_strItemSrc);
    constructor.Bind("item_rect", &m_strItemRect);
    constructor.Bind("have", &m_strHave);
    constructor.Bind("stack", &m_bStack);
    constructor.Bind("total", &m_strTotal);
    constructor.Bind("valid", &m_bValid);
    constructor.Bind("short", &m_bShort);

    constructor.BindEventCallback("ok",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(true); });
    constructor.BindEventCallback("cancel",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(false); });
    /// Enter in a field: RmlUi's single-line input reports it as a change
    /// with "linebreak" set.
    constructor.BindEventCallback("edited",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<bool>("linebreak", false))
                Answer(true);
        });
    /// Escape in a field ( RoseRmlUi hands the key down first ).
    constructor.BindEventCallback("key",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<int>("key_identifier", 0) == Rml::Input::KI_ESCAPE)
                Answer(false);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "goods.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load goods form document '{}'", strDoc.c_str());
        return false;
    }

    /// Not tracked: it opens where it is needed each time.
    m_pPanel = m_pDocument->GetElementById("goods");

    LOG_INFO("[rmlui] goods form document loaded");
    return true;
}

void
RoseRmlGoodsForm::Shutdown() {
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CGoodsDlg*
RoseRmlGoodsForm::Dlg() const {
    return (CGoodsDlg*)g_itMGR.FindDlg(DLG_TYPE_GOODS);
}

void
RoseRmlGoodsForm::SetOpen(bool bOpen) {
    if (!bOpen) {
        if (m_bOpen)
            RoseUi2::PlayWindowSound(DLG_TYPE_GOODS, false);
        m_bOpen = false;
        return;
    }

    CGoodsDlg* pDlg = Dlg();
    if (pDlg == NULL || pDlg->GetIcon() == NULL)
        return; /// nothing asked

    if (!m_bOpen)
        RoseUi2::PlayWindowSound(DLG_TYPE_GOODS, true);
    m_bOpen = true;

    Describe();

    /// The classic defaults, in the fields.
    if (m_pDocument != NULL) {
        if (Rml::Element* pPrice = m_pDocument->GetElementById("price"))
            pPrice->SetAttribute("value", Number(pDlg->GetDefaultPrice()));
        if (Rml::Element* pQty = m_pDocument->GetElementById("quantity"))
            pQty->SetAttribute("value", Number(pDlg->GetDefaultQuantity()));
    }
    Recount();

    CGame::GetInstance().Get_MousePos(m_ptOpen);
    m_bPlace = true;
    m_bFocus = true;
}

/// The item and the kind of question, from the hidden dialog.
void
RoseRmlGoodsForm::Describe() {
    CGoodsDlg* pDlg = Dlg();
    CIcon* pIcon = pDlg ? pDlg->GetIcon() : NULL;
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;

    tagITEM& Item = ((CIconItem*)pIcon)->GetItem();
    m_bSelling = pDlg->GetType() == CGoodsDlg::ADD_SELLLIST;
    m_strTitle = m_bSelling ? "Sell in your shop" : "Buy in your shop";
    m_strItemName = RoseRmlText::FromGame(pIcon->GetName());
    m_strItemSrc.clear();
    m_strItemRect.clear();
    int iModule = 0, iGraphic = 0;
    if (pIcon->GetSprite(iModule, iGraphic))
        RoseRmlIcons::Resolve(iModule, iGraphic, m_strItemSrc, m_strItemRect);

    /// Only a stack has a quantity to ask ( CPrivateStore sends it for those
    /// only ): selling, up to what you have; buying, as many as you like.
    m_bStack = Item.IsEnableDupCNT() != 0;
    m_iMaxQuantity = (m_bSelling && m_bStack) ? (int)Item.GetQuantity() : 0;
    m_strHave = (m_bSelling && m_bStack) ? ("You have " + Money(Item.GetQuantity())) : Rml::String();

    m_Model.DirtyVariable("title");
    m_Model.DirtyVariable("selling");
    m_Model.DirtyVariable("item_name");
    m_Model.DirtyVariable("item_src");
    m_Model.DirtyVariable("item_rect");
    m_Model.DirtyVariable("have");
    m_Model.DirtyVariable("stack");
}

/// The total and whether OK can be pressed, from what is typed.
void
RoseRmlGoodsForm::Recount() {
    if (m_pDocument == NULL)
        return;
    const __int64 iPrice = FieldValue(m_pDocument->GetElementById("price"));
    __int64 iQty = m_bStack ? FieldValue(m_pDocument->GetElementById("quantity")) : 1;
    if (m_iMaxQuantity > 0 && iQty > m_iMaxQuantity)
        iQty = m_iMaxQuantity; /// CGoodsDlg::Confirm clamps the same way

    const Rml::String strTotal = Money(iPrice * iQty);
    const bool bValid = iPrice > 0 && iQty > 0;
    const bool bShort = !m_bSelling && g_pAVATAR != NULL && iPrice * iQty > g_pAVATAR->Get_MONEY();

    if (strTotal != m_strTotal) {
        m_strTotal = strTotal;
        m_Model.DirtyVariable("total");
    }
    if (bValid != m_bValid) {
        m_bValid = bValid;
        m_Model.DirtyVariable("valid");
    }
    if (bShort != m_bShort) {
        m_bShort = bShort;
        m_Model.DirtyVariable("short");
    }
}

void
RoseRmlGoodsForm::Answer(bool bOk) {
    if (!m_bOpen)
        return;

    int iPrice = 0, iQty = 0;
    if (m_pDocument != NULL) {
        const __int64 iP = FieldValue(m_pDocument->GetElementById("price"));
        const __int64 iQ = m_bStack ? FieldValue(m_pDocument->GetElementById("quantity")) : 1;
        iPrice = (int)((iP > 0x7fffffff) ? 0x7fffffff : iP);
        iQty = (int)((iQ > 0x7fffffff) ? 0x7fffffff : iQ);
    }
    if (bOk && (iPrice <= 0 || iQty <= 0))
        return; /// nothing to confirm yet: stay open

    /// Closed first: the confirmation may ask something of its own.
    m_bOpen = false;
    RoseUi2::PlayWindowSound(DLG_TYPE_GOODS, false);
    if (bOk) {
        if (CGoodsDlg* pDlg = Dlg())
            pDlg->Confirm(iPrice, iQty);
    }
}

void
RoseRmlGoodsForm::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlGoodsForm::PlaceAtMouse() {
    /// Centred over where it was asked ( the drop ), a bit above it, on whole
    /// pixels -- as the quantity question.
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet
    m_bPlace = false;

    const float fLeft = floorf((float)m_ptOpen.x - size.x * 0.5f);
    const float fTop = floorf((float)m_ptOpen.y - size.y * 0.35f);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

/// The caret in the price, all of it selected: typing replaces the default.
void
RoseRmlGoodsForm::FocusPrice() {
    m_bFocus = false;
    Rml::Element* pPrice = m_pDocument->GetElementById("price");
    if (pPrice == NULL)
        return;
    pPrice->Focus();
    if (Rml::ElementFormControlInput* pInput = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(pPrice))
        pInput->Select();
    /// The classic chat box stops listening, or both would show a caret.
    if (CTEditBox::s_pFocusEdit != NULL)
        CTEditBox::s_pFocusEdit->SetFocus(false);
}

void
RoseRmlGoodsForm::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
    if (m_bOpen && !bInWorld)
        Answer(false);

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    if (m_bPlace)
        PlaceAtMouse();
    if (m_bFocus)
        FocusPrice();

    Recount();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
