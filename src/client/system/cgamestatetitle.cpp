#include "stdafx.h"

#include "cgamestatetitle.h"

#include "CGame.h"
#include "CCamera.h"
#include "System_FUNC.h"
#include "SystemProcScript.h"
#include "interface/IO_ImageRes.h"

CGameStateTitle::CGameStateTitle(int iID):
    loading_phase(PHASE_NONE), title_texture(NULL), background_zone_id(0) {
    m_iStateID = iID;
}

CGameStateTitle::~CGameStateTitle(void) {}

int
CGameStateTitle::Update(bool bLostFocus) {
    Draw();

    if (this->loading_phase != PHASE_FINISHED) {
        this->advance_loading_phase();
        return 0;
    }

    if (g_GameDATA.auto_connect()) {
        CGame::GetInstance().ChangeState(CGame::GS_AUTOCONNECT);
    } else {
        CGame::GetInstance().ChangeState(CGame::GS_LOGIN);
    }

    return 0;
}

int
CGameStateTitle::Enter(int iPrevStateID) {
    CGame::GetInstance().Load_DataNotUseThread();
    g_pTerrain->SetMapPrefetchEnabled(false);

    this->background_zone_id = SC_GetBGZoneNO();
    this->title_texture = loadTexture("3ddata\\control\\res\\logo.dds", "3ddata\\control\\res\\logo.dds", 1, 0);
    this->loading_phase = PHASE_LOAD_IMAGE_RESOURCES;
    LogString(LOG_NORMAL, "Title loading phase: %s\n", this->get_loading_phase_name());

    return 0;
}

int
CGameStateTitle::Leave(int iNextStateID) {
    if (this->title_texture) {
        unloadTexture(this->title_texture);
        this->title_texture = NULL;
    }
    return 0;
}

void
CGameStateTitle::advance_loading_phase() {
    switch (this->loading_phase) {
        case PHASE_LOAD_IMAGE_RESOURCES:
            ::setDelayedLoad(0);
            CImageResManager::GetSingletonPtr()->LoadImageResources();
            this->loading_phase = PHASE_INIT_INTERFACE;
            break;
        case PHASE_INIT_INTERFACE:
            g_itMGR.Init();
            this->loading_phase = PHASE_LOAD_BASIC_DATA;
            break;
        case PHASE_LOAD_BASIC_DATA:
            CGame::GetInstance().Load_BasicDATA();
            this->loading_phase = PHASE_LOAD_BACKGROUND_ZONE;
            break;
        case PHASE_LOAD_BACKGROUND_ZONE: {
            g_pTerrain->LoadZONE(this->background_zone_id, false);

            D3DVECTOR position;
            position.x = 520000.0f;
            position.y = 520000.0f;
            position.z = 0.0f;

            g_pCamera->Set_Position(position);
            ::setDelayedLoad(2);
            ::setDelayedLoad(0);

            this->loading_phase = PHASE_FINISHED;
            break;
        }
        case PHASE_NONE:
        case PHASE_FINISHED:
        default:
            return;
    }

    LogString(LOG_NORMAL, "Title loading phase: %s\n", this->get_loading_phase_name());
}

const char*
CGameStateTitle::get_loading_phase_name() const {
    switch (this->loading_phase) {
        case PHASE_NONE:
            return "none";
        case PHASE_LOAD_IMAGE_RESOURCES:
            return "load-image-resources";
        case PHASE_INIT_INTERFACE:
            return "init-interface";
        case PHASE_LOAD_BASIC_DATA:
            return "load-basic-data";
        case PHASE_LOAD_BACKGROUND_ZONE:
            return "load-background-zone";
        case PHASE_FINISHED:
            return "finished";
    }
    return "unknown";
}

void
CGameStateTitle::Draw() {
    ::setClearColor(1, 1, 1);

    if (g_pCApp->IsActive()) {
        if (!::beginScene()) {
            return;
        }

        ::clearScreen();

        ::beginSprite(D3DXSPRITE_ALPHABLEND);

        int width, height;

        ::getTextureSize(this->title_texture, width, height);

        const D3DXVECTOR3 center(width / 2.0f, height / 2.0f, 0);
        const float scale_width = g_pCApp->GetWIDTH() / 1024.0f;
        const float scale_height = g_pCApp->GetHEIGHT() / 768.0f;

        D3DXMATRIX mat, mat_scale, mat_trans;
        D3DXMatrixScaling(&mat_scale, scale_width, scale_height, 0.0f);
        D3DXMatrixTranslation(&mat_trans,
            g_pCApp->GetWIDTH() / 2.0f,
            g_pCApp->GetHEIGHT() / 2.0f,
            0);
        D3DXMatrixMultiply(&mat, &mat_scale, &mat_trans);

        ::setTransformSprite(mat);
        ::drawSprite(this->title_texture, nullptr, &center, nullptr, zz_color_white);

        ::endSprite();
        this->render_dev_ui();
        ::endScene();

        ::swapBuffers();
    }
}
