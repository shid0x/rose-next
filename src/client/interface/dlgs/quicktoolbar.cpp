#include "StdAfx.h"

#include "QuickToolBAR.h"
#include "../CDragItem.h"
#include "../CTDrawImpl.h"
#include "../IO_ImageRes.h"
#include "../../GameCommon/Skill.h"

#include "../../OBJECT.h"
#include "../../Network/CNetwork.h"
#include "rose/io/stb.h"

#include "../../System/CGame.h"
#include "../../JCommandState.h"
#include "../../GameCommon/CSkillCommand.h"
#include "../../gamecommon/item.h"
#include "../Icon/CIconQuick.h"
#include "../Icon/CIconItem.h"
#include "../command/dragcommand.h"

#include "../../GameData/Event/CTEventItem.h"

#include "tgamectrl/tpane.h"
#include "tgamectrl/teditbox.h"
#include "tgamectrl/timage.h"
#include "tgamectrl/resourcemgr.h"
#include "tgamectrl/tcontrolmgr.h"

CQuickBAR::CQuickBAR(int iType) {
    SetDialogType(iType);
    m_nCurrentPage = 0;
    m_nType = TYPE_HORIZONTAL;

    m_pDragItem = new CDragItem;
    CTCommand* pCmd = new CTCmdDragItemFromQuickBar;
    m_pDragItem->AddTarget(CDragItem::TARGET_ALL, pCmd);
    // pCmd = new CTCmdMoveIconInQuickBar;
    pCmd = new CTCmdMoveIconInQuickBar(iType); // EVO
    m_pDragItem->AddTarget(iType, pCmd);

    m_pCmdDragItemFromQuickBar = new CTCmdDragItemFromQuickBar;

    for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i) {
        m_QuickSlot[i].SetParent(iType);
        m_QuickSlot[i].SetDragAvailable();
        m_QuickSlot[i].SetDragItem(m_pDragItem);
        //	m_QuickSlot[i].SetControlID( IID_SLOT + i ); // c'est quoi?
        //	this->Add( &m_QuickSlot[i] );
    }

    SetQuickBarType(QUICKBAR_TYPE_NORMAL); // garnet add
}

CQuickBAR::~CQuickBAR() {
    if (m_pDragItem) {
        delete m_pDragItem;
        m_pDragItem = NULL;
    }
}

void
CQuickBAR::Show() {
    CTDialog::Show();
    ChangeType(m_nType);
}

void
CQuickBAR::Draw() {
    if (!IsVision())
        return;

    CTDialog::Draw();

    /// To show a gauge progress..
    ::endSprite();
    ::beginSprite(D3DXSPRITE_ALPHABLEND);

    /// quick slot rendering
    DrawQuickSlot();

    CWinCtrl* pCtrl = NULL;
    for (int i = 0; i < IID_FUNC_TXT_CNT; i++) {
        if (pCtrl = Find(i + IID_FUNC_TXT)) {
            pCtrl->Draw();
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief °¢ CSlot µéÀÇ MoveWindow È£Ãâ..
//----------------------------------------------------------------------------------------------------

void
CQuickBAR::MoveWindow(POINT pt) {
    /// Offset first settings ..
    UpdateCSlotPosition();

    CTDialog::MoveWindow(pt);

    for (int i = 0; i < HOT_ICONS_PER_PAGE; i++) {
        m_QuickSlot[i].MoveWindow(this->m_sPosition);
    }
}

/// quick slot rendering
void
CQuickBAR::DrawQuickSlot() {
    for (int i = 0; i < HOT_ICONS_PER_PAGE; i++) {
        m_QuickSlot[i].Draw();
    }
}

//----------------------------------------------------------------------------------------------------
/// @param PINT ptMouse
/// @brief	¸ÅÇÁ·¹ÀÓ È£Ãâ..
//----------------------------------------------------------------------------------------------------

void
CQuickBAR::Update(POINT ptMouse) {
    if (!IsVision())
        return;

    CTDialog::Update(ptMouse);

    for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i)
        m_QuickSlot[i].Update(ptMouse);
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief À¯Á® ÀÌº¥Æ®( ¸¶¿ì½º, Å°º¸µå.. ) Ã³¸®..
//----------------------------------------------------------------------------------------------------
unsigned int
CQuickBAR::Process(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    switch (GetQuickBarType()) {
        case QUICKBAR_TYPE_NORMAL:
            if (uiMsg == WM_KEYUP) {
                if (GetAsyncKeyState(VK_SHIFT) < 0) {
                    switch (wParam) {
                        case VK_F1:
                            m_nCurrentPage = 0;
                            UpdateHotIconSlot();
                            return uiMsg;
                        case VK_F2:
                            m_nCurrentPage = 1;
                            UpdateHotIconSlot();
                            return uiMsg;
                        case VK_F3:
                            m_nCurrentPage = 2;
                            UpdateHotIconSlot();
                            return uiMsg;
                        case VK_F4:
                            m_nCurrentPage = 3;
                            UpdateHotIconSlot();
                            return uiMsg;
                    }

                    return 0;
                }

                switch (wParam) {
                    case VK_F1:
                    case VK_F2:
                    case VK_F3:
                    case VK_F4:
                    case VK_F5:
                    case VK_F6:
                    case VK_F7:
                    case VK_F8:
                        CIcon* pIcon = m_QuickSlot[wParam - VK_F1].GetIcon();
                        if (pIcon) {
                            pIcon->ExecuteCommand();
                        }
                        return uiMsg;
                }
            }
            break;
        case QUICKBAR_TYPE_EXTENSION:
            if (uiMsg == WM_KEYUP && GetAsyncKeyState(VK_CONTROL) < 0) {
                switch (wParam) {
                    case VK_F1:
                    case VK_F2:
                    case VK_F3:
                    case VK_F4:
                    case VK_F5:
                    case VK_F6:
                    case VK_F7:
                    case VK_F8:
                        CIcon* pIcon = m_QuickSlot[wParam - VK_F1].GetIcon();
                        if (pIcon) {
                            pIcon->ExecuteCommand();
                        }
                        return uiMsg;
                }
            }
            break;
    }

    if (!IsVision())
        return 0;

    // Dlgº¸´Ù SlotºÎÅÍ ¼öÁ¤.
    for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i) {
        if (m_QuickSlot[i].Process(uiMsg, wParam, lParam))
            return uiMsg;
    }

    unsigned iProcID = 0;
    if (iProcID = CTDialog::Process(uiMsg, wParam, lParam)) {
        //		for( int i  = 0; i < HOT_ICONS_PER_PAGE; ++i )
        //		{
        //			if( m_QuickSlot[i].Process( uiMsg, wParam, lParam ) )
        //				return uiMsg;
        //		}
        switch (uiMsg) {
            case WM_LBUTTONUP:
                On_LButtonUP(iProcID, wParam, lParam);
                break;
            default:
                break;
        }
        return uiMsg;
    }

    return 0;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief update cslot of hoticon   OK
//----------------------------------------------------------------------------------------------------
void
CQuickBAR::UpdateHotIconSlot() {
    for (int i = 0; i < HOT_ICONS_PER_PAGE; i++) {
        m_QuickSlot[i].DetachIcon();

        CHotIconSlot* pHotIconSlot = g_pAVATAR->GetHotIconSlot();
        CIcon* pIcon = pHotIconSlot->GetHotIcon(m_nCurrentPage * HOT_ICONS_PER_PAGE + i);

        m_QuickSlot[i].AttachIcon(pIcon);
    }

    UpdateCSlotPosition();

    CWinCtrl* pCtrl = Find(IID_NUMBER);
    if (pCtrl) {
        CTImage* image = (CTImage*)pCtrl;
        int texture_id = 0;
        switch (m_nCurrentPage) {
            case 1:
            case 5:
                texture_id = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI21_NUMBER_2");
                break;
            case 2:
            case 6:
                texture_id = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI21_NUMBER_3");
                break;
            case 3:
            case 7:
                texture_id = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI21_NUMBER_4");
                break;
            default:
                texture_id = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "UI21_NUMBER_1");
                break;
        }

        image->SetImage(texture_id, IMAGE_RES_UI);
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief ÇöÀçÀÇ »óÅÂ¿¡ µû¶ó °¢ Slot °´Ã¼µéÀÇ À§Ä¡¸¦ °»½ÅÇÑ´Ù.
//----------------------------------------------------------------------------------------------------

void
CQuickBAR::UpdateCSlotPosition() {
    POINT pt = {0, 0};

    if (m_nType == TYPE_HORIZONTAL) {
        pt.x = 40;
        pt.y = 21;
        for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i) {
            pt.x = 40 + (m_QuickSlot[0].GetWidth() + 1) * i + (2 * i / HOT_ICONS_PER_PAGE) * 9;
            m_QuickSlot[i].SetOffset(pt);
        }
    } else {
        pt.x = 3;
        pt.y = 40;
        for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i)

        {
            pt.y = 40 + (m_QuickSlot[0].GetHeight() + 1) * i + (2 * i / HOT_ICONS_PER_PAGE) * 10;
            m_QuickSlot[i].SetOffset(pt);
        }
    }

    CWinCtrl* pCtrl = NULL;

    if (m_nType == TYPE_HORIZONTAL) {
        pCtrl = Find("HORIZONTAL");
    } else {
        pCtrl = Find("VERTICAL");
    }

    if (pCtrl == NULL)
        return;

    ///ÇöÀç ÆäÀÌÁö¹øÈ£¸¦ ¹Ù²Ù¾î ÁØ´Ù.
    CWinCtrl* pCtrl1 = pCtrl->Find("1");
    CWinCtrl* pCtrl2 = pCtrl->Find("2");
    CWinCtrl* pCtrl3 = pCtrl->Find("3");
    CWinCtrl* pCtrl4 = pCtrl->Find("4");

    if (pCtrl1 && pCtrl2 && pCtrl3 && pCtrl4) {
        pCtrl1->Hide();
        pCtrl2->Hide();
        pCtrl3->Hide();
        pCtrl4->Hide();

        switch (m_nCurrentPage) {
            case 0:
                pCtrl1->Show();
                break;
            case 1:
                pCtrl2->Show();
                break;
            case 2:
                pCtrl3->Show();
                break;
            case 3:
                pCtrl4->Show();
                break;
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @param OK
/// @brief ÇöÀç ¼±ÅÃµÈ Äü ½½·ÔÀ» ¾ò´Â´Ù. Àý´ëÀ§Ä¡ ½½·Ô¹øÈ£ÀÌ´Ù.
//----------------------------------------------------------------------------------------------------

short
CQuickBAR::GetMouseClickSlot(POINT& ptMouse) {
    /// ÇöÀç ÆäÀÌÁö¿¡ µî·ÏµÈ ½½·ÔµéÀ» µ¹¸ç Ã¼Å©
    for (int i = 0; i < HOT_ICONS_PER_PAGE; i++) {
        if (m_QuickSlot[i].IsInside(ptMouse.x, ptMouse.y) == true) {
            return m_nCurrentPage * HOT_ICONS_PER_PAGE + i;
        }
    }

    return -1;
}
//----------------------------------------------------------------------------------------------------
/// @param
/// @brief process each child control..
//----------------------------------------------------------------------------------------------------

bool
CQuickBAR::On_LButtonUP(unsigned iProcID, WPARAM wParam, LPARAM lParam) {
    switch (iProcID) {
        case IID_BTN_HORIZONTAL_PREV:
        case IID_BTN_VERTICAL_PREV: {
            --m_nCurrentPage;
            if (m_nCurrentPage < m_nStartPage)
                m_nCurrentPage = m_nEndPage - 1;

            UpdateHotIconSlot();
            break;
        }
        case IID_BTN_HORIZONTAL_NEXT:
        case IID_BTN_VERTICAL_NEXT: {
            m_nCurrentPage++;

            if (m_nCurrentPage >= m_nEndPage)
                m_nCurrentPage = m_nStartPage;

            UpdateHotIconSlot();
        }
            return true;
        case IID_BTN_ROTATE: {
            if (m_nType == TYPE_HORIZONTAL)
                ChangeType(TYPE_VERTICAL);
            else
                ChangeType(TYPE_HORIZONTAL);

            UpdateCSlotPosition();

            break;
        }
        default:
            break;
    }

    return false;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Change quick bar type ( vertical <-> horizontal
/// @todo  Number Image Offset, Rotate Button Offset Change
//----------------------------------------------------------------------------------------------------

void
CQuickBAR::ChangeType(int iType, bool bInit) {
    RECT rc = {0, 0, 0, 0};

    if (iType == TYPE_HORIZONTAL) {
        ShowChild(IID_BG_HORIZONTAL);
        ShowChild(IID_BTN_HORIZONTAL_PREV);
        ShowChild(IID_BTN_HORIZONTAL_NEXT);
        HideChild(IID_BG_VERTICAL);
        HideChild(IID_BTN_VERTICAL_PREV);
        HideChild(IID_BTN_VERTICAL_NEXT);

        POINT pt = GetPosition();

        int iWidth = GetWidth();
        int iHeight = GetHeight();
        if (iType != m_nType) {
            if (!bInit) {
                pt.x += GetWidth() / 2 - GetHeight() / 2;
                pt.y += GetHeight() / 2 - GetWidth() / 2;
            }

            SetHeight(iWidth);
            SetWidth(iHeight);
        }

        SetRect(&rc, 0, 0, 18, 63);
        ChangeCaptionRect(rc);

        if (pt.x < 0)
            pt.x = 1;
        if (pt.y < 0)
            pt.y = 1;
        if (pt.x + GetWidth() > g_pCApp->GetWIDTH())
            pt.x = g_pCApp->GetWIDTH() - GetWidth();

        if (pt.y + GetHeight() > g_pCApp->GetHEIGHT())
            pt.y = g_pCApp->GetHEIGHT() - GetHeight();

        /// 타입을 먼저 바꾼후에 MoveWindow 호출 04/5/31 jeddli
        m_nType = iType;

        CWinCtrl* pCtrl = Find(IID_BTN_ROTATE);
        if (pCtrl)
            pCtrl->SetOffset(377, 27);

        pCtrl = Find(IID_NUMBER);
        if (pCtrl)
            pCtrl->SetOffset(19, 24);

        MoveWindow(pt);
    } else {
        HideChild(IID_BG_HORIZONTAL);
        HideChild(IID_BTN_HORIZONTAL_PREV);
        HideChild(IID_BTN_HORIZONTAL_NEXT);
        ShowChild(IID_BG_VERTICAL);
        ShowChild(IID_BTN_VERTICAL_PREV);
        ShowChild(IID_BTN_VERTICAL_NEXT);

        POINT pt = GetPosition();
        int iWidth = GetWidth();
        int iHeight = GetHeight();
        if (iType != m_nType) {
            if (!bInit) {
                pt.x += GetWidth() / 2 - GetHeight() / 2;
                pt.y += GetHeight() / 2 - GetWidth() / 2;
            }
            SetHeight(iWidth);
            SetWidth(iHeight);
        }

        SetRect(&rc, 1, 1, 63, 18);
        ChangeCaptionRect(rc);

        if (pt.x < 0)
            pt.x = 1;
        if (pt.y < 0)
            pt.y = 1;
        if (pt.x + GetWidth() > g_pCApp->GetWIDTH())
            pt.x = g_pCApp->GetWIDTH() - GetWidth();

        if (pt.y + GetHeight() > g_pCApp->GetHEIGHT())
            pt.y = g_pCApp->GetHEIGHT() - GetHeight();

        /// 타입을 먼저 바꾼후에 MoveWindow 호출 04/5/31 jeddli
        m_nType = iType;

        CWinCtrl* pCtrl = Find(IID_BTN_ROTATE);
        if (pCtrl)
            pCtrl->SetOffset(17, 377);

        pCtrl = Find(IID_NUMBER);
        if (pCtrl)
            pCtrl->SetOffset(21, 20);

        MoveWindow(pt);
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief CItemSlotÀÌ º¯°æµÇ¾îÁú¶§¸¶´Ù È£ÃâµÇ´Â Method : ÃßÈÄ CSkillSlot¿¡¼­µµ È£ÃâµÇ¾îÁú¼ö ÀÖ´Ù(
/// SkillÀÌ ¾ø¾îÁö´Â °æ¿ì°¡ »ý±ä´Ù¸é )
//----------------------------------------------------------------------------------------------------
void
CQuickBAR::Update(CObservable* pObservable, CTObject* pObj) {
    ////µî·ÏµÈ ¾ÆÀÌÄÜÁß ¾ÆÀÌÅÛÀÌ°í À§¿Í °°Àº°ÍÀÏ°æ¿ì Áö¿î´Ù.
    assert(pObservable);

    if (pObj && strcmp(pObj->toString(), "CTEventItem") == 0) {
        CTEventItem* pEvent = (CTEventItem*)pObj;

        if (pEvent->GetID() == CTEventItem::EID_DEL_ITEM) ///Áö¿öÁö´Â°Í¸¸ Ã³¸®ÇÏ¸éµÈ´Ù.
        {
            CIcon* pIcon = NULL;
            CIconItem* pItemIcon = NULL;
            CIconQuick* pQuickIcon = NULL;

            int iIndex = pEvent->GetIndex();

            for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i) {
                if (g_pAVATAR->m_HotICONS.m_IconPAGE[m_nCurrentPage][i].m_cType == 1) {
                    if (pIcon = m_QuickSlot[i].GetIcon()) {
                        pQuickIcon = (CIconQuick*)pIcon;
                        pItemIcon = (CIconItem*)pQuickIcon->GetIcon();
                        ///ÀÎº¥Åä¸® ÀÎµ¦½º°¡ °°°í Áö¿öÁö´Â ¾ÆÀÌÅÛÀÏ°æ¿ì
                        ///°°Àº ¾ÆÀÌÅÛÀÌ 2±ºµ¥ µî·ÏµÇ¾î ÀÖÀ»¼ö ÀÖ´Ù.
                        if (pItemIcon->GetIndex() == iIndex) {
                            /// Detach½Ã¿¡ pQuickIconÀº ÆÄ±«µÈ´Ù.( DetachÀü¿¡ Command½ÇÇàÇÒ°Í )
                            m_pCmdDragItemFromQuickBar->Exec(pQuickIcon);
                            m_QuickSlot[i].DetachIcon();
                        }
                    }
                }
            }
        }
    } else {
        assert(0 && "CTEvent is NULL or Invalid Type@CQuickBAR::Update");
    }
}

//*-----------------------------------------------------------------------------------------------------/
/// ¼­¹ö¿¡ ÆÐÅ¶ º¸³»Áö ¾Ê°í Å¬¶óÀÌ¾ðÆ®¿¡¼­¸¸ Data¸¦ »èÁ¦ÇÒ °æ¿ì
/// °ÔÀÓ³»¿¡¼­ Ä³¸¯ÅÍ ¼±ÅÃÃ¢À¸·Î ÀÌµ¿½Ã
//*-----------------------------------------------------------------------------------------------------/
void
CQuickBAR::Clear() {
    for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i)
        m_QuickSlot[i].DetachIcon();
}

void
CQuickBAR::SetInterfacePos_After() {
    for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i) {
        MoveCtrl2ListEnd(m_QuickSlot[i].GetControlID());
    }
}

// ADDED

void
CQuickBAR::SetQuickBarType(short nType) {
    m_nQuickBarType = nType;

    switch (nType) {
        case QUICKBAR_TYPE_NORMAL:
            SetStartEndPage(0, QUICKBAR_MAX_HOT_ICONS_PAGES);
            break;
        case QUICKBAR_TYPE_EXTENSION:
            SetStartEndPage(QUICKBAR_MAX_HOT_ICONS_PAGES, MAX_ICONS_PAGES);
            break;
    }
    m_nCurrentPage = m_nStartPage;
}

short
CQuickBAR::GetQuickBarType() {
    return m_nQuickBarType;
}

void
CQuickBAR::SetStartEndPage(short nStart, short nEnd) {
    m_nStartPage = nStart;
    m_nEndPage = nEnd;
}
