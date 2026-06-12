#pragma once

void dev_ui_init(HWND handle);
void dev_ui_frame();
void dev_ui_render();
void dev_ui_destroy();
bool dev_ui_proc(HWND handle, UINT uiMsg, WPARAM wParam, LPARAM lParam);