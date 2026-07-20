#ifndef _OVERLAYPANELUTIL_H_
#define _OVERLAYPANELUTIL_H_

#include <windows.h>

/**
 * 직접-드로잉 오버레이 패널( 몬스터 인스펙터 / 아이템 미리보기 )의 공용 헬퍼.
 *
 * - DrawTextAt / DrawPanelBg : 스프라이트 블록 안 절대좌표 텍스트/배경 드로잉.
 * - PaneCamera* : 엔진 아바타 셀렉션 카메라는 증감( update* ) API 만 있어서
 *   절대값을 지정하려면 현재값 미러가 필요하다. 미러는 싱글턴 상태의 그림자라
 *   반드시 프로세스에 하나만 있어야 한다 — 패널마다 정적 미러를 두면 두 패널이
 *   동시에 열렸을 때 서로의 변경을 몰라 프레이밍이 깨진다. 여기 한 곳에서만 유지.
 */
namespace OverlayPanel {

/// 화면 스프라이트 블록 안에서 절대 좌표에 텍스트를 그린다.
void DrawTextAt(int x, int y, int w, int h, DWORD color, DWORD format, int iFont, const char* msg);

/// ID_BLACK_PANEL 하단의 평탄한 소스 영역만 잘라 늘려 그리는 균일 배경.
/// ( 원본 스프라이트 상단의 그라데이션 띠/타일링 흰줄 문제 회피 )
void DrawPanelBg(int x, int y, int w, int h, DWORD color);

/// 아바타 셀렉션 카메라의 직교 뷰 절대값 지정( 내부 단위 m ).
void PaneCameraSetFrame(float fViewLength, float fViewHeight);

/// 궤도 회전( 라디안 증가 — 모델이 도는 것처럼 보인다 ).
void PaneCameraSpin(float fDeltaSeta);

} // namespace OverlayPanel

#endif // _OVERLAYPANELUTIL_H_
