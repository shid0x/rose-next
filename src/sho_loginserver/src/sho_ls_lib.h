#pragma once

// #include "classTIME.h"

#include "rose/common/server_config.h"

class CTimer;



class SHO_LS {
public:
    static SHO_LS* m_pInstance;

    CTimer* m_pTIMER;

    HINSTANCE hinstance;
    HWND hwnd;
    Rose::Common::ServerConfig config;

public:
    SHO_LS();
    ~SHO_LS();

    void init(HINSTANCE hInstance, HWND hwnd, const Rose::Common::ServerConfig& config);
    bool start();
    void stop();

    bool connect_database(const Rose::Common::DatabaseConfig& config);
    bool start_server_listener(HWND hwnd, uint32_t port);
    void stop_server_listener();
    bool start_client_listener(uint32_t port, const std::string& password, uint32_t max_users);
    void stop_client_listener();

    void set_max_users(int count);
    void set_minimum_access_level(int access_level);

public:
    union {
        BYTE password[65];
        DWORD password_buffer[16];
    };

    void Send_ANNOUNCE(void* pServer, char* szAnnounceMsg);

    static SHO_LS* GetInstance() { return m_pInstance; }
    static SHO_LS* init_instance(HINSTANCE hinstance, HWND hwnd, const Rose::Common::ServerConfig& config) {
        if (!m_pInstance) {
            m_pInstance = new SHO_LS();
            m_pInstance->init(hinstance, hwnd, config);
        }
        return m_pInstance;
    }
    void destroy_instance() { SAFE_DELETE(m_pInstance); }
};

extern DWORD GetServerBuildNO();
extern DWORD GetServerStartTIME();