#include "stdafx.h"

#ifdef _DEBUG

#include "dev_game_window.h"

#include "interface/io_imageres.h"
#include "network/cnetwork.h"

#include "imgui.h"

#include "zz_texture.h"

static const std::array<std::tuple<unsigned int, STBDATA*>, 14> ITEM_DATA = {
    std::make_tuple(ITEM_TYPE_FACE_ITEM, &g_TblFACEITEM),
    std::make_tuple(ITEM_TYPE_HELMET, &g_TblHELMET),
    std::make_tuple(ITEM_TYPE_ARMOR, &g_TblARMOR),
    std::make_tuple(ITEM_TYPE_GAUNTLET, &g_TblGAUNTLET),
    std::make_tuple(ITEM_TYPE_BOOTS, &g_TblBOOTS),
    std::make_tuple(ITEM_TYPE_KNAPSACK, &g_TblBACKITEM),
    std::make_tuple(ITEM_TYPE_JEWEL, &g_TblJEWELITEM),
    std::make_tuple(ITEM_TYPE_WEAPON, &g_TblWEAPON),
    std::make_tuple(ITEM_TYPE_SUBWPN, &g_TblSUBWPN),
    std::make_tuple(ITEM_TYPE_USE, &g_TblUSEITEM),
    std::make_tuple(ITEM_TYPE_GEM, &g_TblGEMITEM),
    std::make_tuple(ITEM_TYPE_NATURAL, &g_TblNATUAL),
    std::make_tuple(ITEM_TYPE_QUEST, &g_TblQUESTITEM),
    std::make_tuple(ITEM_TYPE_RIDE_PART, &g_PatITEM),
};

constexpr const char*
item_type_label(unsigned int type) {
    switch (type) {
        case ITEM_TYPE_FACE_ITEM:
            return "Mask";
        case ITEM_TYPE_HELMET:
            return "Hat";
        case ITEM_TYPE_ARMOR:
            return "Chest";
        case ITEM_TYPE_GAUNTLET:
            return "Gloves";
        case ITEM_TYPE_BOOTS:
            return "Shoes";
        case ITEM_TYPE_KNAPSACK:
            return "Back";
        case ITEM_TYPE_JEWEL:
            return "Accessory";
        case ITEM_TYPE_WEAPON:
            return "Weapon";
        case ITEM_TYPE_SUBWPN:
            return "Subweapon";
        case ITEM_TYPE_USE:
            return "Consumable";
        case ITEM_TYPE_GEM:
            return "Gem";
        case ITEM_TYPE_NATURAL:
            return "ETC";
        case ITEM_TYPE_QUEST:
            return "Quest";
        case ITEM_TYPE_RIDE_PART:
            return "Cart";
        default:
            return "";
    }
}

void
load_stat_data(GameWindowState& state) {
    for (size_t row_idx = 0; row_idx < g_TblGEMITEM.row_count; ++row_idx) {
        std::string label;
        // Append gem name
        if (row_idx > 300) {
            label += ITEM_NAME(ITEM_TYPE_GEM, row_idx);
        } else {
            // Append stat info
            for (int j = 0; j < 2; j++) {
                short nAttribute = GEMITEM_ADD_DATA_TYPE(row_idx, j);
                if (nAttribute != 0) {
                    if (j == 1) {
                        label += " / ";
                    }
                    std::string attr = CStringManager::GetSingleton().GetAbility(nAttribute);
                    int amount = GEMITEM_ADD_DATA_VALUE(row_idx, j);
                    if (nAttribute == AT_SAVE_MP) {
                        label += fmt::format("{} {}%", attr, amount);
                    } else {
                        label += fmt::format("{} {}", attr, amount);
                    }
                }
            }
        }

        if (!label.empty()) {
            state.stat_names.push_back({row_idx, label});
        }
    }
}

void
draw_item_tab(GameWindowState& state) {
    CImageRes* item_image_manager = CImageResManager::GetSingleton().GetImageRes(IMAGE_RES_ITEM);
    if (!item_image_manager) {
        return;
    }

    static char filter_item_name[128] = "";

    static int spawn_item_quantity = 1;
    static int spawn_item_refine = 0;
    static int spawn_item_durability = 120;
    static int spawn_item_lifespan = 100;
    static int spawn_item_stat_id = 0;
    static bool spawn_item_socket = true;

    {
        if (!ImGui::BeginChild("item_window_left_pane", ImVec2(150, 0), true)) {
            return ImGui::EndChild();
        }

        for (auto [item_type, _]: ITEM_DATA) {
            if (ImGui::Selectable(item_type_label(item_type),
                    state.selected_item_type == item_type)) {
                state.selected_item_type = item_type;
            }
        }
        ImGui::EndChild();
    }
    ImGui::SameLine();
    {
        if (!ImGui::BeginChild("item_window_details", ImVec2(225, 0), true)) {
            return ImGui::EndChild();
        }
        ImGui::Text("Filters");
        ImGui::Separator();
        ImGui::InputText("Name##filter_item", filter_item_name, IM_ARRAYSIZE(filter_item_name));

        ImGui::Text("Spawn");
        ImGui::Separator();
        ImGui::SliderInt("Quantity##spawn_item", &spawn_item_quantity, 1, 999);
        ImGui::SliderInt("Refine##spawn_item", &spawn_item_refine, 0, 9);
        ImGui::SliderInt("Durability##spawn_item", &spawn_item_durability, 0, 120);
        ImGui::SliderInt("Lifespan##spawn_item", &spawn_item_lifespan, 0, 100, "%d%%");
        static std::string combo_label = "";
        if (ImGui::BeginCombo("Stat/Gem", combo_label.c_str())) {
            for (const auto [id, label]: state.stat_names) {
                const bool is_selected = (spawn_item_stat_id == id);
                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    spawn_item_stat_id = id;
                    combo_label = label;
                }

                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Socket##spawn_item", &spawn_item_socket);
        ImGui::EndChild();
    }
    ImGui::SameLine();
    {
        if (!ImGui::BeginChild("item_window_right_pane",
                ImVec2(0, 0),
                true,
                ImGuiWindowFlags_HorizontalScrollbar)) {
            return ImGui::EndChild();
        }

        for (auto [item_type, stb]: ITEM_DATA) {
            if (item_type != state.selected_item_type) {
                continue;
            }

            for (short item_id = 0; item_id < stb->row_count; ++item_id) {
                std::string item_name(ITEM_NAME(item_type, item_id));
                if (item_name.empty()) {
                    continue;
                }

                if (filter_item_name) {
                    std::string name(item_name);
                    std::string filter(filter_item_name);
                    auto to_lower = [](unsigned char c) { return std::tolower(c); };
                    std::transform(name.begin(), name.end(), name.begin(), to_lower);
                    std::transform(filter.begin(), filter.end(), filter.begin(), to_lower);
                    if (name.find(filter) == std::string::npos) {
                        continue;
                    }
                }

                const int icon_id = ITEM_ICON_NO(item_type, item_id);
                if (!icon_id) {
                    continue;
                }

                stTexture* texture_data = item_image_manager->GetTexture(icon_id);
                stSprite* sprite_data = item_image_manager->GetSprite(icon_id);
                if (!texture_data || !sprite_data) {
                    continue;
                }

                zz_texture* tex = reinterpret_cast<zz_texture*>(texture_data->m_Texture);
                if (!tex) {
                    continue;
                }
                HNODE tex_d3d_id = ::getTexturePointer(texture_data->m_Texture);
                if (!tex_d3d_id) {
                    continue;
                }
                float x1 = static_cast<float>(sprite_data->m_Rect.left) / tex->get_width();
                float y1 = static_cast<float>(sprite_data->m_Rect.top) / tex->get_width();
                float x2 = static_cast<float>(sprite_data->m_Rect.right) / tex->get_width();
                float y2 = static_cast<float>(sprite_data->m_Rect.bottom) / tex->get_width();

                ImTextureID im_tex_id = reinterpret_cast<ImTextureID>(tex_d3d_id);
                ImVec2 size(40, 40);
                ImVec2 uv1(x1, y1);
                ImVec2 uv2(x2, y2);

                std::string button_label = fmt::format("Spawn##{}_{}", item_type, item_id);
                if (ImGui::Button(button_label.c_str())) {
                    std::string spawn_cmd;
                    if (tagBaseITEM::is_stackable(item_type)) {
                        spawn_cmd =
                            fmt::format("/item {} {} {}", item_type, item_id, spawn_item_quantity);
                    } else {
                        spawn_cmd = fmt::format("/item {} {} {} {}",
                            item_type,
                            item_id,
                            spawn_item_stat_id,
                            spawn_item_socket ? 1 : 0);
                    }
                    g_pNet->Send_cli_CHAT(const_cast<char*>(spawn_cmd.c_str()));
                }
                ImGui::SameLine(65.0f);
                ImGui::Text("%d:%d", item_type, item_id);
                ImGui::SameLine(115.0f);
                ImGui::Image(im_tex_id, size, uv1, uv2);
                ImGui::SameLine(165.0f);
                ImGui::Text("%s", item_name.c_str());
            }
        }
        ImGui::EndChild();
    }
}

void
draw_skill_tab(GameWindowState& state) {
    CImageRes* skill_image_manager =
        CImageResManager::GetSingleton().GetImageRes(IMAGE_RES_SKILL_ICON);
    if (!skill_image_manager) {
        return;
    }

    std::vector<std::tuple<int, const char*>> skill_job_data = {
        {0, "General"},
        {41, "Soldier"},
        {42, "Muse"},
        {43, "Hawker"},
        {44, "Dealer"},
    };

    {
        if (!ImGui::BeginChild("skill_window_left_pane", ImVec2(150, 0), true)) {
            return ImGui::EndChild();
        }

        for (auto const& [job_id, job_name]: skill_job_data) {
            if (ImGui::Selectable(job_name, state.selected_skill_job == job_id)) {
                state.selected_skill_job = job_id;
            }
        }

        ImGui::EndChild();
    }
    ImGui::SameLine();
    {
        if (!ImGui::BeginChild("skill_window_right_pane", ImVec2(0, 0), true)) {
            return ImGui::EndChild();
        }

        const STBDATA& stb = g_SkillList.m_SkillDATA;
        for (size_t skill_id = 0; skill_id < stb.row_count; ++skill_id) {
            const std::string skill_name(SKILL_NAME(skill_id));
            if (skill_name.empty()) {
                continue;
            }

            // Only include first level of skills or individual skills
            const int skill_core_id(SKILL_1LEV_INDEX(skill_id));
            if (skill_core_id != skill_id) {
                continue;
            }

            const int skill_icon(SKILL_ICON_NO(skill_id));
            if (!skill_icon) {
                continue;
            }

            int skill_job(SKILL_AVAILBLE_CLASS_SET(skill_id));
            switch (skill_job) {
                // Soldier
                case 61:
                case 62:
                    skill_job = 41;
                    break;
                // Muse
                case 63:
                case 64:
                    skill_job = 42;
                    break;
                case 65:
                case 66:
                    skill_job = 43;
                    break;
                case 67:
                case 68:
                    skill_job = 44;
                    break;
            }

            if (skill_job != state.selected_skill_job) {
                continue;
            }

            stTexture* texture_data = skill_image_manager->GetTexture(skill_icon);
            stSprite* sprite_data = skill_image_manager->GetSprite(skill_icon);
            if (!texture_data || !sprite_data) {
                continue;
            }

            zz_texture* tex = reinterpret_cast<zz_texture*>(texture_data->m_Texture);
            if (!tex) {
                continue;
            }
            HNODE tex_d3d_id = ::getTexturePointer(texture_data->m_Texture);
            if (!tex_d3d_id) {
                continue;
            }
            float x1 = static_cast<float>(sprite_data->m_Rect.left) / tex->get_width();
            float y1 = static_cast<float>(sprite_data->m_Rect.top) / tex->get_width();
            float x2 = static_cast<float>(sprite_data->m_Rect.right) / tex->get_width();
            float y2 = static_cast<float>(sprite_data->m_Rect.bottom) / tex->get_width();

            ImTextureID im_tex_id = reinterpret_cast<ImTextureID>(tex_d3d_id);
            ImVec2 size(40, 40);
            ImVec2 uv1(x1, y1);
            ImVec2 uv2(x2, y2);

            // Button to learn skill
            std::string button_label = fmt::format("Learn##{}", skill_id);
            if (ImGui::Button(button_label.c_str())) {
                std::string cmd = fmt::format("/add skill {}", skill_id);
                g_pNet->Send_cli_CHAT(const_cast<char*>(cmd.c_str()));
            }
            ImGui::SameLine(65.0f);
            ImGui::Image(im_tex_id, size, uv1, uv2);
            ImGui::SameLine(115.0f);
            ImGui::Text("%s", skill_name.c_str());
        }

        ImGui::EndChild();
    }
}

void
draw_map_tab(GameWindowState& state) {
    {
        if (!ImGui::BeginChild("map_window_left_pane", ImVec2(225, 0), true)) {
            return ImGui::EndChild();
        }

        for (size_t map_id = 0; map_id < g_TblZONE.row_count; ++map_id) {
            std::string map_name(ZONE_NAME(map_id));
            if (map_name.empty()) {
                continue;
            }

            if (ImGui::Selectable(map_name.c_str(), state.selected_map_id == map_id)) {
                state.selected_map_id = map_id;
            }
        }
        ImGui::EndChild();
    }
    ImGui::SameLine();
    {
        if (!ImGui::BeginChild("map_window_right_pane", ImVec2(0, 0), true)) {
            return ImGui::EndChild();
        }

        ImGui::Text(ZONE_NAME(state.selected_map_id));
        ImGui::Separator();
        if (ImGui::Button("Teleport")) {
            std::string cmd = fmt::format("/tp {}", state.selected_map_id);
            g_pNet->Send_cli_CHAT(const_cast<char*>(cmd.c_str()));
        }

        ImGui::EndChild();
    }
}

void
draw_game_window(GameWindowState& state) {
    if (!ImGui::BeginTabBar("game_data_tabs")) {
        return;
    }

    if (ImGui::BeginTabItem("Item")) {
        draw_item_tab(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Skill")) {
        draw_skill_tab(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Map")) {
        draw_map_tab(state);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}

#endif