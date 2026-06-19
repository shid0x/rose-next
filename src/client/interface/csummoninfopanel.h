#ifndef _CSUMMONINFOPANEL_
#define _CSUMMONINFOPANEL_

#include <windows.h>

/**
 * 플레이어가 소환한 몹들의 정보(이름 / 능력치 / HP)를 화면 우측에 표시하는 패널.
 *
 * - tgamectrl 다이얼로그가 아닌, 몬스터 HP 표시처럼 직접 스프라이트/폰트로 그리는
 *   독립형 오버레이라 별도 XML 리소스가 필요 없다.
 * - 소환몹이 하나도 없으면 아무것도 그리지 않는다.
 * - 여러 소환몹은 세로로 쌓아서 표시한다.
 * - 좌클릭 드래그로 위치를 옮길 수 있다(위치는 세션 단위로만 유지, 저장하지 않음).
 */
class CSummonInfoPanel {
public:
    CSummonInfoPanel();
    ~CSummonInfoPanel();

    /// HUD 스프라이트 블록 안에서 호출 (beginSprite/endSprite 사이).
    void Draw();

    /// 드래그 입력 처리. 화면 좌표(클라이언트 픽셀)를 받는다.
    /// 패널을 실제로 잡았/움직였을 때 true 를 반환 -> 호출측에서 메시지를 소비한다.
    bool OnLButtonDown(int x, int y);
    bool OnMouseMove(int x, int y);
    bool OnLButtonUp(int x, int y);

    bool IsDragging() const { return m_bDragging; }

private:
    /// 현재 소환몹 개수에 맞춰 패널 전체 영역을 계산한다.
    int GetSummonCount() const;
    bool GetStackRect(RECT& rcOut) const;
    void EnsureDefaultPosition();

    POINT m_ptPos;        /// 스택의 좌상단 위치(화면 픽셀)
    bool m_bPositionInit; /// 기본 위치(우측) 초기화 여부
    bool m_bDragging;     /// 드래그 중 여부
    POINT m_ptDragGrab;   /// 드래그 시작 시 마우스-패널 좌상단 오프셋
};

#endif // _CSUMMONINFOPANEL_
