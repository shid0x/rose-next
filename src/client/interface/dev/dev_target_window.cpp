#include "stdafx.h"

#ifdef _DEBUG

#include "dev_target_window.h"

#include "jcommandstate.h"

#include "imgui.h"

void
draw_target_window(TargetWindowState& state) {
    if (!state.is_open) {
        return;
    }

    ImGui::Begin("Developer - Target", &state.is_open, ImGuiWindowFlags_AlwaysAutoResize);

    int target_id = g_UserInputSystem.GetCurrentTarget();
    if (target_id < 0 || target_id > MAX_CLIENT_OBJECTS) {
        ImGui::Text("Target ID: Invalid");
        ImGui::End();
        return;
    }

    ImGui::Text(fmt::format("ID: {}", target_id).c_str());
    if (target_id == 0) {
        ImGui::End();
        return;
    }

    CGameOBJ* obj = g_pObjMGR->m_pOBJECTS[target_id];
    if (!obj) {
        ImGui::Text("Object with this id does not exist.");
        ImGui::End();
        return;
    }

    int obj_type_id = obj->Get_TYPE();
    std::string obj_type_str = obj_type_string(static_cast<t_ObjTAG>(obj_type_id));
    ImGui::SameLine();
    ImGui::Text(fmt::format("Type: {}({})", obj_type_str, obj_type_id).c_str());
    ImGui::Separator();

    switch (obj->Get_TYPE()) {
        case OBJ_USER:
        case OBJ_AVATAR:
        case OBJ_NPC:
        case OBJ_MOB: {
            CObjCHAR* character = reinterpret_cast<CObjCHAR*>(obj);
            int str = 0;
            int dex = 0;
            int intt = 0;
            int con = 0;
            int cha = character->Get_CHARM();
            int sen = character->Get_SENSE();

            if (character->IsUSER()) {
                CObjAVT* avatar = reinterpret_cast<CObjAVT*>(obj);
                intt = avatar->Get_INT();
                con = avatar->Get_CON();
            }

            if (character->Get_TYPE() == OBJ_USER) {
                CObjUSER* user = reinterpret_cast<CObjUSER*>(obj);
                str = user->Get_STR();
                dex = user->Get_DEX();
            }

            if (character) {
                ImGui::Text("General");
                ImGui::Separator();
                ImGui::Text("Name: %s", character->Get_NAME());
                ImGui::Text("Level: %d", character->Get_LEVEL());
                ImGui::Text("HP: %d/%d", character->Get_HP(), character->Get_MaxHP());
                ImGui::SameLine();
                ImGui::Text("MP: %d/%d", character->Get_MP(), character->Get_MaxMP());
                ImGui::Text("Position: (%.3f, %.3f, %.3f)",
                    character->Get_CurXPOS(),
                    character->Get_CurYPOS(),
                    character->Get_CurZPOS());
                ImGui::Separator();
                ImGui::Text("PvP state: %d", character->pvp_state);
                ImGui::Text("PvP enabled: %d", character->is_pvp_enabled());
                ImGui::Text("Team number: %d", character->Get_TeamNO());
                ImGui::Separator();

                ImGui::Text("Stats");
                ImGui::Separator();
                ImGui::Columns(2);
                ImGui::Text("Str: %d", str);
                ImGui::Text("Dex: %d", dex);
                ImGui::Text("Int: %d", intt);
                ImGui::Text("Con: %d", con);
                ImGui::Text("Cha: %d", cha);
                ImGui::Text("Sen: %d", sen);
                ImGui::NextColumn();
                ImGui::Text("Attack: %d", character->stats.attack_power);
                ImGui::Text("Def: %d", character->Get_DEF());
                ImGui::Text("M-Res: %d", character->Get_RES());
                ImGui::Text("Hit: %d", character->stats.hit_rate);
                ImGui::Text("Crit: %d", character->Get_CRITICAL());
                ImGui::Text("Dodge: %d", character->Get_AVOID());
                ImGui::Text("A-spd: %d", character->stats.attack_speed);
                ImGui::Text("M-spd: %d", character->stats.move_speed);
                ImGui::Columns(1);
                ImGui::Separator();

                ImGui::Text("Animation");
                ImGui::Separator();
                tagMOTION* anim = character->m_pCurMOTION;
                ImGui::Text("Motion id: %d", anim->m_nActionIdx);
                ImGui::Text("Model speed: %.3f", character->Get_ModelSPEED());
                ImGui::Text("Move speed: %.3f", character->Get_MoveAniSPEED());
                ImGui::Text("Attack speed: %.3f", character->Get_fAttackSPEED());
                ImGui::Separator();

                ImGui::Text("Zone");
                ImGui::Separator();
                ImGui::Text("PvP state: %d", g_pTerrain->pvp_state);
                ImGui::Text("PvP enabled: %d", g_pTerrain->is_pvp_zone());
                ImGui::Separator();

                ImGui::Text("Move mode: %d", character->m_btMoveMODE);
            }
        }
    }

    switch (obj->Get_TYPE()) {
        case OBJ_USER: {
            ImGui::Text("THAS YOU");
            break;
        }
        case OBJ_AVATAR: {
            ImGui::Text("THAS AN AVATAR");
            break;
        }
        case OBJ_NPC: {
            ImGui::Text("THAS AN NPC");
            break;
        }
        case OBJ_MOB: {
            ImGui::Text("THAS A MOB");
            break;
        }
        default: {
            ImGui::Text("Currently no information available for this type.");
        }
    }

    ImGui::End();
}

#endif