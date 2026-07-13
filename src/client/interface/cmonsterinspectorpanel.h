#ifndef _CMONSTERINSPECTORPANEL_
#define _CMONSTERINSPECTORPANEL_

#include <windows.h>
#include <vector>

class CObjCHAR;

/**
 * "Monster Inspector" — 몬스터 인스펙터 스킬(SKILL_WINDOW_MONSTER_INSPECTOR)로 여는
 * 순수 클라이언트 창. 몬스터의 레벨 / HP / 능력치 / 드랍 아이템 목록과 3D 모델을 보여준다.
 *
 * - CSummonInfoPanel 처럼 tgamectrl 다이얼로그가 아닌 직접 스프라이트/폰트 드로잉
 *   오버레이라 XML 리소스가 필요 없다. CUIMediator::Draw() 안(HUD 스프라이트 블록)에서 그린다.
 * - 능력치는 LIST_NPC.STB, 드랍 목록은 ITEM_DROP.STB(둘 다 클라이언트가 이미 로드)에서
 *   읽는다. HP 는 살아있는 대상 오브젝트에서 실시간으로 읽고, 대상이 죽거나 사라지면
 *   마지막 값으로 얼어붙는다(정적 데이터는 계속 표시).
 * - 3D 모델: 몬스터 모델의 클라이언트 전용 복제(퍼핏)를 엔진의 아바타 셀렉션
 *   뷰포트 파이프라인( setAvatarViewPort / RenderSelectedAvatar / setDefaultViewPort )
 *   으로 패널 안 프리뷰 영역에 직접 렌더링한다. 씬에 삽입하지 않으므로 월드
 *   카메라 이동/회전과 완전히 무관하고, 배경은 패널이 어둡게 칠한다.
 *   ( 스프라이트를 flushSprite 로 먼저 밀어낸 뒤 그 위에 3D 를 그린다 )
 * - 서버와는 어떤 통신도 하지 않는다. 어그로 없음은 구조적으로 보장된다.
 */
class CMonsterInspectorPanel {
public:
    CMonsterInspectorPanel();
    ~CMonsterInspectorPanel();

    /// 클라이언트 오브젝트 인덱스(OBJ_MOB)로 창을 연다. 이미 열려있으면 대상만 교체.
    void Open(int iClientObjIdx);
    void Close();
    bool IsOpen() const { return m_bOpen; }

    /// 매 프레임: 퍼핏 위치 갱신 (씬 업데이트 전/후 어디든 프레임당 1회)
    void Update();

    /// HUD 스프라이트 블록 안에서 호출 (beginSprite/endSprite 사이).
    void Draw();

    /// 입력. 화면 좌표(클라이언트 픽셀). true 반환 = 메시지 소비.
    bool OnLButtonDown(int x, int y);
    bool OnMouseMove(int x, int y); /// 드래그중일 때만 true (hover 추적은 항상)
    bool OnLButtonUp(int x, int y);

    bool IsDragging() const { return m_bDragging; }

private:
    struct DropEntry {
        short nType;   /// 아이템 타입 (1~14)
        short nItemNo; /// 아이템 번호
        int iIconNo;   /// ITEM_ICON_NO
    };

    void EnsureDefaultPosition();
    void GetPanelRect(RECT& rcOut) const;
    void GetCloseRect(RECT& rcOut) const;
    void GetPreviewRect(RECT& rcOut) const;
    void BuildDropList();
    void AppendDropTable(int iDropTBL, std::vector<DropEntry>& out) const;

    /// 살아있는 대상 오브젝트 (없으면 NULL)
    CObjCHAR* GetTargetOBJ() const;

    /// ---- 3D 프리뷰 퍼핏 ----
    bool PuppetCreate(short nCharIdx);
    void PuppetDestroy();
    void PuppetDrawInPane(const RECT& rcPane); /// 스프라이트 flush 후 뷰포트 렌더

    bool m_bOpen;
    int m_iTargetObjIdx;  /// 클라이언트 오브젝트 인덱스 (오브젝트가 사라질 수 있음)
    short m_nCharIdx;     /// LIST_NPC.STB 행 (정적 데이터의 원천)

    /// 대상이 사라져도 표시를 유지하기 위한 마지막 HP 스냅샷
    int m_iLastHP;
    int m_iLastMaxHP;

    std::vector<DropEntry> m_Drops;

    POINT m_ptPos;        /// 패널 좌상단 (화면 픽셀)
    bool m_bPositionInit;
    bool m_bDragging;
    POINT m_ptDragGrab;
    POINT m_ptHover;      /// 마지막 마우스 위치 (드랍 아이콘 툴팁용)

    /// 퍼핏 상태 (엔진 노드 핸들. 0 = 없음)
    unsigned int m_hPuppetModel;               /// HNODE
    unsigned int* m_pPuppetPartVIS[10];        /// MAX_BODY_PART 개의 HNODE 배열
    short m_nPuppetCharIdx;
    float m_fPuppetModelHeight;                /// 스케일 1 기준 모델 높이 (cm)
};

#endif // _CMONSTERINSPECTORPANEL_
