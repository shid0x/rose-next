#include "stdafx.h"

#include <iostream>

#include "lib_util.h"
#include "sho_ws_lib.h"

#include "rose/common/server_config.h"
#include "rose/common/util.h"

SHO_WS* g_instance;

int
shutdown(int exit_code = 0) {
    LOG_INFO("Shutting down...");
    g_instance->Active(false);
    g_instance->ShutdownCLI_SOCKET();
    g_instance->Shutdown();
    g_instance->Destroy();
    return exit_code;
}

BOOL WINAPI
CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
            // Handle the CTRL-C signal.
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            return shutdown(1);
        default:
            return FALSE;
    }
}

int
main(int argc, char** argv) {
    HWND console_window = GetConsoleWindow();
    HINSTANCE console_handle = GetModuleHandle(nullptr);
    SetConsoleTitle("ROSE Next - World Server");

    // Parse args
    auto args = Rose::Util::parse_args(argc, argv);

    std::string config_path = get_exe_dir().append("server.toml");
    auto config_path_arg = args.find("--config");
    if (config_path_arg != args.end()) {
        config_path = config_path_arg->second;
    }

    // Load Config
    Rose::Common::ServerConfig config;
    bool config_loaded = config.load(config_path, "ROSE");

    // Initialize the logger
    Rose::Common::logger_init(config.worldserver.log_path.c_str(), config.worldserver.log_level);

    if (!config_loaded) {
        LOG_WARN("Could not load config file, using default settings");
    }

    // Start the server
    LOG_INFO("Initializing the server");
    g_instance = SHO_WS::InitInstance(console_handle,
        (char*)config.worldserver.data_dir.c_str(),
        config.worldserver.language);

    LOG_INFO("Connecting to the database");
    bool db_connected = g_instance->connect_database(config);
    if (!db_connected) {
        return shutdown(1);
    }

    LOG_INFO("Starting the server");
    g_instance->Start(console_window,
        (char*)config.loginserver.ip.c_str(),
        config.loginserver.server_port,
        (char*)config.worldserver.ip.c_str(), // Log Server IP
        config.worldserver.port, // Log server port
        (char*)config.worldserver.world_name.c_str(),
        config.worldserver.server_port,
        config.worldserver.port,
        false);

    LOG_INFO("Starting the client socket");
    g_instance->StartCLI_SOCKET();

    LOG_INFO("Activating the server");
    g_instance->Active(true);

    SetConsoleCtrlHandler(CtrlHandler, true);

    while (true) {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}