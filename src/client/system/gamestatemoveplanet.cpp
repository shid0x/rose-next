#include "stdafx.h"

#include ".\gamestatemoveplanet.h"
#include "CCamera.h"
#include "CGame.h"
#include "CSkyDome.h"
#include "SystemProcScript.h"
#include "System_FUNC.h"
#include "../GameProc/LiveCheck.h"

#include "cclientstorage.h"
#include "SFX/SFXManager.h"

const int CUTSCENE_ZONE_NO = 19;

int CGameStateMovePlanet::m_iTargetPlanet = 1;

CGameStateMovePlanet::CGameStateMovePlanet(void) {}

CGameStateMovePlanet::CGameStateMovePlanet(int iStateID) {
    m_iStateID = iStateID;
}

CGameStateMovePlanet::~CGameStateMovePlanet(void) {}

int
CGameStateMovePlanet::Update(bool bLostFocus) {

    g_pCamera->Update();
    g_pTerrain->SetCenterPosition(520000.0f, 520000.0f);

    g_pEffectLIST->Proc();

    CSFXManager::GetSingleton().Update();

    g_DayNNightProc.Proc();

    CSystemProcScript::GetSingleton().CallLuaFunction("ProcMovePlanetCutScene",
        ZZ_PARAM_INT,
        g_DayNNightProc.GetZoneTime(),
        ZZ_PARAM_END);
    CLiveCheck::GetSingleton().Check();

    ::updateScene();

    // processing  ...
    if (g_ClientStorage.m_VideoOption.background_render || !bLostFocus) {
        if (::beginScene()) {
            ::setClearColor(0.0f, 0.0f, 0.0f);
            ::clearScreen();
            ::renderScene();

            ::beginSprite(D3DXSPRITE_ALPHABLEND);
            CSFXManager::GetSingleton().Draw();
            ::endSprite();

            this->render_dev_ui();
            ::endScene();
            ::swapBuffers();
        }
    }

    return 0;
}

int
CGameStateMovePlanet::Enter(int iPrevStateID) {
    ::SetOceanSFXOnOff(false);

    /// 현재 존데이터 풀고 새로운 존 데이터 로드
    g_pTerrain->FreeZONE();

    g_GameDATA.m_bJustObjectLoadMode = true;

    /// 새로운 컷씬 존 로드
    g_pTerrain->LoadZONE(CUTSCENE_ZONE_NO);
    g_pTerrain->InitZONE(0, 0);

    HNODE hCameraMotion = ::findNode("MovePlanet_Camera01");
    if (hCameraMotion == 0)
        hCameraMotion = ::loadMotion("MovePlanet_Camera01",
            "3DData\\CUTSCENE\\WARP01\\Camera01.ZMO",
            1,
            4,
            3,
            1,
            0);

    HNODE motion_camera = ::findNode("MovePlanet_camera");
    if (motion_camera == 0)
        motion_camera = ::loadCamera("MovePlanet_camera", "cameras\\camera01.zca", hCameraMotion);

    CSkyDOME::Init(g_GameDATA.m_hShader_sky,
        g_GameDATA.m_hLight,
        ZONE_BG_IMAGE(g_pTerrain->GetZoneNO()));

    g_pCamera->Init(motion_camera);

    CSystemProcScript::GetSingleton().CallLuaFunction("InitPlanetMoveWorld",
        ZZ_PARAM_INT,
        1,
        ZZ_PARAM_END);

    D3DVECTOR PosENZIN;
    PosENZIN.x = 520000.0f;
    PosENZIN.y = 520000.0f;
    PosENZIN.z = 0.0f;

    g_pCamera->Set_Position(PosENZIN);

    ::setFogRange(70000, 80000);
    setAlphaFogRange(70000, 80000);

    g_DayNNightProc.SetWorldTime(g_pTerrain->GetZoneNO(), 0);
    g_DayNNightProc.SetDayEnviTick(100);

    D3DXVECTOR3 posAVATAR;
    posAVATAR.x = 0;
    posAVATAR.y = 0;
    posAVATAR.z = 0.0f;
    g_pAVATAR->ResetCUR_POS(posAVATAR);

    CSFXManager::GetSingleton().StartWideScreenEffect();

    return 0;
}

int
CGameStateMovePlanet::Leave(int iNextStateID) {
    g_pCamera->ResetToAvatarCamera();

    g_GameDATA.m_bJustObjectLoadMode = false;

    g_DayNNightProc.ResetDayEnviTick();

    CSystemProcScript::GetSingleton().CallLuaFunction("ClearMovePlanetCutScene", ZZ_PARAM_END);
    StopScreenFadeInOut();

    ::SetOceanSFXOnOff(true);

    t_OptionVideo option;
    g_ClientStorage.GetVideoOption(option);
    g_ClientStorage.ApplyCameraOption(option.iCamera);

    /// SFX
    CSFXManager::GetSingleton().StopWideScreenEffect();

    return 0;
}

int
CGameStateMovePlanet::ProcMouseInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    return 0;
}

int
CGameStateMovePlanet::ProcKeyboardInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    switch (uiMsg) {
        case WM_KEYDOWN: {
            CGame::GetInstance().ChangeState(CGame::GS_WARP);
            break;
        } 
        default:
            break;
    }
    return 0;
}
