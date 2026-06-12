#include "stdafx.h"

#undef __T_PACKET

#include "sho_ls_lib.h"

#include "rose/common/server_config.h"
#include "rose/common/util.h"

SHO_LS* g_instance;

int
shutdown(int exit_code = 0) {
    LOG_INFO("Shutting down...");
    g_instance->stop();
    g_instance->destroy_instance();
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
    SetConsoleTitle("ROSE Next - Login Server");

    // Parse args
    auto args = Rose::Util::parse_args(argc, argv);

    std::string config_path = get_exe_dir().append("server.toml");
    auto config_path_arg = args.find("--config");
    if (config_path_arg != args.end()) {
        config_path = config_path_arg->second;
    }

    // Load config
    Rose::Common::ServerConfig config;
    bool config_loaded = config.load(config_path, "ROSE");

    // Initialize the logger
    Rose::Common::logger_init(config.loginserver.log_path.c_str(), config.loginserver.log_level);

    if (!config_loaded) {
        LOG_WARN("Could not load config file, using default settings");
    }

    // Start server
    LOG_INFO("Initializing the server");
    g_instance = SHO_LS::init_instance(console_handle, console_window, config);

    LOG_INFO("Starting the server");
    bool server_started = g_instance->start();
    if (!server_started) {
        LOG_ERROR("Failed to start the server, shutting down");
        return shutdown(1);
    }

    SetConsoleCtrlHandler(CtrlHandler, true);

    LOG_INFO("Server ready");

    while (true) {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}