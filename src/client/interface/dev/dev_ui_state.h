#pragma once

#ifdef _DEBUG

#include "dev_game_window.h"
#include "dev_main_window.h"
#include "dev_target_window.h"

struct DevWindowState {
    TargetWindowState target_window_state;
    GameWindowState game_window_state;
};

#endif