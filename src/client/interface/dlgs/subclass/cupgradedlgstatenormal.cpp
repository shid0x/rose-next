#include "stdafx.h"

#include ".\cupgradedlgstatenormal.h"
#include "../CUpgradeDlg.h"
#include "../../../GameData/CUpgrade.h"
#include "../../../object.h"

CUpgradeDlgStateNormal::CUpgradeDlgStateNormal(CUpgradeDlg* pParent) {
    m_pParent = pParent;
}

CUpgradeDlgStateNormal::~CUpgradeDlgStateNormal(void) {}
void
CUpgradeDlgStateNormal::Enter() {}

void
CUpgradeDlgStateNormal::Leave() {}

void
CUpgradeDlgStateNormal::Update(POINT ptMouse) {}

void
CUpgradeDlgStateNormal::Draw() {}

unsigned
CUpgradeDlgStateNormal::Process(unsigned uiMsg, WPARAM wParam, LPARAM lParam) {
    if (m_pParent->m_TargetItemSlot.Process(uiMsg, wParam, lParam))
        return uiMsg;

    for (int i = 0; i < 3; ++i) {
        if (m_pParent->m_MaterialSlots[i].Process(uiMsg, wParam, lParam))
            return uiMsg;
    }

    if (unsigned uiProcID = m_pParent->CTDialog::Process(uiMsg, wParam, lParam)) {
        if (uiMsg == WM_LBUTTONUP) {
            switch (uiProcID) {
                case IID_BTN_START:
                    m_pParent->Start();
                    break;
                case IID_BTN_CLOSE:
                    m_pParent->Hide();
                    break;
                default:
                    break;
            }
        }
        return uiMsg;
    }
    return 0;
}
