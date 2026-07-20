#include "stdafx.h"

#include "OverlayPanelUtil.h"

#include "..\\Game.h"

#include "CTDrawImpl.h"
#include "IO_ImageRes.h"
#include "tgamectrl/resourcemgr.h"

namespace OverlayPanel {

void
DrawTextAt(int x, int y, int w, int h, DWORD color, DWORD format, int iFont, const char* msg) {
    D3DXMATRIX mat;
    D3DXMatrixTranslation(&mat, (float)x, (float)y, 0.0f);
    ::setTransformSprite(mat);

    RECT rc = {0, 0, w, h};
    ::drawFont(g_GameDATA.m_hFONT[iFont], true, &rc, color, format | DT_SINGLELINE, msg);
}

void
DrawPanelBg(int x, int y, int w, int h, DWORD color) {
    if (w <= 0 || h <= 0)
        return;

    /// ID_BLACK_PANEL 원본은 상단에 밝아지는 그라데이션이 구워져 있다:
    /// - 통째로 늘리면 위쪽에 넓은 밝은 띠가 생기고,
    /// - 얇은 스트립으로 타일링하면 스트립마다 밝은 윗줄이 반복돼 흰 줄이 생긴다.
    /// 그래서 DrawFit 을 쓰지 않고, 스프라이트 아래쪽의 평탄한 소스 영역만 잘라
    /// 한 번에 늘려 그린다( DrawFit 과 동일한 지오메트리 스케일 방식 ).
    CImageRes* pImageRes = CImageResManager::GetSingleton().GetImageRes(IMAGE_RES_UI);
    if (pImageRes == NULL)
        return;

    int iBlackPanel = CResourceMgr::GetInstance()->GetImageNID(IMAGE_RES_UI, "ID_BLACK_PANEL");
    stTexture* pTextureInfo = pImageRes->GetTexture(iBlackPanel);
    stSprite* pSpriteInfo = pImageRes->GetSprite(iBlackPanel);
    if (pTextureInfo == NULL || pSpriteInfo == NULL || pTextureInfo->m_Texture == NULL)
        return;

    RECT rcSrc = pSpriteInfo->m_Rect;
    int iSrcH = rcSrc.bottom - rcSrc.top;
    int iKeepH = iSrcH / 4;
    if (iKeepH < 2)
        iKeepH = 2;
    rcSrc.top = rcSrc.bottom - iKeepH - 1;
    rcSrc.bottom -= 1; /// 확대 필터링시 경계 텍셀 블리딩 방지

    float fScaleWidth = (float)w / (rcSrc.right - rcSrc.left);
    float fScaleHeight = (float)h / (rcSrc.bottom - rcSrc.top);

    D3DXMATRIX mat;
    D3DXVECTOR2 scale(fScaleWidth, fScaleHeight);
    D3DXVECTOR2 pos((float)x, (float)y);
    D3DXMatrixTransformation2D(&mat, NULL, NULL, &scale, NULL, NULL, &pos);
    ::setTransformSprite(mat);

    ::drawSprite(pTextureInfo->m_Texture, &rcSrc, NULL, &D3DXVECTOR3(0, 0, 0), color);
}

/// 아바타 셀렉션 카메라 싱글턴 상태의 미러( 기본값 2.15 / -0.10 ).
/// 이 두 함수만 카메라를 만지므로 미러는 항상 실제 값과 일치한다.
static float s_fCurLength = 2.15f;
static float s_fCurHeight = -0.10f;

void
PaneCameraSetFrame(float fViewLength, float fViewHeight) {
    ::updateAvatarSelectionCameraLength(fViewLength - s_fCurLength);
    s_fCurLength = fViewLength;
    ::updateAvatarSelectionCameraHeight(fViewHeight - s_fCurHeight);
    s_fCurHeight = fViewHeight;
}

void
PaneCameraSpin(float fDeltaSeta) {
    ::updateAvatarSelectionCameraSeta(fDeltaSeta);
}

} // namespace OverlayPanel
