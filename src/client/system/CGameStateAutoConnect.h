#pragma once

#include "CGameState.h"

enum class AutoConnectError {
    None,
    ServerError,
    LoginError,
    CharSelectError,
};

/// Game state that handles automatic login, server/channel and avatar select
class CGameStateAutoConnect: public CGameState {
public:
    CGameStateAutoConnect(int state);

    // -- CGameState
    int Update(bool bLostFocus) override;
    int Enter(int previousState) override;
    int Leave(int nextState) override;
    int ProcMouseInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) override;
    int ProcKeyboardInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) override;
    // --

    // Handlers
    void on_loginserver_connected() override;
    void on_login_succeeded() override;
    void on_login_failed(int code) override;
    void on_charserver_connected() override;
    void on_charserver_connect_failed() override;

private:
    std::vector<std::string> messages;
    AutoConnectError last_error;
};