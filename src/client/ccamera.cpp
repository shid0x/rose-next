#include "stdafx.h"

#include "ccamera.h"
#include "game.h"

CCamera* CCamera::m_pInstance = NULL;

constexpr float CAMERA_MIN_ZOOM = Rose::GameStaticConfig::CAMERA_MIN_ZOOM;
#ifndef _DEBUG
constexpr float CAMERA_MAX_ZOOM = Rose::GameStaticConfig::CAMERA_MAX_ZOOM;
#else
constexpr float CAMERA_MAX_ZOOM = Rose::GameStaticConfig::CAMERA_MAX_ZOOM * 100.0f;
#endif

CCamera::CCamera(): m_hNODE(NULL), m_hMotion(NULL) {
}

CCamera::~CCamera() {}

CCamera*
CCamera::Instance() {
    if (!m_pInstance) {
        m_pInstance = new CCamera;
    }

    return m_pInstance;
}

void
CCamera::Destroy() {
    if (m_hNODE != NULL) {
        ::unloadCamera(m_hNODE);
        m_hNODE = NULL;
    }

    if (m_hMotion != NULL) {
        ::unloadMotion(m_hMotion);
        m_hMotion = NULL;
    }

    SAFE_DELETE(m_pInstance);
}

void
CCamera::Init(HNODE hNODE) {
    m_hNODE = hNODE;

    setCameraDefault(hNODE);

    m_Angle.x = 25.f;
    m_Angle.y = 180.f;
    m_Angle.z = 0.f;

    m_bFollowMode = false; // 1 : 뒤에서 따라가는 모드, 0 : 3인칭 모드

    if (m_hMotion != NULL) {
        ::unloadMotion(m_hMotion);
        m_hMotion = NULL;
    }
}

void
CCamera::SetSightInfo(int iInfoIdx) {}

void
CCamera::ResetToAvatarCamera() {
    if (m_hNODE != NULL) {
        HNODE hMotion = findNode("SelectAvatarCameraMotion");
        if (hMotion) {
            attachMotion(m_hNODE, 0);
            unloadMotion(hMotion);
        }

        ::unloadCamera(m_hNODE);
        m_hNODE = NULL;
    }

    m_hNODE = ::findNode("avatar_camera");

    if (m_hNODE == NULL) {
        LogString(LOG_DEBUG_, "Load camera failed[ cameras/camera01.zca ]");
        return;
    }

    setCameraDefault(m_hNODE);

    ::setPosition(m_hNODE, m_PosAT.x, m_PosAT.y, m_PosAT.z);
}

/// set motion to camera

void
CCamera::SetMotion(const char* strMotion) {
    if (m_hNODE == NULL)
        return;

    if (m_hMotion != NULL) {
        ::unloadMotion(m_hMotion);
        m_hMotion = NULL;
    }

    m_hMotion = ::findNode("CameraMotion");
    if (m_hMotion != NULL) {
        ::unloadMotion(m_hMotion);
        m_hMotion = NULL;
    }

    int ZZ_INTERP_SQUAD = 3, ZZ_INTERP_CATMULLROM = 4;
    m_hMotion =
        ::loadMotion("CameraMotion", strMotion, 1, ZZ_INTERP_CATMULLROM, ZZ_INTERP_SQUAD, 1, 1);

    if (m_hMotion == NULL) {
        LogString(LOG_DEBUG_, "Camera motion loading failed[ %s ]", strMotion);
        return;
    }

    ::attachMotion(m_hNODE, m_hMotion);

    ::controlAnimatable(m_hNODE, 1);
}

bool
CCamera::Toggle_FloowMODE() {
    m_bFollowMode = !m_bFollowMode;

    ::setCameraFollowMode(m_hNODE, m_bFollowMode);

    return m_bFollowMode;
}

void
CCamera::Add_PITCH(short nMovement) {
    float p = pitch() + ((float)nMovement / g_pCApp->GetHEIGHT());

    if (p < 0.2) {
        p = 0.2f;
    } else if (p > 1.0) {
        p = 1.0f;
    }

    setCameraFollowPitch(m_hNODE, p);
}

void
CCamera::Add_YAW(short nMovement) {
    const float fMaxAngle = 180.f;

    float yaw = getCameraFollowYaw(m_hNODE);
    yaw = yaw - 480.f * nMovement / g_pCApp->GetWIDTH();

    setCameraFollowYaw(m_hNODE, yaw);
}

void
CCamera::Add_Distance(float fDistance) {
    float d = distance() + fDistance;
    if (d < CAMERA_MIN_ZOOM) {
        d = CAMERA_MIN_ZOOM;
    }
    if (d > CAMERA_MAX_ZOOM) {
        d = CAMERA_MAX_ZOOM;
    }

    ::setCameraFollowDistance(m_hNODE, d);
}

void
CCamera::Attach(HNODE hModel) {
    ::cameraAttachTarget(m_hNODE, hModel);
    ::setCameraTargetHeight(m_hNODE, 170);
    ::setCameraFollowDistance(m_hNODE, 1000.0f);
    ::setCameraFollowYaw(m_hNODE, 0.0f);
    ::setCameraFollowPitch(m_hNODE, 0.5f);
    ::setCameraFollowMode(m_hNODE, m_bFollowMode);

    ::setCameraFollowDistanceRange(m_hNODE, CAMERA_MIN_ZOOM, CAMERA_MAX_ZOOM * 2.0f);
}

//#ifdef	_DEBUG
D3DVECTOR
CCamera::Get_Position(void) {
    ::getCameraEye(m_hNODE, (float*)&m_PosAT);

    return m_PosAT;
}
//#endif
//-------------------------------------------------------------------------------------------------
void
CCamera::Set_Position(float fX, float fY, float fZ) {
    m_PosAT.x = fX;
    m_PosAT.y = fY;
    m_PosAT.z = fZ;

    ::setPosition(m_hNODE, fX, fY, fZ);
}

void
CCamera::LookAt(float eyeX,
    float eyeY,
    float eyeZ,
    float centerX,
    float centerY,
    float centerZ,
    float upX,
    float upY,
    float upZ) {
    ::lookAt(m_hNODE, eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ);
}

void
CCamera::set_follow_mode(bool b) {
    this->m_bFollowMode = b;
    ::setCameraFollowMode(this->m_hNODE, this->m_bFollowMode);
}

float
CCamera::distance() {
    return ::getCameraFollowDistance(this->m_hNODE);
}

void
CCamera::set_distance(float d) {
    if (d < CAMERA_MIN_ZOOM) {
        d = CAMERA_MIN_ZOOM;
    }
    if (d > CAMERA_MAX_ZOOM) {
        d = CAMERA_MAX_ZOOM;
    }

    ::setCameraFollowDistance(this->m_hNODE, d);
}

float
CCamera::yaw() {
    return ::getCameraFollowYaw(this->m_hNODE);
}

void
CCamera::set_yaw(float yaw) {
    // Engine will automatically wrap values to (-180, 180) range
    ::setCameraFollowYaw(this->m_hNODE, yaw);
}

float
CCamera::pitch() {
    return ::getCameraFollowPitch(this->m_hNODE);
}

void
CCamera::set_pitch(float pitch) {
    pitch = std::clamp(pitch, 0.0f, 1.0f);
    ::setCameraFollowPitch(this->m_hNODE, pitch);
}