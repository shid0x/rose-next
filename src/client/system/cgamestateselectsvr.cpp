#include "stdafx.h"

#include ".\cgamestateselectsvr.h"
#include "../Interface/ExternalUI/ExternalUILobby.h"
#include "../CCamera.h"
#include "../interface/CUIMediator.h"

#include "../Game.h"
#include "../System/CGame.h"
#include "tgamectrl/tmsgbox.h"
#include "../Network/CNetwork.h"

#include "../Interface/ExternalUI/ExternalUILobby.h"
#include "../Interface/CTFontImpl.h"
#include "../Interface/IO_ImageRes.h"

#include "../Interface/Command/CTCmdHotExec.h"
#include "../Interface/Cursor/CCursor.h"

#include "io_quest.h"

#include "../CSkyDOME.H"
#include "tgamectrl/time2.h"
#include "SystemProcScript.h"

#include "cclientstorage.h"

CGameStateSelectSvr::CGameStateSelectSvr(int iID) {
    m_iStateID = iID;
    m_iPrevState = 0;
}

CGameStateSelectSvr::~CGameStateSelectSvr(void) {}
int
CGameStateSelectSvr::Update(bool bLostFocus) {
    g_EUILobby.Update();

    g_pCamera->Update();

    D3DVECTOR PosENZIN = g_pCamera->Get_Position();
    g_pTerrain->SetCenterPosition(PosENZIN.x, PosENZIN.y);

    g_DayNNightProc.ShowAllEffect(false);

    ::updateScene();

    // processing  ...
    if (g_ClientStorage.m_VideoOption.background_render || !bLostFocus) {
        if (::beginScene()) //  디바이스가 손실된 상태라면 0을 리턴하므로, 모든 렌더링 스킵
        {
            ::clearScreen();
            ::renderScene();

            ::beginSprite(D3DXSPRITE_ALPHABLEND);

            /// Screen message display
            g_UIMed.Draw();

            /// UI display
            g_EUILobby.Draw();

            ::endSprite();

            this->render_dev_ui();
            ::endScene();
            ::swapBuffers();
        }
    }
    return 0;
}

int
CGameStateSelectSvr::Enter(int iPrevStateID) {
    m_iPrevState = iPrevStateID;
    return 1;
}

int
CGameStateSelectSvr::Leave(int iNextStateID) {

    return 1;
}

void
CGameStateSelectSvr::ServerDisconnected() {
    g_EUILobby.ServerDisconnected();
}

int
CGameStateSelectSvr::ProcMouseInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    if (g_EUILobby.MsgProc(uiMsg, wParam, lParam))
        return 1;

    return 0;
}

int
CGameStateSelectSvr::ProcKeyboardInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    if (g_EUILobby.MsgProc(uiMsg, wParam, lParam))
        return 1;

    return 0;
}