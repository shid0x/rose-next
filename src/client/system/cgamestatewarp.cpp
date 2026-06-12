#include "stdafx.h"

#include "CGameStateWarp.h"
#include <process.h>
#include "../Object.h"
#include "../CCamera.h"
#include "../CClientStorage.h"
#include "../CSkyDOME.h"
#include "../Network/CNetwork.h"
#include "../interface/ExternalUI/ExternalUILobby.h"
#include "../interface/CLoading.h"
#include "../interface/CUIMediator.h"
#include "../interface/Dlgs/CMinimapDlg.h"

#include "CGame.h"
#include <crtdbg.h>

static bool tDone = false;
CGameStateWarp::CGameStateWarp(int iID) {
    m_iStateID = iID;
}

CGameStateWarp::~CGameStateWarp(void) {}

int
CGameStateWarp::Update(bool bLostFocus) {
#ifdef __THREADED_LOADING
    // Draw();
    DWORD ret = WaitForSingleObject(m_hThread, 100);

    switch (ret) {
        case WAIT_OBJECT_0:
            if (tDone)
                CGame::GetInstance().ChangeState(CGame::GS_MAIN);
            break;
        case WAIT_TIMEOUT:
            break;
        case WAIT_FAILED:
            g_pCApp->SetExitGame();
            break;
        default:
            break;
    }
#endif
    return 0;
}

int
CGameStateWarp::Enter(int iPrevStateID) {
    pauseSpriteSFX(true);

    m_iPrevStateID = iPrevStateID;

    LogString(LOG_NORMAL, "FreeZone(%d)\n", g_pTerrain->GetZoneNO());

    int iPrevZoneNum = g_pTerrain->GetZoneNO();
    int iNextZoneNum = 0;
    g_pTerrain->FreeZONE();

#ifdef __THREADED_LOADING
    gsv_TELEPORT_REPLY Reply;
    CGame::GetInstance().GetLoadingData(Reply);
    iNextZoneNum = Reply.m_nZoneNO;
    g_Loading.LoadTexture(Reply.m_nZoneNO, ZONE_PLANET_NO(Reply.m_nZoneNO));
    Draw();

    m_hThread = (HANDLE)_beginthreadex(NULL, 0, &ThreadFunc, NULL, CREATE_SUSPENDED, NULL);
    SetThreadPriority(m_hThread, THREAD_PRIORITY_HIGHEST);

    if (m_hThread) {
        tDone = false;
        ResumeThread(m_hThread);
    } else /// Thread 생성 실패시 메인쓰레드에서 로딩하고 State를 바꾸어 준다.
#endif
    {
#ifndef __THREADED_LOADING
        gsv_TELEPORT_REPLY Reply;
        CGame::GetInstance().GetLoadingData(Reply);

        iNextZoneNum = Reply.m_nZoneNO;

        g_Loading.LoadTexture(Reply.m_nZoneNO, ZONE_PLANET_NO(Reply.m_nZoneNO));
        Draw();
#endif
        ThreadFunc(NULL);
        CGame::GetInstance().ChangeState(CGame::GS_MAIN);
    }

    return 0;
}
int
CGameStateWarp::Leave(int iNextStateID) {
    g_Loading.UnloadTexture();
    gsv_TELEPORT_REPLY Reply;
    CGame::GetInstance().GetLoadingData(Reply);

    g_pAVATAR->Warp_POSITION(Reply.m_PosWARP.x, Reply.m_PosWARP.y);

    g_pCamera->Attach(g_pAVATAR->GetZMODEL());
    g_pCamera->Update();
    /// Loading mode change
    //	g_pTerrain->SetLoadingMode( true );
    //	::setDelayedLoad( true );

    CSkyDOME::Init(g_GameDATA.m_hShader_sky,
        g_GameDATA.m_hLight,
        ZONE_BG_IMAGE(g_pTerrain->GetZoneNO()));

    /// PAT Setting
    g_pAVATAR->m_bRunMODE = Reply.m_btRunMODE;
    g_pAVATAR->m_btMoveMODE = Reply.m_btRideMODE;

//-------------------------------------------------------------------------------------------------
//박지호::2인승일때 자신의 카트를 생성하지 않도록 한다.
#if defined(_GBC)
    if (g_pAVATAR->m_btMoveMODE == MOVE_MODE_DRIVE) {
#else if
    if (g_pAVATAR->m_btMoveMODE == MOVE_MODE_DRIVE || g_pAVATAR->m_btMoveMODE == MOVE_MODE_RIDEON) {
#endif
        //-------------------------------------------------------------------------------------------------
        g_pAVATAR->CreateCartFromMyData();
        g_pAVATAR->GetSkillSlot()->SetActiveSkillEnableByRideState(true);
    } else {
        g_pAVATAR->GetSkillSlot()->SetActiveSkillEnableByRideState(false);
    }

    pauseSpriteSFX(false);

    return 0;
}

void
CGameStateWarp::Draw() /// Test Code
{
    if (g_pCApp->IsActive()) {
        if (!::beginScene()) //  디바이스가 손실된 상태라면 0을 리턴하므로, 모든 렌더링 스킵
        {
            return;
        }

        ::clearScreen();

        ::beginSprite(D3DXSPRITE_ALPHABLEND);

        g_Loading.Draw();

        ::endSprite();

        this->render_dev_ui();
        ::endScene();
        ::swapBuffers();
    }
}

unsigned __stdcall CGameStateWarp::ThreadFunc(void* pArguments) {
    gsv_TELEPORT_REPLY Reply;
    CGame::GetInstance().GetLoadingData(Reply);

    //	::setDelayedLoad( 2 );

    ::setDelayedLoad(0);

    LogString(LOG_NORMAL, "LoadZone(%d)\n", Reply.m_nZoneNO);
    g_pTerrain->LoadZONE(Reply.m_nZoneNO);
    g_pTerrain->InitZONE(Reply.m_PosWARP.x, Reply.m_PosWARP.y);

    /// 개인상점 리스트 클리어
    g_UIMed.ResetPersonalStore();

    CMinimapDLG* pDlg = g_itMGR.GetMinimapDLG();

    if (pDlg)
        pDlg->SetMinimap(ZONE_MINIMAP_NAME(Reply.m_nZoneNO),
            "3DData\\Control\\Res\\MiniMap_Arrow.tga",
            ZONE_MINIMAP_STARTX(Reply.m_nZoneNO),
            ZONE_MINIMAP_STARTY(Reply.m_nZoneNO));

    ::setDelayedLoad(2);
    ::setDelayedLoad(1);

    return 0;
}