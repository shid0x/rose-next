#include "stdafx.h"

#include ".\cmakestatenormal.h"
#include "../../Icon/CIconItem.h"
#include "../MakeDLG.h"
#include "../../CToolTipMgr.h"

#include "../../Command/UICommand.h"

#include "../../../Common/IO_Skill.h"
#include "../../../System/CGame.h"
#include "../../../Network/CNetwork.h"

#include "../../../System/CGame.h"

#include "tgamectrl/jcombobox.h"
CMakeStateNormal::CMakeStateNormal(CMakeDLG* pParent) {
    m_pParent = pParent;
    // m_iSelectedItemIdx = -1;
}

void
CMakeStateNormal::Draw() {}

void
CMakeStateNormal::Update(POINT ptMouse) {
    /*int iRet = CManufacture::GetInstance().IsValidSendMakeItemReq();
    std::string	strToolTipMsg;
    if( iRet )
    {
        m_pParent->SetEnableChild( CMakeDLG::IID_BTN_START, false );
        switch( iRet )
        {
        case 1:
            strToolTipMsg = "NULL";
            break;
        case 2:
            strToolTipMsg = STR_NOT_ENOUGH_INVENTORY_SPACE;
            break;
        case 3:
            strToolTipMsg = STR_NOT_ENOUGH_MANA;
            break;
        case 4:
            strToolTipMsg = STR_NOT_EXIST_MATERIAL;
            break;
        case 5:
            strToolTipMsg = STR_NOT_ENOUGH_MATERIAL;
            break;
        default:
            break;
        }
    }
    else
    {
        m_pParent->SetEnableChild( CMakeDLG::IID_BTN_START, true );
        strToolTipMsg = STR_START_MAKE_ITEM;
    }

    CWinCtrl* pCtrl = m_pParent->Find( CMakeDLG::IID_BTN_START);

    if( CWinCtrl::IsProcessMouseOver() )
        if( CWinCtrl::GetProcessMouseOverCtrl() == pCtrl )
            CToolTipMgr::GetInstance().RegToolTip( (short)ptMouse.x, (short)ptMouse.y ,
    strToolTipMsg.c_str() );
*/
}

void
CMakeStateNormal::Show() {}

void
CMakeStateNormal::Hide() {}

unsigned int
CMakeStateNormal::Process(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    unsigned iProcID;
    if (iProcID = m_pParent->CTDialog::Process(uiMsg, wParam, lParam)) {
        for (int i = 0; i < g_iMaxCountMaterial; ++i)
            if (m_pParent->m_listMaterialSlot[i].Process(uiMsg, wParam, lParam))
                return uiMsg;

        switch (uiMsg) {
            case WM_LBUTTONUP:
                OnLButtonUp(iProcID, wParam, lParam);
                break;
            case WM_LBUTTONDOWN:
                OnLButtonDown(iProcID, wParam, lParam);
                break;
            default:
                break;
        }
        return uiMsg;
    }
    return 0;
}

void
CMakeStateNormal::OnLButtonDown(unsigned iProcID, WPARAM wParam, LPARAM lParam) {
    switch (iProcID) {
        case CMakeDLG::IID_COMBOBOX_ITEM: {
            /// Item을 바꾸어 주자
            CWinCtrl* pCtrl = m_pParent->Find(CMakeDLG::IID_COMBOBOX_ITEM);
            if (pCtrl) {
                CJComboBox* pComboBox = (CJComboBox*)pCtrl;
                const CTObject* pObj = pComboBox->GetSelectedItem();
                if (pObj) {
                    CMakeComboItem* pComboItem = (CMakeComboItem*)pObj;
                    CManufacture::GetInstance().SetMakeItem(pComboItem->GetItem());
                }
            }
            break;
        }
        case CMakeDLG::IID_COMBOBOX_CLASS: {
            CWinCtrl* pCtrl = m_pParent->Find(CMakeDLG::IID_COMBOBOX_CLASS);
            if (pCtrl) {
                CJComboBox* pComboBox = (CJComboBox*)pCtrl;
                const CTObject* pObj = pComboBox->GetSelectedItem();
                if (pObj) {
                    CMakeComboClass* pComboClass = (CMakeComboClass*)pObj;
                    CManufacture::GetInstance().SetMakeClass(pComboClass->GetClass());
                }
            }
            break;
        }
        default:
            break;
    }
}

void
CMakeStateNormal::OnLButtonUp(unsigned iProcID, WPARAM wParam, LPARAM lParam) {
    switch (iProcID) {
        case CMakeDLG::IID_BTN_CLOSE:
            m_pParent->Hide();
            return;
        case CMakeDLG::IID_BTN_START:
            m_pParent->Start();
            return;
        default:
            break;
    }
}

void
CMakeStateNormal::Init() {
    m_pParent->SetEnableChild(CMakeDLG::IID_BTN_CLOSE, true);
}

bool
CMakeStateNormal::IsModal() {
    return m_pParent->CTDialog::IsModal();
}