
#include "stdAFX.h"

#include "SHO_LS_LIB.h"

#include "CLS_Account.h"
#include "CLS_Client.h"
#include "CLS_Server.h"
#include "CLS_SqlTHREAD.h"
#include "blockLIST.h"
#include "csocketwnd.h"

using namespace Rose::Common;

SHO_LS* SHO_LS::m_pInstance = NULL;

CLS_AccountLIST* g_pListJOIN = NULL;
CLS_AccountLIST* g_pListWAIT = NULL;

CLS_SqlTHREAD* g_pThreadSQL = NULL;

CLS_ListCLIENT* g_pListCLIENT;
CLS_ListSERVER* g_pListSERVER;

#define DEF_CLIENT_POOL_SIZE 8192 // µ¿½Ã Á¢¼Ó ´ë±â ¼ÒÄÏ
#define INC_CLIENT_POOL_SIZE 1024

#define DEF_SERVER_POOL_SIZE 256
#define INC_SERVER_POOL_SIZE 128

#define DEF_SEND_IO_POOL_SIZE (DEF_CLIENT_POOL_SIZE + INC_CLIENT_POOL_SIZE)
#define INC_SEND_IO_POOL_SIZE (INC_CLIENT_POOL_SIZE)

#define JOIN_HASH_TABLE_SIZE 4096
#define WAIT_HASH_TABLE_SIZE 512

#define LS_TIMER_CHECK_WAIT_LIST 1
#define LS_TICK_CHECK_WAIT_LIST 1000

static DWORD s_dwTick = 0;
VOID CALLBACK
LS_TimerProc(HWND hwnd /* handle to window */,
    UINT uMsg /* WM_TIMER message */,
    UINT_PTR idEvent /* timer identifier */,
    DWORD dwTime /* current system time */) {
    if (LS_TIMER_CHECK_WAIT_LIST == idEvent) {
        if (++s_dwTick % 2)
            g_pListWAIT->Delete_IdleACCOUNT(90); // ¼­¹ö ÀÌµ¿ÇÏ´Âµ¥ 30ÃÊ ÀÌ»ó °É¸®¸é Â©·¯~
        else
            g_pListCLIENT->Delete_IdleSOCKET();
    }
}

#include "../sho_ls.ver"
DWORD
GetServerBuildNO() {
    return BUILD_NUM;
}
DWORD g_dwStartTIME = 0;

DWORD
GetServerStartTIME() {
    return g_dwStartTIME;
}

SHO_LS::SHO_LS(): m_pTIMER(nullptr) {}

SHO_LS::~SHO_LS() {
    SAFE_DELETE(m_pTIMER);

    if (g_pThreadSQL) {
        g_pThreadSQL->Destroy();
        g_pThreadSQL = NULL;
    }

    SAFE_DELETE(g_pListJOIN);
    SAFE_DELETE(g_pListWAIT);

    SAFE_DELETE(g_pListCLIENT);
    SAFE_DELETE(g_pListSERVER);

    if (CSocketWND::GetInstance()) {
        CSocketWND::GetInstance()->Destroy();
    }
    CPoolSENDIO::Destroy();
}

void
SHO_LS::init(HINSTANCE hinstance, HWND hwnd, const Rose::Common::ServerConfig& config) {
    ::Sleep(500);

    CPoolSENDIO::Instance(DEF_SEND_IO_POOL_SIZE, INC_SEND_IO_POOL_SIZE);

    g_pListCLIENT = new CLS_ListCLIENT(DEF_CLIENT_POOL_SIZE, INC_CLIENT_POOL_SIZE);
    g_pListSERVER = new CLS_ListSERVER(DEF_SERVER_POOL_SIZE, INC_SERVER_POOL_SIZE);

    g_pListJOIN = new CLS_AccountLIST((char*)"JOIN", JOIN_HASH_TABLE_SIZE);
    g_pListWAIT = new CLS_AccountLIST((char*)"WAIT", WAIT_HASH_TABLE_SIZE);

    CSocketWND* pSockWND = CSocketWND::InitInstance(hinstance, 1);

    this->hinstance = hinstance;
    this->hwnd = hwnd;
    this->config = config;
}

bool
SHO_LS::start() {
    LOG_INFO("Starting the database connection.");
    bool db_connected = this->connect_database(this->config.database);
    if (!db_connected) {
        return false;
    }

    LOG_INFO("Starting the server socket");
    bool started = this->start_server_listener(this->hwnd, this->config.loginserver.server_port);
    if (!started) {
        return false;
    }

    LOG_INFO("Starting the client socket");
    bool client_listener_started = this->start_client_listener(
        this->config.loginserver.port,
        this->config.loginserver.password,
        this->config.loginserver.max_users);
    if (!client_listener_started) {
        return false;
    }

    return true;
}

void
SHO_LS::stop() {
    this->stop_client_listener();
    this->stop_server_listener();
    SAFE_DELETE(m_pTIMER);
}

//-------------------------------------------------------------------------------------------------
void
SHO_LS::Send_ANNOUNCE(void* pServer, char* szAnnounceMsg) {
    if (g_pListSERVER) {
        g_pListSERVER->Send_lsv_ANNOUNCE_CHAT(pServer, szAnnounceMsg);
    }
}

bool
SHO_LS::connect_database(const DatabaseConfig& db_config) {
    if (!g_pThreadSQL) {
        g_pThreadSQL = CLS_SqlTHREAD::Instance();

        if (!g_pThreadSQL->db.connect(db_config.connection_string)) {
            std::string error_message = g_pThreadSQL->db.last_error_message();
            LOG_ERROR("Failed to connect to the database: {}", error_message.c_str());

            g_pThreadSQL->Destroy();
            g_pThreadSQL = NULL;
            return false;
        }

        g_pThreadSQL->Resume();
    }
    this->set_minimum_access_level(this->config.loginserver.minimum_access_level);
    return true;
}

bool
SHO_LS::start_server_listener(HWND hMainWND, uint32_t server_port) {

    g_dwStartTIME = classTIME::GetCurrentAbsSecond();

    g_pListSERVER->Active(server_port, 1024, 60);

    m_pTIMER = new CTimer(hMainWND,
        LS_TIMER_CHECK_WAIT_LIST,
        LS_TICK_CHECK_WAIT_LIST,
        (TIMERPROC)LS_TimerProc);
    m_pTIMER->Start();

    return true;
}

void
SHO_LS::stop_server_listener() {
    if (g_pListSERVER) {
        g_pListSERVER->Shutdown();
    }
}

bool
SHO_LS::start_client_listener(uint32_t port, const std::string& password, uint32_t max_users) {
    // TODO: This is will crash if password is less than 32 bytes long.
    // Requires fixing here and syncing with world server.
    ::CopyMemory(this->password, (byte*)password.c_str(), 64);

    g_pListCLIENT->Active(port, 65535, 3 * 60);
    this->set_max_users(max_users);

    return true;
}

void
SHO_LS::stop_client_listener() {
    if (g_pListCLIENT) {
        g_pListCLIENT->Shutdown();
    }
}

void
SHO_LS::set_max_users(int count) {
    if (g_pListCLIENT) {
        g_pListCLIENT->max_users = count;
    }
}

void
SHO_LS::set_minimum_access_level(int access_level) {
    if (g_pThreadSQL) {
        g_pThreadSQL->minimum_access_level = access_level;
    }
}