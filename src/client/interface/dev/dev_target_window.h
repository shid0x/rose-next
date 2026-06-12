#pragma once

#ifdef _DEBUG

struct TargetWindowState {
    bool is_open;
};

void draw_target_window(TargetWindowState& state);

#endif