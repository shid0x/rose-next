#ifndef __LIB_GAMEMAIN_H
#define __LIB_GAMEMAIN_H

#define __SHO_GS

#define ENABLE_CHATROOM // Enables the chatroom functions

#include "LIB_Util.h"

#include "CClientSOCKET.h"
#include "CVector.h"
#include "DLLIST.h"
#include "DataTYPE.h"
#include "NET_Prototype.h"
#include "SLLIST.h"
#include "util/classhash.h"
#include "classINDEX.h"
#include "classIOCP.h"
#include "util/classstr.h"
#include "classSYNCOBJ.h"
#include "classTIME.h"
#include "classUTIL.h"
#include "md5.h"

#include "ETC_Math.h"

#include "rose/io/stb.h"
#include "OBJECT.h"

#include "rose/common/log.h"
#include "rose/common/game_config.h"
#include "rose/common/server_config.h"

#define GS_TIMER_LSV 1
#define GS_TIMER_WORLD_TIME 3

#define RECONNECT_TIME_TICK 10000 // 10 sec


namespace Rose::Common {
class ServerConfig;
}

class CLIB_GameSRV {
public:
    Rose::Common::ServerConfig config;

private:
    static CLIB_GameSRV* m_pInstance;

    CTimer* m_pWorldTIMER;
    bool* m_pCheckedLocalZONE;

    DWORD m_dwRandomSEED;

    BYTE m_btChannelNO;
    BYTE m_btLowAGE;
    BYTE m_btHighAGE;
    DWORD m_dwUserLIMIT;

    int m_iLangTYPE;

    CLIB_GameSRV();
    virtual ~CLIB_GameSRV();
    void init(HINSTANCE hInstance, const Rose::Common::ServerConfig& config);

    bool CheckSTB_UseITEM();
    bool CheckSTB_NPC();
    bool CheckSTB_DropITEM();
    bool CheckSTB_ItemRateTYPE();
    bool CheckSTB_Motion();
    bool CheckSTB_GemITEM();
    bool CheckSTB_ListPRODUCT();

    bool Load_BasicDATA();
    void Free_BasicDATA();

    bool ConnectToLSV();
    void DisconnectFromLSV();

    friend VOID CALLBACK GS_TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

public:
    bool connect_database();
    bool reload_game_config();

    bool Start(HWND hMainWND,
        BYTE btChannelNO,
        BYTE btLowAge,
        BYTE btHighAge);
    void Shutdown();

    short InitLocalZone(bool bAllActive);
    bool CheckZoneToLocal(short nZoneNO, bool bChecked);
    char* GetZoneName(short nZoneNO);
    bool IsLocalCheckedZONE(short nZoneNO) { return m_pCheckedLocalZONE[nZoneNO]; }

    DWORD GetRandomSeed() { return m_dwRandomSEED; }

    BYTE GetChannelNO() { return m_btChannelNO; }
    BYTE GetLowAGE() { return m_btLowAGE; }
    BYTE GetHighAGE() { return m_btHighAGE; }

    int GetLangTYPE() { return m_iLangTYPE; }

    void Set_UserLIMIT(DWORD dwUserLimit);
    DWORD Get_UserLIMIT() { return m_dwUserLIMIT; }

    static CLIB_GameSRV* GetInstance() { return m_pInstance; }
    static CLIB_GameSRV* InitInstance(HINSTANCE hInstance, const Rose::Common::ServerConfig& config) {
        if (NULL == m_pInstance) {
            m_pInstance = new CLIB_GameSRV();
            _ASSERT(m_pInstance);
            m_pInstance->init(hInstance, config);
        }
        return m_pInstance;
    }

    void Destroy() { SAFE_DELETE(m_pInstance); }

    static Rose::Common::GameConfig& game_config() {
        return CLIB_GameSRV::GetInstance()->config.game;
    }
};

extern VOID CALLBACK GS_TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

inline short
Get_ServerChannelNO() {
    return CLIB_GameSRV::GetInstance()->GetChannelNO();
}
inline int
Get_ServerLangTYPE() {
    return CLIB_GameSRV::GetInstance()->GetLangTYPE();
}

inline Rose::Common::ServerConfig& server_config() {
    return CLIB_GameSRV::GetInstance()->config;
}

inline bool
reload_server_game_config() {
    return CLIB_GameSRV::GetInstance()->reload_game_config();
}
#endif
