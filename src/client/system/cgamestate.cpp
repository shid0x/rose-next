#include "stdafx.h"

#include "cgamestate.h"
#include "capplication.h"
#include "interface/dev/dev_ui.h"

CGameState::CGameState(void):
    m_iStateID(0),
    dev_ui_enabled(false) {
}

CGameState::~CGameState(void) {}

void
CGameState::ServerDisconnected() {
    g_pCApp->SetExitGame();
}

int
CGameState::ProcWndMsgInstant(unsigned uiMsg, WPARAM wParam, LPARAM lParam) {
    switch (uiMsg) {
        case WM_SYSKEYDOWN:
            if (wParam == VK_OEM_3) { // '~' key
                dev_ui_enabled = !dev_ui_enabled;
            }
    }

    if (this->dev_ui_enabled) {
        return dev_ui_proc(g_pCApp->GetHWND(), uiMsg, wParam, lParam);
    }

    return 0;
}

int
CGameState::Update(bool bLostFocus) {
    _ASSERT(0 && "CGameState::Update");
    *(int*)0 = 10;
    return 0;
}

int
CGameState::Enter(int iPrevStateID) {
    _ASSERT(0 && "CGameState::Enter");
    *(int*)0 = 10;
    return 0;
}

int
CGameState::Leave(int iNextStateID) {
    _ASSERT(0 && "CGameState::Leave");
    *(int*)0 = 10;
    return 0;
}

int
CGameState::ProcMouseInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    _ASSERT(0 && "CGameState::ProcMouseInput");
    *(int*)0 = 10;
    return 0;
}

int
CGameState::ProcKeyboardInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    _ASSERT(0 && "CGameState::ProcKeyboardInput");
    *(int*)0 = 10;
    return 0;
}

void
CGameState::render_dev_ui(void) {
    if (this->dev_ui_enabled) {
        dev_ui_frame();
        dev_ui_render();
    }
}