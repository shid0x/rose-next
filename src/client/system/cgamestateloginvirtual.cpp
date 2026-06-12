#include "stdafx.h"

#include ".\cgamestateloginvirtual.h"
#include "../Network/CNetwork.h"
#include "../Interface/Command/CTCmdHotExec.h"
#include "../Game.h"
#include "../Interface/ExternalUI/ExternalUILobby.h"
#include "../CCamera.h"
#include "../interface/CUIMediator.h"
#include "SystemProcScript.h"
#include "CGame.h"

#include "cclientstorage.h"

CGameStateLoginVirtual::CGameStateLoginVirtual(int iID) {
    m_iStateID = iID;
}

CGameStateLoginVirtual::~CGameStateLoginVirtual(void) {}

int
CGameStateLoginVirtual::Update(bool bLostFocus) {
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
CGameStateLoginVirtual::Enter(int iPrevStateID) {
    g_pNet->DisconnectFromServer();
    return 1;
}

void
CGameStateLoginVirtual::ServerDisconnected() {}
void
CGameStateLoginVirtual::WorldServerDisconnected() {
    if (!g_pNet->ConnectToServer(g_GameDATA.server_ip, g_GameDATA.server_port, NS_CON_TO_LSV)) {
        CTCmdExit Command;
        Command.Exec(NULL);
    }
}
int
CGameStateLoginVirtual::Leave(int iNextStateID) {
    CSystemProcScript::GetSingleton().CallLuaFunction("LeaveLoginVirtual", ZZ_PARAM_END);
    return 1;
}

void
CGameStateLoginVirtual::AcceptedConnectLoginSvr() {
    g_pNet->send_login_req(g_GameDATA.username, g_GameDATA.password);
}

int
CGameStateLoginVirtual::ProcMouseInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    return 1;
}
int
CGameStateLoginVirtual::ProcKeyboardInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    return 1;
}
