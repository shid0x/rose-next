#pragma once

#ifdef _DEBUG

struct GameWindowState {
    // Main Window
    bool is_open = false;

    // Item tab
    int selected_item_type = ITEM_TYPE_FACE_ITEM;
    std::vector<std::tuple<size_t, std::string>> stat_names; // Loaded at runtime

    // Skill tab
    int selected_skill_job = 0;

    // Map tab
    int selected_map_id = 1;

};

void load_stat_data(GameWindowState& state);

void draw_game_window(GameWindowState& state);

#endif