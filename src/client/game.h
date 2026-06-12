#ifndef __GAME_H
#define __GAME_H

#include "Util/JSingleton.h"
#include "util/classstr.h"
#include "datatype.h"

#include <string>

#define SHADER_NOLIT_SKIN 0
#define SHADER_NOLIT 1

enum {
    FONT_NORMAL = 0,
    FONT_LARGE,
    FONT_SMALL,
    FONT_NORMAL_BOLD,
    FONT_LARGE_BOLD,
    FONT_SMALL_BOLD,
    FONT_TITLE,
    FONT_DESCRIPTION,
    FONT_NORMAL_OUTLINE,
    FONT_OUTLINE_18_BOLD,
    FONT_OUTLINE_14_BOLD,
    FONT_OUTLINE_24_BOLD,
    FONT_OUTLINE_16_BOLD,
    FONT_OUTLINE_11_BOLD,
    MAX_FONT,
};

class CGAMEDATA: public CJSingleton<CGAMEDATA> {
public:
    CGAMEDATA();
    ~CGAMEDATA();

public:
    std::string server_ip;
    uint16_t server_port;

    std::string username;
    std::string password;

    uint8_t auto_connect_server_id;
    uint8_t auto_connect_channel_id;
    std::string auto_connect_character_name;

public:
    // Returns true if there is enough information to auto-connect the user
    bool auto_connect();

    bool m_bDrawBoundingVolume;

    HNODE m_hShader_nolit_skin;
    HNODE m_hShader_nolit;
    HNODE m_hShader_terrain;
    HNODE m_hShader_terrain_Rough;
    HNODE m_hShader_lightmap;
    HNODE m_hShader_ocean;
    HNODE m_hShader_specular;
    HNODE m_hShader_specular_skin;
    HNODE m_hShader_lit_skin;
    HNODE m_hShader_lit;
    HNODE m_hShader_sky;

    bool m_bNoUI; // 인터페이스 표시할지 여부. 디폴트 false - zho
    bool m_bNoWeight; ///무게제한 무시 flag: 추후 클라이언트에서 뺄것

    bool m_bTranslate; // Raven - Translate Tool
    CStrVAR m_cLang; // Raven- Language

    bool m_bFilmingMode; // 플레이 동영상 촬영 모드
    bool m_bShowCursor; // 마우스 커서 보이냐 안보이냐
    bool m_bShowDropItemInfo; // 드랍아이템 정보 보이기( 현재 alt 키가 눌릴경우
                              // 활성화 )

#ifdef _DEBUG
    bool m_bShowCurPos;
#endif

    /// 시스템 타임.
    SYSTEMTIME m_SystemTime;

    bool m_bDisplayDebugInfo;
    float m_fTestVal;
    bool m_bForOpenTestServer;

    int m_iTemp;
    bool m_bCheckDupRUN; /// 중복실행 방지...

    HNODE m_hLight;
    HNODE m_hCharacterLight;
    HNODE m_hCharacterLight2;

    CStrVAR m_TrailTexture;

    HNODE m_hFONT[MAX_FONT];

    tPOINTF m_PosCENTER;
    POINTS m_PosPATCH;
    CRITICAL_SECTION m_csNZIN;

    /// 스태미너 데이터
    int m_iWorldStaminaVal;
    int AdjustAvatarStamina(int iGetExp);

    void Init() { ::InitializeCriticalSection(&m_csNZIN); }
    void Free() { ::DeleteCriticalSection(&m_csNZIN); }
    void Lock() { ::EnterCriticalSection(&m_csNZIN); }
    void Unlock() { ::LeaveCriticalSection(&m_csNZIN); }

    DWORD m_dwGameStartTime;
    DWORD m_dwElapsedGameTime;
    DWORD m_dwFrevFrameTime;
    DWORD m_dwElapsedFrameTime;

    void UpdateGameTime();
    DWORD GetTime();
    DWORD GetGameTime() { return GetTime(); }
    DWORD GetElapsedFrameTime() { return m_dwElapsedFrameTime; }

    /// 게임시작후 진행된 프레임수
    DWORD m_dwElapsedGameFrame;

    ///
    /// Random number
    ///

    int Random(unsigned int iRand) { return rand() % iRand; };
    void RandomSeedInit(unsigned int iSeed) { srand(iSeed); }

    void Update();

    //--------------------------------------------------------------------------------------
    /// 게임 연출관련
    //--------------------------------------------------------------------------------------
    bool m_bJustObjectLoadMode; /// 지형 블락을 등록하지 않을 것인가?

#if defined(_DEBUG) || defined(_D_RELEASE)
    bool m_bObserverCameraMode; /// 디버깅용 업져버 카메라..
#endif

    //--------------------------------------------------------------------------------------
    /// 경험치 획득 딜레이를 위한 변수
    //--------------------------------------------------------------------------------------
    __int64 m_iReceivedAvatarEXP;

    //--------------------------------------------------------------------------------------
    /// 서버및 채널 정보
    //--------------------------------------------------------------------------------------
    std::string m_strJoinServerName;
    std::string m_strJoinChannelName;

    //--------------------------------------------------------------------------------------
    /// NHN Japan을 위한 Data
    //--------------------------------------------------------------------------------------
    bool m_is_NHN_JAPAN;

private:
};

#define g_GameDATA (CGAMEDATA::GetSingleton())

extern D3DCOLOR g_dwRED;
extern D3DCOLOR g_dwGREEN;
extern D3DCOLOR g_dwBLUE;
extern D3DCOLOR g_dwBLACK;
extern D3DCOLOR g_dwWHITE;
extern D3DCOLOR g_dwYELLOW;
extern D3DCOLOR g_dwGRAY;
extern D3DCOLOR g_dwVIOLET;
extern D3DCOLOR g_dwORANGE;
extern D3DCOLOR g_dwPINK;
extern D3DCOLOR g_dwCOLOR[];

void DrawLoadingImage();

void DestroyWaitDlg();

#endif
