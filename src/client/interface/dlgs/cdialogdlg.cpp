#include "stdafx.h"

#include ".\cdialogdlg.h"
#include "../../Object.h"
#include "subclass/CDialogNpcScriptItem.h"
#include "subclass/CDialogNpcScriptAnswerItem.h"
#include "subclass/CDialogNpcScriptAnswerItemNew.h"
#include "../IO_ImageRes.h"
#include "CMinimapDlg.h"
#include "tgamectrl/resourcemgr.h"
#include "tgamectrl/tcaption.h"
#include "tgamectrl/zlistbox.h"
#include "tgamectrl/timage.h"
#include "tgamectrl/tbutton.h"
#include "zz_interface.h"
#include "../../System/CGame.h"

const int BG_IMAGE_HEIGHT = 150;
CDialogDlg::CDialogDlg(void) {
    m_hNpcFace = NULL;
    m_heightNpcFace = 0;
    m_widthNpcFace = 0;
}

CDialogDlg::~CDialogDlg(void) {}

void
CDialogDlg::SetScript(char* pszScript) {
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

    int iWidth = 350;
    int iImageID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_NPC_SCRIPT_IMAGE_TOP");
    int iLineHeight = 16;
    CDialogNpcScriptItem* pItem =
        new CDialogNpcScriptItem(0, NULL, iImageID, iLineHeight, iWidth, 0);
    pListBox->Add(pItem);
    iListBoxHeight += pItem->GetHeight();

    iImageID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_NPC_SCRIPT_IMAGE_MIDDLE");
    iLineHeight = 20;
    pItem = new CDialogNpcScriptItem(1, pszScript, iImageID, iLineHeight, iWidth, 25);
    pListBox->Add(pItem);
    iListBoxHeight += pItem->GetHeight();

    iImageID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_NPC_SCRIPT_IMAGE_BOTTOM");
    iLineHeight = 16;
    pItem = new CDialogNpcScriptItem(-1, NULL, iImageID, iLineHeight, iWidth, 0);
    pListBox->Add(pItem);
    iListBoxHeight += pItem->GetHeight();

    pListBox->SetHeight(iListBoxHeight);
    SetHeight(GetHeight() + iListBoxHeight);

    pListBox->SetValue(0);
    pListBox->SetExtent(3);

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
    iImageID = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_NPC_SCRIPT_ANSWER_TOP");
    iLineHeight = 10;
    iWidth = 350;
    CDialogNpcScriptAnswerItem* pAnswerItem =
        new CDialogNpcScriptAnswerItem(0, NULL, 0, NULL, iImageID, iLineHeight, iWidth);
    pListBox->Add(pAnswerItem);
    iAnswerListBoxHeight += pAnswerItem->GetHeight();

    iImageID =
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_NPC_SCRIPT_ANSWER_BOTTOM");
    iLineHeight = 17;
    pAnswerItem = new CDialogNpcScriptAnswerItem(-1, NULL, 0, NULL, iImageID, iLineHeight, iWidth);
    pListBox->Add(pAnswerItem);
    iAnswerListBoxHeight += pAnswerItem->GetHeight();

    pListBox->SetExtent(2);
    pListBox->SetValue(0);
    pListBox->SetHeight(iAnswerListBoxHeight);
    SetHeight(GetHeight() + iAnswerListBoxHeight);
}

void
CDialogDlg::AddAnswerExample(char* pszScript, int iEventID, void (*fpEventHandle)(int iEventID)) {
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
        CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI13_NPC_SCRIPT_ANSWER_MIDDLE");
    int iHeight = 25;
    int iWidth = 350;
    CDialogNpcScriptAnswerItem* pAnswerItem = new CDialogNpcScriptAnswerItem(iItemCount - 1,
        pszScript,
        iEventID,
        fpEventHandle,
        iImageID,
        iHeight,
        iWidth);

    pListBox->InsertItem(iItemCount - 1, pAnswerItem);
    pListBox->SetExtent(pListBox->GetExtent() + 1);

    pListBox->SetValue(0);
    pListBox->SetHeight(pListBox->GetHeight() + pAnswerItem->GetHeight());

    SetHeight(GetHeight() + pAnswerItem->GetHeight());
}

unsigned
CDialogDlg::Process(unsigned uiMsg, WPARAM wParam, LPARAM lParam) {
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
CDialogDlg::Update(POINT ptMouse) {
    if (!IsVision())
        return;

    CTDialog::Update(ptMouse);
    CGameOBJ* pObj = g_pObjMGR->Get_OBJECT(m_nTargetClientIdx);

    if (pObj == NULL)
        Hide();
    else if (g_pAVATAR->Get_DISTANCE(pObj->Get_CurPOS()) >= 1000)
        Hide();
}

void
CDialogDlg::SetTargetNpcClientObjectIndex(short iIndex) {
    m_nTargetClientIdx = iIndex;
    CGameOBJ* pObj = g_pObjMGR->Get_OBJECT(iIndex);
    if (pObj && pObj->IsA(OBJ_NPC)) {
        m_strNpcName = pObj->Get_NAME();
        CObjNPC* p = (CObjNPC*)pObj;

        if (CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_MINIMAP)) {
            CMinimapDLG* pMinimap = (CMinimapDLG*)pDlg;
            if (pMinimap->GetIndicatorNpc(p->Get_CharNO()))
                pMinimap->RemoveAutodeleteIndicatorNpc(p->Get_CharNO());
        }

        m_Script.Clear();
        if (!m_strTempScript.empty()) {
            m_Script.SetSplitType(CJStringParser::SPLIT_WORD);
            m_Script.SetDefaultColor(g_dwWHITE);
            m_Script.SetDefaultFont(FONT_OUTLINE_11_BOLD);
            m_Script.SetString(m_strTempScript.c_str(), GetWidth() - (m_widthNpcFace + 100));
        }
    }
}

int
CDialogDlg::IsInValidShow() {
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
CDialogDlg::Hide() {
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
CDialogDlg::Draw() {
    if (!IsVision())
        return;

    // 홍근 : 히어로 퀘스트 추가.
    if (m_fNpctalkinterfaceHideTime > 0) {
        m_fNpctalkinterfaceHideTime -= g_GameDATA.GetElapsedFrameTime();
        return;
    }

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

short
CDialogDlg::GetTargetNpcClientObjectIndex() {
    return m_nTargetClientIdx;
}

void
CDialogDlg::Show() {
    CTDialog::Show();
}

bool
CDialogDlg::Create(const char* IDD) {
    if (CTDialog::Create(IDD)) {
        return true;
    }
    return false;
}

void
CDialogDlg::SetNpcFace(HNODE hNode_) {
    m_hNpcFace = hNode_;
}

void
CDialogDlg::SetNpcName(std::string strName) {
    m_strNpcName = strName;
}

//홍근 : 히어로 퀘스트
void
CDialogDlg::SetNpctalkinterfaceHide(float fTime) {
    m_fNpctalkinterfaceHideTime = fTime;
}

//홍근 : 히어로 퀘스트
float
CDialogDlg::GetNpctalkinterfaceHide() {
    return m_fNpctalkinterfaceHideTime;
}
