#include "stdafx.h"


#ifdef _DEBUG

#include "dev_ui.h"
#include "dev_ui_state.h"

// #include "ccamera.h"
#include "interface/ctdrawimpl.h"
#include "rose/io/stb.h"
// #include "system/cgame.h"
// #include "system/cgamestate.h"

#include "fmt/format.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

#include "zz_hash_table.h"
#include "zz_node.h"
/// #include "zz_texture.h"
#include "zz_system.h"
#include "zz_visible.h"


// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam);

static DevWindowState g_state;


void
dev_ui_init(HWND handle) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    ImGui_ImplWin32_Init(handle);
    ImGui_ImplDX9_Init(reinterpret_cast<IDirect3DDevice9*>(::getDevice()));

    // Setup data
    load_stat_data(g_state.game_window_state);
}

void
dev_ui_frame() {
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(550, 680), ImGuiCond_FirstUseEver);

    
    draw_main_window(g_state);
    draw_target_window(g_state.target_window_state);

    ImGui::EndFrame();

    /*
    Can I move this into frame? If not should move these values to the main window state
    ::useWireMode(wireframe);
    ::UserObserverCamera(observer_camera);
    ::useFog(use_fog);
    ::setFogColor(fog_color[0], fog_color[1], fog_color[2]);
    ::setFogRange(fog[0] * ZZ_SCALE_OUT, fog[1] * ZZ_SCALE_OUT);
    ::setAlphaFogRange(alpha_fog[0] * ZZ_SCALE_OUT, alpha_fog[1] * ZZ_SCALE_OUT);
    */
}

void
dev_ui_render() {
    IDirect3DDevice9* device = reinterpret_cast<IDirect3DDevice9*>(::getDevice());

    DWORD prev_fill_mode = D3DFILL_SOLID;
    device->GetRenderState(D3DRS_FILLMODE, &prev_fill_mode);

    // Never render dev UI as wireframe
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    device->SetRenderState(D3DRS_FILLMODE, prev_fill_mode);
}

void
dev_ui_destroy() {
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool
dev_ui_proc(HWND handle, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!ImGui::GetCurrentContext()) {
        return false;
    }

    if (ImGui_ImplWin32_WndProcHandler(handle, msg, wParam, lParam)) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

#else
void
dev_ui_init(HWND handle) {}

void
dev_ui_frame() {}

void
dev_ui_render() {}

void
dev_ui_destroy() {}

bool
dev_ui_proc(HWND handle, UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    return false;
}
#endif