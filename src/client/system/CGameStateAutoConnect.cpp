#include "stdafx.h"

#include <sstream>

#include "cgame.h"
#include "CGameStateAutoConnect.h"
#include "interface/externalui/externaluilobby.h"

#include "Network/cnetwork.h"

std::array<const char*, 12> login_error_message = {
    "", // No error
    "General failure",
    "Account not found",
    "Invalid password",
    "Already logged in",
    "Refused account",
    "Need charge",
    "Not enough permission",
    "Too many users",
    "No real name",
    "Invalid version",
    "Out of IP",
};

CGameStateAutoConnect::CGameStateAutoConnect(int state): last_error(AutoConnectError::None) {
    this->m_iStateID = state;
}

int
CGameStateAutoConnect::Update(bool bLostFocus) {
    updateScene();
    if (!beginScene()) {
        return 0;
    }

    clearScreen();

    int messageY = 15;
    for (const std::string& message: this->messages) {
        drawFont(g_GameDATA.m_hFONT[FONT_LARGE_BOLD],
            false,
            15,
            messageY,
            zz_color_black,
            message.c_str());
        messageY += 15;
    }

    if (this->last_error != AutoConnectError::None) {
        drawFont(g_GameDATA.m_hFONT[FONT_LARGE_BOLD],
            false,
            15,
            messageY,
            zz_color_black,
            "Press any key to continue...");
    }

    endScene();
    swapBuffers();

    return 0;
}

int
CGameStateAutoConnect::Enter(int previousState) {
    g_EUILobby.InitEUIManager();

    this->messages.clear();
    this->messages.push_back("Started auto connect.");

    if (!g_pNet->ConnectToServer(g_GameDATA.server_ip, g_GameDATA.server_port, NS_CON_TO_LSV)) {
        this->last_error = AutoConnectError::ServerError;
    }

    return 0;
}

int
CGameStateAutoConnect::Leave(int nextState) {
    return 0;
}

int
CGameStateAutoConnect::ProcMouseInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    return 0;
}

int
CGameStateAutoConnect::ProcKeyboardInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    switch (this->last_error) {
        case AutoConnectError::ServerError:
            g_pCApp->SetExitGame();
            break;
        default:
            CGame::GetInstance().ChangeState(CGame::GS_LOGIN);
            break;
    };

    return 0;
}

void
CGameStateAutoConnect::on_loginserver_connected() {
    this->messages.push_back("Connected to login server.");
    g_pNet->send_login_req(g_GameDATA.username, g_GameDATA.password);
}

void
CGameStateAutoConnect::on_login_succeeded() {
    int server_id = 1;
    int channel_id = 1;

    if (g_GameDATA.auto_connect_server_id > 1) {
        server_id = g_GameDATA.auto_connect_server_id;
    }

    if (g_GameDATA.auto_connect_channel_id > 1) {
        channel_id = g_GameDATA.auto_connect_channel_id;
    }

    std::stringstream message("");
    message << "Login succeeded. Connecting: Server #" << server_id << "; Channel #" << channel_id
            << ".";
    this->messages.push_back(message.str());

    g_pNet->Send_cli_SELECT_SERVER(server_id, channel_id);
    return;
}

void
CGameStateAutoConnect::on_login_failed(int code) {
    if (code == RESULT_LOGIN_REPLY_ALREADY_LOGGEDIN) {
        this->messages.push_back("Account already logged in. Attempting to reconnect.");

        if (!g_pNet->ConnectToServer(g_GameDATA.server_ip, g_GameDATA.server_port, NS_CON_TO_LSV)) {
            this->last_error = AutoConnectError::ServerError;
            return;
        }

        g_pNet->send_login_req(g_GameDATA.username, g_GameDATA.password);
        return;
    }

    std::string message = "Failed to login: ";
    if (code > 0 && code < login_error_message.size()) {
        message += std::string(login_error_message.at(code)) + ".";
    } else {
        message += "Unkown error #" + std::to_string(code) + ".";
    }

    this->messages.push_back(message);
    this->last_error = AutoConnectError::LoginError;
    return;
}

void
CGameStateAutoConnect::on_charserver_connected() {
    if (g_GameDATA.auto_connect_character_name.empty()) {
        this->messages.push_back("No character name provided");
        this->last_error = AutoConnectError::CharSelectError;
        return;
    }

    std::stringstream message("");
    message << "Server/Channel select succeeded. Selecting character: "
            << g_GameDATA.auto_connect_character_name << ".";
    this->messages.push_back(message.str());

    CGame::GetInstance().Load_BasicDATA2();
    g_pNet->Send_cli_SELECT_CHAR(0, (char*)g_GameDATA.auto_connect_character_name.c_str());
    return;
}

void
CGameStateAutoConnect::on_charserver_connect_failed() {
    this->messages.push_back("Failed to connect to charserver.");
    this->last_error = AutoConnectError::CharSelectError;
    return;
}