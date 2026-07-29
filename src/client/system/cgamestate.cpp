#include "stdafx.h"

#include "cgamestate.h"
#include "capplication.h"
#include "interface/dev/dev_ui.h"
#include "rmlui/RoseRmlUi.h"

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

    /// RmlUi gets first refusal, and consumes only what it actually handled --
    /// otherwise the world stops receiving hover and camera input. Derived
    /// states call this base first, so this covers the whole legacy chain.
    if (RoseRmlUi::ProcessWndMsg(g_pCApp->GetHWND(), uiMsg, wParam, lParam)) {
        return 1;
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

    /// Shared overlay hook: every game state calls this inside its
    /// beginScene()/endScene() pair, after the game UI has been drawn, which is
    /// exactly where the RmlUi pass belongs. It must be OUTSIDE the engine's
    /// beginSprite()/endSprite() block -- the renderer's state guard assumes it
    /// owns the device for the duration of the pass.
    RoseRmlUi::Update();
    RoseRmlUi::Render();
}