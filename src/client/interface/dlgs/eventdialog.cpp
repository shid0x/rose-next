#include "stdafx.h"

#include ".\eventdialog.h"
#include "../../Object.h"
#include "subclass/CDialogNpcScriptItem.h"
#include "subclass/CDialogNpcScriptAnswerItem.h"
#include "subclass/CDialogNpcScriptAnswerItemNew.h"
#include "../IO_ImageRes.h"

#include "tgamectrl/resourcemgr.h"
#include "tgamectrl/tcaption.h"
#include "tgamectrl/zlistbox.h"
#include "tgamectrl/timage.h"
#include "tgamectrl/tbutton.h"

const int BG_IMAGE_HEIGHT = 150;
CEventDialog::CEventDialog(void) {
    m_hNpcFace = NULL;

    m_heightNpcFace = 0;
    m_widthNpcFace = 0;
}

CEventDialog::~CEventDialog(void) {}
void
CEventDialog::SetScript(char* pszScript) {
    assert(pszScript);
    if (pszScript == NULL)
        return;

    CTCaption* pCaption = GetCaption();
    assert(pCaption);
    if (pCaption == NULL)
        return;
    SetHeight(pCaption->GetHeight());

    CWinCtrl* pCtrl = Find(IID_ZLISTBOX_NPCSCRIPT);
    assert(pCtrl);
    if (pCtrl == NULL)
        return;

    int iListBoxHeight = 0;
    CZListBox* pListBox = (CZListBox*)pCtrl;
    pListBox->Clear();

    int iWidth = 244;
    int iLineHeight = 21;
    int iImageID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "EVENT_DIALOG_UPSIDE_MIDDLE");
    CDialogNpcScriptItem* pItem =
        new CDialogNpcScriptItem(1, pszScript, iImageID, iLineHeight, iWidth, 30);
    pListBox->Add(pItem);
    iListBoxHeight += pItem->GetHeight();

    iImageID = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "EVENT_DIALOG_UPSIDE_BOTTOM");
    iLineHeight = 12;
    pItem = new CDialogNpcScriptItem(0, NULL, iImageID, iLineHeight, iWidth, 0);
    pListBox->Add(pItem);
    iListBoxHeight += pItem->GetHeight();

    pListBox->SetHeight(iListBoxHeight);
    SetHeight(GetHeight() + iListBoxHeight);

    pListBox->SetValue(0);
    pListBox->SetExtent(2);

    ///답변 예제를 담을 ListBox의 위치를 조정한다.
    pCtrl = Find(IID_ZLISTBOX_ANSWER_EXAMPLE);
    assert(pCtrl);
    if (pCtrl == NULL)
        return;

    pListBox = (CZListBox*)pCtrl;
    pListBox->SetOffset(0, GetHeight());
    MoveWindow(m_sPosition);

    pListBox->Clear();

    int iAnswerListBoxHeight = 0;
    iImageID = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "EVENT_DIALOG_UNDERSIDE_TOP");
    iLineHeight = 10;
    iWidth = 244;
    CDialogNpcScriptAnswerItem* pAnswerItem =
        new CDialogNpcScriptAnswerItem(0, NULL, 0, NULL, iImageID, iLineHeight, iWidth);
    pListBox->Add(pAnswerItem);
    iAnswerListBoxHeight += pAnswerItem->GetHeight();

    iImageID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "EVENT_DIALOG_UNDERSIDE_BOTTOM");
    iLineHeight = 12;
    pAnswerItem = new CDialogNpcScriptAnswerItem(0, NULL, 0, NULL, iImageID, iLineHeight, iWidth);
    pListBox->Add(pAnswerItem);
    iAnswerListBoxHeight += pAnswerItem->GetHeight();

    iImageID = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "EVENT_DIALOG_FOOT");
    iLineHeight = 3;
    pAnswerItem = new CDialogNpcScriptAnswerItem(0, NULL, 0, NULL, iImageID, iLineHeight, iWidth);
    pListBox->Add(pAnswerItem);
    iAnswerListBoxHeight += pAnswerItem->GetHeight();

    pListBox->SetExtent(3);
    pListBox->SetValue(0);
    pListBox->SetHeight(iAnswerListBoxHeight);
    SetHeight(GetHeight() + iAnswerListBoxHeight);
}

void
CEventDialog::AddAnswerExample(char* pszScript, int iEventID, void (*fpEventHandle)(int iEventID)) {
    CWinCtrl* pCtrl = Find(IID_ZLISTBOX_ANSWER_EXAMPLE);
    assert(pCtrl);
    if (pCtrl == NULL)
        return;
    assert(pszScript);
    if (pszScript == NULL)
        return;
    CZListBox* pListBox = (CZListBox*)pCtrl;
    int iItemCount = pListBox->GetSize();

    if (iItemCount <= 0)
        return;

    int iImageID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "EVENT_DIALOG_UNDERSIDE_MIDDLE");
    int iHeight = 20;
    int iWidth = 244;
    CDialogNpcScriptAnswerItem* pAnswerItem = new CDialogNpcScriptAnswerItem(iItemCount - 2,
        pszScript,
        iEventID,
        fpEventHandle,
        iImageID,
        iHeight,
        iWidth);

    pListBox->InsertItem(iItemCount - 2, pAnswerItem);
    pListBox->SetExtent(pListBox->GetExtent() + 1);

    pListBox->SetValue(0);
    pListBox->SetHeight(pListBox->GetHeight() + pAnswerItem->GetHeight());

    SetHeight(GetHeight() + pAnswerItem->GetHeight());
}

unsigned
CEventDialog::Process(unsigned uiMsg, WPARAM wParam, LPARAM lParam) {
    if (!IsVision())
        return 0;
    if (unsigned uiProcID = CTDialog::Process(uiMsg, wParam, lParam)) {
        if (uiProcID == IID_BTN_CLOSE && uiMsg == WM_LBUTTONUP)
            Hide();

        return uiMsg;
    }
    return 0;
}

void
CEventDialog::SetTargetNpcClientObjectIndex(short iIndex) {}

int
CEventDialog::IsInValidShow() {
    if (g_pAVATAR && g_pAVATAR->Get_HP() <= 0)
        return 99;

    if (g_itMGR.IsDlgOpened(DLG_TYPE_DEAL))
        return DLG_TYPE_DEAL;

    if (g_itMGR.IsDlgOpened(DLG_TYPE_EXCHANGE))
        return DLG_TYPE_EXCHANGE;

    if (g_itMGR.IsDlgOpened(DLG_TYPE_DIALOG))
        return DLG_TYPE_DIALOG;

    if (g_itMGR.IsDlgOpened(DLG_TYPE_MAKE))
        return DLG_TYPE_MAKE;

    return 0;
}

void
CEventDialog::Hide() {
    CTDialog::Hide();
    CZListBox* pListBox = NULL;
    CWinCtrl* pCtrl = Find(IID_ZLISTBOX_NPCSCRIPT);
    assert(pCtrl);
    if (pCtrl) {
        pListBox = (CZListBox*)pCtrl;
        pListBox->Clear();
    }

    pCtrl = Find(IID_ZLISTBOX_ANSWER_EXAMPLE);
    assert(pCtrl);
    if (pCtrl) {
        pListBox = (CZListBox*)pCtrl;
        pListBox->Clear();
    }

    CTCaption* pCaption = GetCaption();
    if (pCaption)
        SetHeight(pCaption->GetHeight());
}

void
CEventDialog::Draw() {
    if (!IsVision())
        return;
    CTDialog::Draw();

    if (!m_strNpcName.empty()) {
        D3DXMATRIX mat;
        D3DXMatrixTranslation(&mat, (float)m_sPosition.x, (float)m_sPosition.y, 0.0f);
        ::setTransformSprite(mat);
        ::drawFont(g_GameDATA.m_hFONT[FONT_NORMAL_BOLD],
            true,
            50,
            5,
            g_dwWHITE,
            m_strNpcName.c_str());
    }
}

void
CEventDialog::Show() {
    CTDialog::Show();
}

bool
CEventDialog::Create(const char* IDD) {
    if (CTDialog::Create(IDD)) {
        return true;
    }
    return false;
}