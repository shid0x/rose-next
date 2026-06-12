#include "stdafx.h"

#ifdef _DEBUG

#include "dev_ui_state.h"

#include "ccamera.h"
#include "system/cgame.h"
#include "system/cgamestate.h"

#include "zz_hash_table.h"
#include "zz_node.h"
#include "zz_system.h"
#include "zz_visible.h"

#include "imgui.h"

void
draw_camera_info() {
    CCamera* cam = CCamera::Instance();
    if (!cam) {
        return;
    }

    float distance = cam->distance();
    float pitch = cam->pitch();
    float yaw = cam->yaw();

    ImGui::Checkbox("Follow mode", &cam->m_bFollowMode);
    ImGui::SliderFloat("Distance", &distance, 0.0f, Rose::GameStaticConfig::CAMERA_MAX_ZOOM * 10);
    ImGui::SliderFloat("Pitch", &pitch, 0.0f, 1.0f);
    ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f);

    cam->set_follow_mode(cam->m_bFollowMode);
    cam->set_distance(distance);
    cam->set_pitch(pitch);
    cam->set_yaw(yaw);
}


void
draw_main_window(DevWindowState& state) {
    ImGui::Begin("Developer",
        &CGame::GetInstance().active_state->dev_ui_enabled,
        ImGuiWindowFlags_AlwaysAutoResize);

    zz_system* zz = reinterpret_cast<zz_system*>(::getZnzin());

    bool wireframe = ::getUseWireMode();
    bool observer_camera = ::GetObserverCameraOnOff();

    // Fog
    bool use_fog = ::getUseFog();
    float fog_color[3] = {zz->get_rs()->fog_color[0],
        zz->get_rs()->fog_color[1],
        zz->get_rs()->fog_color[2]};
    float fog[2] = {zz->get_rs()->fog_start, zz->get_rs()->fog_end};
    float alpha_fog[2] = {zz->get_rs()->alpha_fog_start, zz->get_rs()->alpha_fog_end};

    // Buttons
    {
        if (ImGui::Button("Target")) {
            state.target_window_state.is_open = true;
        }
    }

    // Main Tab bar
    { 
        ImGui::BeginTabBar("main_tabbar");

        // Game Tab
        if (ImGui::BeginTabItem("Game")) {
            if (ImGui::BeginChild("main_game_tab_child", ImVec2(800, 600))) {
                draw_game_window(state.game_window_state);
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        // Misc Tab
        // TODO: Organize this better?
        if (ImGui::BeginTabItem("Misc")) {
            if (ImGui::CollapsingHeader("General")) {
                ImGui::Checkbox("Disable UI", &g_GameDATA.m_bNoUI);
                ImGui::SameLine();
                ImGui::Checkbox("Filming mode", &g_GameDATA.m_bFilmingMode);
                ImGui::SameLine();
                ImGui::Checkbox("Observer mode", &g_GameDATA.m_bObserverCameraMode);

                if (ImGui::Button("Reload UI")) {
                    g_itMGR.reload_dialogs();
                }
            }

            if (ImGui::CollapsingHeader("Camera")) {
                draw_camera_info();
            }

            if (ImGui::CollapsingHeader("Engine settings")) {
                ImGui::Checkbox("Wireframe", &wireframe);
                ImGui::Checkbox("Use Fog", &use_fog);
                ImGui::ColorEdit3("Fog color", fog_color);
                ImGui::SliderFloat2("Fog range", fog, 0.0f, 1000.0f);
                ImGui::SliderFloat2("Alpha fog range", alpha_fog, 0.0f, 1000.0f);
            }

            if (ImGui::CollapsingHeader("Scene Tree")) {
                if (ImGui::TreeNode("Visibles")) {
                    zz_visible* visible;

                    zz_hash_table<zz_node*>::iterator it;
                    zz_hash_table<zz_node*>* nodes = zz->visibles->get_hash_table();

                    for (it = nodes->begin(); it != nodes->end(); it++) {
                        visible = (zz_visible*)(*it);
                        if (visible->is_visible()) {
                            ImGui::Text(visible->get_name());
                        }
                    }
                    ImGui::TreePop();
                }
            }

            if (ImGui::CollapsingHeader("Class Information")) {
                ImGui::Text("111 Soldier");
                ImGui::Text("121 Knight");
                ImGui::Text("122 Champion");
                ImGui::Text("");
                ImGui::Text("211 Muse");
                ImGui::Text("221 Magician");
                ImGui::Text("222 Cleric");
                ImGui::Text("");
                ImGui::Text("311 Hawker");
                ImGui::Text("311 Scout");
                ImGui::Text("321 Raider");
                ImGui::Text("");
                ImGui::Text("411 Dealer");
                ImGui::Text("421 Artisan");
                ImGui::Text("422 Bourgeois");
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
#endif