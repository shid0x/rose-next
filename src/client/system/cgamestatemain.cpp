#include "stdafx.h"

#include "system/cgame.h"
#include "system/cgamestatemain.h"

#include "util/clipboardutil.h"

#include "bullet.h"
#include "BoneEffectBudget.h"
#include "ccamera.h"
#include "cclientstorage.h"
#include "cskydome.h"
#include "cviewmsg.h"
#include "jcommandstate.h"
#include "object.h"
#include "systemprocscript.h"
#include "network/cnetwork.h"

#include "game.h"
#include "gamecommon/skill.h"
#include "gamecommon/item.h"
#include "gamedata/cclan.h"
#include "gamedata/cparty.h"
#include "gameproc/cdaynnightproc.h"
#include "gameproc/delayedexp.h"
#include "gameproc/livecheck.h"
#include "gameproc/preventduplicatedcommand.h"
#include "gameproc/skillcommanddelay.h"
#include "gameproc/targetmanager.h"
#include "gameproc/useitemdelay.h"

#include "interface/CHelpMgr.h"
#include "interface/CTFontImpl.h"
#include "interface/CUIMediator.h"
#include "interface/ClanMarkManager.h"
#include "interface/Dlgs/ChattingDlg.h"
#include "interface/ExternalUI/ExternalUILobby.h"
#include "interface/controls/EffectString.h"
#include "interface/cursor/CCursor.h"

#include "sfx/isfx.h"
#include "sfx/sfxFont.h"
#include "sfx/sfxManager.h"
#include "tgamectrl/tcontrolmgr.h"
#include "tgamectrl/teditbox.h"
#include "tgamectrl/time2.h"

#include "tutorial/tutorialeventmanager.h"

#include <crtdbg.h>

#define CAMERA_MOVE_SPEED 10

static CEffect* s_pEF = NULL;
static short s_nEffectIDX = 29;
// static tPOINT16 s_PosClick;

#define SCREEN_LEFT 0x0001
#define SCREEN_RIGHT 0x0002
#define SCREEN_UP 0x0004
#define SCREEN_DOWN 0x0008
#define MOUSE_STOP_CHECK_FRAME 5

const int SKILLINDEX_JUMP = 13;
const int SKILLINDEX_PICKUP = 12;
const int SKILLINDEX_SIT = 11;

CGameStateMain::CGameStateMain(int iID) {
    m_iStateID = iID;

    m_bPickedPOS = false;
    m_iPickedOBJ = 0;

    CGame::GetInstance().ResetCheckFrame();
    //    CGame::GetInstance().m_bUseUserMouse = true;
}

CGameStateMain::~CGameStateMain(void) {}

int
CGameStateMain::Update(bool bLostFocus) {

    if (g_pCApp->IsActive()) {
        CD3DSOUND::UpdateListener(g_pCamera);
    }

#ifdef __VIRTUAL_SERVER
    g_pTerrain->Proc_RegenAREA();
#endif
    g_pEffectLIST->Proc();
    g_pBltMGR->ProcBULLET();

    g_pCamera->Update();

    g_UIMed.Update();
    g_DayNNightProc.Proc();
    g_UseItemDelay.Proc();
    g_UseSkillDelay.Proc();
    g_SoloSkillDelayTick.Proc();
    g_SoloUseItemDelayTick.Proc();

    CParty::GetInstance().Update();
    CSkillCommandDelay::GetSingleton().Proc();
    // processing  ...

    /// SFX
    CSFXManager::GetSingleton().Update();

    /// Tutorial event check..
    CTutorialEventManager::GetSingleton().Proc();

    if (g_ClientStorage.m_VideoOption.background_render || !bLostFocus) {
        UpdateCameraPositionByMouse();
        UpdateCheckFrame();
        CTargetManager::GetSingleton().Proc();
    }

    CLiveCheck::GetSingleton().Check();
    CDelayedExp::GetSingleton().Proc();
    CPreventDuplicatedCommand::GetSingleton().Proc();

    CClanMarkManager::GetSingleton().UpdatePool();

    ::updateSceneTransform(); // �̵� �ִϸ��̼� ó��...
    ::updateSceneEx(); // ���ϸ��̼� ó��...

    g_pObjMGR->ProcOBJECT();
    D3DVECTOR vPos = g_pAVATAR->GetWorldPos();
    g_pTerrain->SetCenterPosition(vPos.x, vPos.y);

    ::updateSceneExAfter();

    CBoneEffectBudget::Instance().Update();

    //�� ��ȣ : ���ż�ȯ ������Ʈ
    goddessMgr.Update();

    if (g_ClientStorage.m_VideoOption.background_render || !bLostFocus) {
        /*		UpdateCameraPositionByMouse();
                UpdateCheckFrame();
                CTargetManager::GetSingleton().Proc();
                CSkillCommandDelay::GetSingleton().Proc();   */

        if (::beginScene()) {
            ::clearScreen();
            ::renderScene();

            if (!g_GameDATA.m_bNoUI) {
                Render_GameMENU();
            }

            this->render_dev_ui();

            ::endScene();
            ::swapBuffers();
        }
    } else {
        Sleep(30);
    }

    g_pObjMGR->ClearViewObjectList();

    return 0;
}

int
CGameStateMain::Enter(int iPrevStateID) {
    ::SetOceanSFXOnOff(true);
    g_pTerrain->SetMapPrefetchEnabled(true);

    CGame::GetInstance().ClearWndMsgQ();
    g_pNet->Send_cli_JOIN_ZONE(g_pAVATAR->GetWeightRate());

    ///���� ��Ŷ �����ִ�.
    g_HelpMgr.Update();

    g_itMGR.ChangeState(IT_MGR::STATE_NORMAL);
    if (CGame::REPAIR_NONE != CGame::GetInstance().GetRepairMode())
        CGame::GetInstance().ResetRepairMode();

    if (ISFX* p = CSFXManager::GetSingleton().FindSFXWithType(SFX_FONT)) {
        CSFXFont* sfx_font = (CSFXFont*)p;

        POINT draw_position;
        int draw_width;

        /// Zone Name
        SIZE size = getFontTextExtent(g_GameDATA.m_hFONT[FONT_OUTLINE_18_BOLD],
            ZONE_NAME(g_pTerrain->GetZoneNO()));
        draw_width = size.cx;
        draw_position.x = g_pCApp->GetWIDTH() / 2 - size.cx / 2;
        draw_position.y = 150; ///�ϴ� ����

        CEffectString* child = new CEffectString;
        child->SetType(CSFXFont::TYPE_ZONE_NAME);
        child->SetString(FONT_OUTLINE_18_BOLD,
            (char*)ZONE_NAME(g_pTerrain->GetZoneNO()),
            draw_position,
            draw_width,
            6 * 1000);
        sfx_font->AddEffectString(child);

        /// Zone Description
        draw_width = 300;
        draw_position.x = g_pCApp->GetWIDTH() / 2 - 300 / 2;
        draw_position.y = 250;
        child = new CEffectString;
        child->SetType(CSFXFont::TYPE_ZONE_DESC);
        child->SetString(FONT_OUTLINE_14_BOLD,
            (char*)ZONE_DESC(g_pTerrain->GetZoneNO()),
            draw_position,
            draw_width,
            6 * 1000);
        sfx_font->AddEffectString(child);
    }

    if (g_pTerrain->is_clan_zone())
        g_itMGR.CloseDialog(DLG_TYPE_MINIMAP);
    else
        g_itMGR.OpenDialog(DLG_TYPE_MINIMAP, false);

#ifdef DISCORD
    g_pCApp->update_discord_status(g_pAVATAR);
#endif

    return 0;
}

int
CGameStateMain::Leave(int iNextStateID) {
    g_pTerrain->SetMapPrefetchEnabled(false);

    g_pCamera->Detach();

    ::SetOceanSFXOnOff(false);

    if (ISFX* p = CSFXManager::GetSingleton().FindSFXWithType(SFX_FONT)) {
        CSFXFont* sfx_font = (CSFXFont*)p;
        sfx_font->RemoveEffectStringsByType(CSFXFont::TYPE_ZONE_NAME);
        sfx_font->RemoveEffectStringsByType(CSFXFont::TYPE_ZONE_DESC);
    }

#ifdef DISCORD
    g_pCApp->update_discord_status(nullptr);
#endif

    return 0;
}

void
CGameStateMain::ServerDisconnected() {
    g_itMGR.ServerDisconnected();
}

#include "../GameProc/UseItemDelay.h"

void
CGameStateMain::Render_GameMENU() {

    ::beginSprite(D3DXSPRITE_ALPHABLEND);

    g_pViewMSG->Draw();
    /// Screen message display
    g_UIMed.Draw();

    ::endSprite();

    ::drawSpriteSFX();

    ::beginSprite(D3DXSPRITE_ALPHABLEND);

    g_itMGR.Update();

    /// UI display
    //	g_pViewMSG->Draw ();

    CTargetManager::GetSingleton().Draw();

    CTIme::GetInstance().Draw();

    /// SFX
    CSFXManager::GetSingleton().Draw();
    // CTutorialEventManager::GetSingleton().Draw();
    ::endSprite();

    if (g_GameDATA.m_bDisplayDebugInfo) {
        /// Debug HUD layout: single yellow column well clear of the
        /// top-left UI (char panel ends near x=230). 16 px row stride.
        /// Korean strings from the original client were removed because
        /// drawFontf does not handle CP949 in this build — only English.
        const int kDebugX = 450;
        const int kDebugRowStride = 16;
        int nRowY = 15;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "%d FPS, Patch[ %d, %d ], Pos[ %.1f, %.1f, %.1f ]",
            g_pCApp->DisplayFrameRate(),
            g_GameDATA.m_PosPATCH.x,
            g_GameDATA.m_PosPATCH.y,
            g_pAVATAR->Get_CurPOS().x,
            g_pAVATAR->Get_CurPOS().y,
            g_pAVATAR->Get_CurPOS().z);
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "TotOBJ:%d Cnst:%d Item:%d Tree:%d Mob:%d Fx:%d Patch:%d",
            g_pObjMGR->Get_ObjectCount(),
            g_pObjMGR->Get_ObjectCount(OBJ_CNST),
            g_pObjMGR->Get_ObjectCount(OBJ_ITEM),
            g_pObjMGR->Get_ObjectCount(OBJ_GROUND),
            g_pObjMGR->Get_ObjectCount(OBJ_MOB),
            g_pEffectLIST->GetCount(),
            CTERRAIN::m_RegistedPatchCnt);
        nRowY += kDebugRowStride;

        /// Terrain streaming pipeline depth. loads = maps queued for disk
        /// load, unload = maps deferred pending hysteresis, dirty = freed
        /// CMAPs waiting recycle, prefetch = depth of the OS-page-cache
        /// prefetcher thread. All four should return to 0 shortly after
        /// stopping movement; a non-zero steady-state means streaming is
        /// still catching up and you are paying InsertToScene bursts.
        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "Stream: loads=%u unload=%u dirty=%u prefetch=%u cold22=%u",
            g_pTerrain->GetQueuedMapLoadCount(),
            g_pTerrain->GetPendingUnloadCount(),
            g_pTerrain->GetDirtyMapCount(),
            g_pTerrain->GetPrefetchQueueDepth(),
            g_pTerrain->GetProximityColdCount());
        nRowY += kDebugRowStride;

        /// Per-frame scene-toggle activity. ins/rem = CMAP_PATCH
        /// InsertToScene/RemoveFromScene calls this frame; resPatch =
        /// patches currently held in-scene (grace + proximity + frustum).
        /// A sustained non-zero ins on a stable camera means terrain is
        /// thrashing — correlate with camera angle to validate the
        /// frustum-rotation hypothesis.
        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "Scene: ins=%u rem=%u ring=%u sub=%u resPatch=%u drawPatch=%d",
            CMAP_PATCH::s_nInsertThisFrame,
            CMAP_PATCH::s_nRemoveThisFrame,
            CPatchManager::s_nRingStampsThisFrame,
            g_pTerrain->m_PatchManager.GetFrustumPatchCount(),
            g_pTerrain->m_PatchManager.GetResidentPatchCount(),
            g_pTerrain->m_PatchManager.m_nDrawingPatch);
        nRowY += kDebugRowStride;

        const BoneEffectBudgetStats& boneFx = CBoneEffectBudget::Instance().GetStats();
        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "BoneFx: groups=%d effects=%d emitters=%d active=%d cap=%d emit=%.0f tiers F/R/M/O=%d/%d/%d/%d topNpc=%d",
            boneFx.groups,
            boneFx.effects,
            boneFx.emitters,
            boneFx.activeParticles,
            boneFx.runtimeCapacity,
            boneFx.configuredEmitRate,
            boneFx.tierCounts[PARTICLE_EFFECT_TIER_FULL],
            boneFx.tierCounts[PARTICLE_EFFECT_TIER_REDUCED],
            boneFx.tierCounts[PARTICLE_EFFECT_TIER_MINIMAL],
            boneFx.tierCounts[PARTICLE_EFFECT_TIER_OFF],
            boneFx.topNpc);
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "PartBatch: groups=%d particles=%d draws=%d fallback=%d saved=%d",
            ::getParticleBatchGroups(),
            ::getParticleBatchParticles(),
            ::getParticleBatchDrawCalls(),
            ::getParticleBatchFallback(),
            ::getParticleBatchSavedDrawCalls());
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "AtkSpd:%d MoveSpd:%.1f BaseSpd:%.1f",
            g_pAVATAR->stats.attack_speed,
            (g_pAVATAR->GetPetMode() < 0) ? g_pAVATAR->adjusted_move_speed
                                          : g_pAVATAR->m_pObjCART->adjusted_move_speed,
            (g_pAVATAR->GetPetMode() < 0) ? g_pAVATAR->stats.move_speed
                                          : g_pAVATAR->m_pObjCART->stats.move_speed);
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "WorldTime:%d ZoneTime:%d Blend:%.2f",
            g_DayNNightProc.GetWorldTime(),
            g_DayNNightProc.GetZoneTime(),
            g_DayNNightProc.GetBlendFactor());
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "WorldRATE:%d WorldPROD:%d CountryCode:%d",
            Get_WorldRATE(),
            Get_WorldPROD(),
            0);
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "bCastingStart:%d DoingSkill:%d Command:%d",
            g_pAVATAR->m_bCastingSTART,
            g_pAVATAR->m_nDoingSkillIDX,
            g_pAVATAR->Get_COMMAND());
        nRowY += kDebugRowStride;

        if (g_pAVATAR->GetCur_SummonCNT() > 0) {
            ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
                false,
                kDebugX,
                nRowY,
                g_dwYELLOW,
                "Summons: count=%d used=%d max=%d",
                g_pAVATAR->GetCur_SummonCNT(),
                g_pAVATAR->GetCur_SummonUsedCapacity(),
                g_pAVATAR->GetCur_SummonMaxCapacity());
            nRowY += kDebugRowStride;
        }

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "Stamina:%d SkillDelay:%d",
            g_pAVATAR->m_GrowAbility.m_nSTAMINA,
            CSkillCommandDelay::GetSingleton().GetSkillCommandDelayProgressRatio());
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "UseItemDelay  HP:%d MP:%d Other:%d",
            g_UseItemDelay.GetUseItemDelay(USE_ITEM_HP),
            g_UseItemDelay.GetUseItemDelay(USE_ITEM_MP),
            g_UseItemDelay.GetUseItemDelay(USE_ITEM_OTHERS));
    }
}

int
CGameStateMain::ProcMouseInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    CGame& refGame = CGame::GetInstance();

    POINT ptMouse = {LOWORD(lParam), HIWORD(lParam)};
    ;

    if (!g_GameDATA.m_bNoUI && g_itMGR.MsgProc(uiMsg, wParam, lParam))
        return 1;

    m_ScreenPOS.m_nX = (short)ptMouse.x;
    m_ScreenPOS.m_nY = (short)ptMouse.y;

    switch (uiMsg) {
        case WM_LBUTTONDOWN: {
            // set screen mouse position & world position
            Pick_POSITION();
            On_WM_LBUTTONDOWN(wParam, lParam);
            break;
        }
            // Dagnarus
        case WM_RBUTTONDOWN: {
            On_WM_RBUTTONDOWN(wParam, lParam);
        } break;

        case WM_MOUSEWHEEL:
            On_WM_MOUSEWHEEL(wParam, lParam);
            break;

        case WM_LBUTTONDBLCLK:
            On_WM_LBUTTONDBCLICK(wParam, lParam);
            break;

        default:
            break;
    }

    return true;
}

#include "System/System_FUNC.h"
int
CGameStateMain::ProcKeyboardInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    int iRet = 0;

    switch (uiMsg) {
        case WM_KEYUP: {
            unsigned int oemScan = int(lParam & (0xff << 16)) >> 16;
            UINT vk = MapVirtualKey(oemScan, 1);
            switch (vk) {
                case VK_CONTROL:
                    g_GameDATA.m_bShowDropItemInfo = false;
                    break;

                case 0x5A: // z : �ɱ�/���� ���
                    if (CTControlMgr::GetInstance()->GetKeyboardInputType()
                            == CTControlMgr::INPUTTYPE_NORMAL
                        && NULL == CTEditBox::s_pFocusEdit) {
                        assert(g_pAVATAR);
                        CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
                        assert(pSkillSlot);

                        CSkill* pSkill = pSkillSlot->GetSkillBySkillIDX(SKILLINDEX_SIT);
                        assert(pSkill);
                        if (pSkill)
                            pSkill->Execute();
                    }
                    break;
                case VK_SPACE:
                    if (CTControlMgr::GetInstance()->GetKeyboardInputType()
                            == CTControlMgr::INPUTTYPE_NORMAL
                        && NULL == CTEditBox::s_pFocusEdit) {
                        assert(g_pAVATAR);
                        CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
                        assert(pSkillSlot);

                        CSkill* pSkill = pSkillSlot->GetSkillBySkillIDX(SKILLINDEX_JUMP);
                        if (pSkill)
                            pSkill->Execute();
                    }
                    break;
                case 192: //` : �ݱ�
                    break;
                case VK_SHIFT: //
                    if (CTControlMgr::GetInstance()->GetKeyboardInputType()
                            == CTControlMgr::INPUTTYPE_NORMAL
                        && NULL == CTEditBox::s_pFocusEdit) {
                        g_UserInputSystem.SetTargetSelf();
                    }
                default:
                    break;
            }
        } break;

        case WM_SYSKEYDOWN: {
            unsigned int oemScan = int(lParam & (0xff << 16)) >> 16;
            UINT vk = MapVirtualKey(oemScan, 1);
            if (GetAsyncKeyState(VK_SHIFT) < 0) {
                switch (vk) {
                    // case 0x4B://K
                    //	{
                    //		if( CTControlMgr::GetInstance()->GetKeyboardInputType() ==
                    // CTControlMgr::INPUTTYPE_AUTOENTER )
                    //		{
                    //			it_SetKeyboardInputType( CTControlMgr::INPUTTYPE_NORMAL );
                    //		}
                    //		else
                    //		{
                    //			it_SetKeyboardInputType( CTControlMgr::INPUTTYPE_AUTOENTER );
                    //			if( CTDialog* pDlg = g_itMGR.FindDlg( DLG_TYPE_CHAT ) )
                    //				pDlg->Show();
                    //		}
                    //	}
                    //	break;
                    case 0x52: /// R
                        if (CGame::GetInstance().GetRight() >= CHEAT_GM)
                            CGame::GetInstance().ToggleAutoRun();

                        break;
                    default:
                        break;
                }
            }
        } break;

        default:
            break;
    }

    if (g_itMGR.MsgProc(uiMsg, wParam, lParam))
        return 1;

    switch (uiMsg) {

        case WM_CHAR:
            return On_WM_CHAR(wParam, lParam);

        case WM_KEYDOWN:
            return On_WM_KEYDOWN(wParam, lParam);

        case WM_SYSKEYDOWN: {
            unsigned int oemScan = int(lParam & (0xff << 16)) >> 16;
            UINT vk = MapVirtualKey(oemScan, 1);

            switch (vk) {
                ///'2'
                case 0x32:
                    //{
                    // CGame::GetInstance().ToggleUserMouseFlag();
                    // CGame::GetInstance().SetUserCursor( CGame::CURSOR_DEFAULT );
                    //	CCursor::GetInstance().ChangeNextState();
                    //}
                    /// ScreenFadeInStart( 25, 0, 0, 0, 0, 0 );
                    break;

                /// �Կ���� ��ȯ..
                case 0x35: // 5
                {
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
                        g_GameDATA.m_bFilmingMode = !g_GameDATA.m_bFilmingMode;
                        if (g_GameDATA.m_bFilmingMode)
                            ::setVisibilityRecursive(g_pAVATAR->GetZMODEL(), 0.0f);
                        else
                            ::setVisibilityRecursive(g_pAVATAR->GetZMODEL(), 1.0f);
                    }
                } break;

                /// ���콺 Ŀ�� �����
                case 0x36: // 6
                {
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
                        g_GameDATA.m_bShowCursor = !g_GameDATA.m_bShowCursor;
                    }
                } break;

                /// 'K'
                case 0x4B: {
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
                        g_DayNNightProc.SetWorldTime(g_pTerrain->GetZoneNO(),
                            g_DayNNightProc.GetWorldTime() + 10);
                    }
                } break;

                /// '9'
                case 0x39: {
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
                        int iClientObjectIndex = g_UserInputSystem.GetCurrentTarget();
                        CObjCHAR* pChar = g_pObjMGR->Get_ClientCharOBJ(
                            g_pObjMGR->Get_ServerObjectIndex(iClientObjectIndex),
                            false);
                        if (pChar) {
                            switch (pChar->Get_TYPE()) {
                                case OBJ_MOB:
                                    g_pNet->Send_cli_CHAT("/get tg");
                                    break;
                                case OBJ_AVATAR: {
                                    std::string cheat = "/get hp ";
                                    cheat.append(pChar->Get_NAME());
                                    g_pNet->Send_cli_CHAT((char*)cheat.c_str());
                                } break;
                                case OBJ_USER:
                                    g_pNet->Send_cli_CHAT("/get hp");
                                    break;
                                default:
                                    break;
                            }
                        } else {
                            g_pNet->Send_cli_CHAT("/get hp");
                        }
                    }
                } break;

                /// 'j'
                case 0x4A:
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
#ifdef _DEBUG
                        g_GameDATA.m_bShowCurPos = !g_GameDATA.m_bShowCurPos;
#endif
                    }
                    break;

                /// 'H'
                // case 0x48:
                //	if( CGame::GetInstance().GetRight() )
                //	{
                //		g_GameDATA.m_bNoUI = !g_GameDATA.m_bNoUI;
                //	}
                //	break;

                /// 'D'
                case 0x44:
                    /*char* d_right;
                    sprintf( d_right, "Right: %i", CGame::GetInstance().GetRight() );
                    ::OutputDebugString(d_right);*/
                    if (CGame::GetInstance().GetRight()) {
                        g_GameDATA.m_bDisplayDebugInfo = !g_GameDATA.m_bDisplayDebugInfo;
                    }
                    break;
                case 0x31: {
#ifdef _DEBUG
                    if (CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_CHAT)) {
                        CChatDLG* pChatDlg = (CChatDLG*)pDlg;
                        pChatDlg->SendChatMsgRepeat();
                    }
#else

                    if (CGame::GetInstance().GetRight() >= CHEAT_DEV) {
                        if (CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_CHAT)) {
                            CChatDLG* pChatDlg = (CChatDLG*)pDlg;
                            pChatDlg->SendChatMsgRepeat();
                        }
                    }

#endif
                    break;
                }

#if defined(_DEBUG) || defined(_D_RELEASE)
                    /// '7'
                case 0x37:
                    /// g_ClientStorage.SetUseRoughMap( !g_ClientStorage.GetUseRoughMap() );
                    {
                        int iFace = g_pAVATAR->GetCharExpression();
                        iFace++;
                        if (iFace > 6)
                            iFace = 0;

                        g_pAVATAR->SetCharExpression(iFace);
                        g_pAVATAR->Update();
                    }
                    break;

                ///'8'
                case 0x38: {
                    if (!s_pEF) {
                        s_pEF = g_pEffectLIST->Add_EffectWithIDX(s_nEffectIDX);
                        if (s_pEF) {
                            s_pEF->LinkNODE(g_pAVATAR->GetZMODEL());
                            s_pEF->InsertToScene();
                        }

                        s_nEffectIDX = (s_nEffectIDX + 1) % g_pEffectLIST->GetFileCNT();
                    } else {
                        /// g_pEffectLIST->Del_EFFECT( s_pEF );
                        SAFE_DELETE(s_pEF);
                        s_pEF = NULL;
                    }
                } break;

                ///'0'
                case 0x30:
                    g_GameDATA.m_bObserverCameraMode = !g_GameDATA.m_bObserverCameraMode;
                    SetObserverCameraOnOff();
                    break;

#endif
                default:
                    return false;
            }
            return true;
        }
        default:
            break;
    }
    return false;
    ;
}

///*-------------------------------------------------------------------------------------*/
#ifdef _DEBUG
    #include "../ObjFixedEvent.h"
#endif //_DEBUG
bool
CGameStateMain::On_WM_KEYDOWN(WPARAM wParam, LPARAM lParam) {
    /// ���ɿ� ���õ� �޼��� ó��...
    g_UserInputSystem.OnKeyDown(wParam, lParam);

    static float s_fScale = 1.0f;

    if (lParam & 0x40000000) {
        // ������ ���� �ִ� Ű��....
        return false;
    }

    switch (wParam) {
#ifdef _DEBUG
        case '8': {
            CObjFixedEvent* pObj = g_pObjMGR->GetEventObject(1);
            if (pObj == NULL)
                return 0;
            pObj->ExecEventScript(0);
        } break;
        case '9': {
            CObjFixedEvent* pObj = g_pObjMGR->GetEventObject(1);
            if (pObj == NULL)
                return 0;
            pObj->ExecEventScript(1);
        }
#endif //_DEBUG
        break;
            /*case VK_UP:
                g_pAVATAR->m_fHeightOfGround += 10.0f;
                break;

            case VK_DOWN:
                g_pAVATAR->m_fHeightOfGround -= 10.0f;
                break;*/

        case VK_CONTROL: {
            g_GameDATA.m_bShowDropItemInfo = true;
        } break;

        case VK_TAB: {
            if ((g_pAVATAR->Get_STATE() != CS_SIT) && (g_pAVATAR->Get_STATE() != CS_SITTING))
                g_pNet->Send_cli_TOGGLE(TOGGLE_TYPE_RUN);
        }
            return true;

        case 'C':
        case 'c': {

            if (GetAsyncKeyState(VK_CONTROL) < 0) {
                CTEditBox* pEditBox;
                pEditBox = g_itMGR.GetFocusEditBox();
                if (pEditBox != NULL) {
                    char* ptext = pEditBox->get_text();
                    if (ptext != NULL) {
                        CClipboardUtil::CopyStringtoClibboard(std::string(ptext));
                    }
                }
            }
        }
            return true;
        case 'V':
        case 'v': {
            if (GetAsyncKeyState(VK_CONTROL) < 0) {
                CTEditBox* pEditBox;
                pEditBox = g_itMGR.GetFocusEditBox();
                if (pEditBox != NULL) {
                    std::string ptext = CClipboardUtil::GetStringFromClibboard();
                    if (ptext.c_str() != NULL) {
                        pEditBox->Insert(ptext.c_str());
                    }
                }
            }
        }
            return true;

        default: {

        } break;
    }
    return false;
}

bool
CGameStateMain::On_WM_CHAR(WPARAM wParam, LPARAM lParam) {
    switch (wParam) {
        case VK_ESCAPE: {
            g_UserInputSystem.ClearMouseState();
        } break;
        case VK_RETURN:
            LogString(LOG_NORMAL, "VK_RETURN \n");

#ifdef __USE_IME
            m_IME.ClearString();
            m_IME.SetActive(true);
            m_IME.ToggleInputMode(true); // ������ �ѱ۷� ��ȯ ��Ų��.
#endif
            return true;
    }
    return false;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

bool
CGameStateMain::On_WM_LBUTTONDOWN(WPARAM wParam, LPARAM lParam) {
    if (NULL == g_pAVATAR)
        return true;

    if (g_pAVATAR->Get_HP() <= 0)
        return true;

    if (!this->m_bPickedPOS)
        return true;

    /// �Է��� ������ ����ʹ� �������.
    if (g_pAVATAR->bCanUserInput()) {
        g_UserInputSystem.ClickObject(this->m_iPickedOBJ, this->m_PosPICK, wParam);
    } else {
        g_itMGR.AppendChatMsg(STR_DOING_SKILL_ACTION, IT_MGR::CHAT_TYPE_SYSTEM);
    }

    CGame::GetInstance().ResetAutoRun();
    return true;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief WM_RBUTTONDOWN
//    9/14 ���� �������.. (ī�޶� ��ũ�� �浹 )
//----------------------------------------------------------------------------------------------------

bool
CGameStateMain::On_WM_RBUTTONDOWN(WPARAM wParam, LPARAM lParam) {
    if (NULL == g_pAVATAR) {
        return true;
    }

    if (g_pAVATAR->Get_HP() <= 0) {
        return true;
    }

    if (!this->m_bPickedPOS) {
        return true;
    }

    if (!g_pAVATAR->bCanUserInput()) {
        return true;
    }

    /// �Է��� ������ ����ʹ� �������.
    g_UserInputSystem.RButtonDown(this->m_iPickedOBJ, this->m_PosPICK, wParam);
    CGame::GetInstance().ResetAutoRun();
    return true;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

bool
CGameStateMain::On_WM_LBUTTONDBCLICK(WPARAM wParam, LPARAM lParam) {
    if (NULL == g_pAVATAR)
        return true;

    if (g_pAVATAR->Get_HP() <= 0)
        return true;

    if (!this->m_bPickedPOS)
        return true;

    /// �Է��� ������ ����ʹ� �������.
    if (g_pAVATAR->bCanUserInput()) {
        g_UserInputSystem.DBClickObject(this->m_iPickedOBJ, this->m_PosPICK, wParam);
    } else {
        g_itMGR.AppendChatMsg(STR_DOING_SKILL_ACTION, g_dwRED);
    }
    CGame::GetInstance().ResetAutoRun();
    return true;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

bool
CGameStateMain::On_WM_MOUSEWHEEL(WPARAM wParam, LPARAM lParam) {
    short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

#ifdef _DEBUG
    if (g_GameDATA.m_bObserverCameraMode) {
        ObserverCameraZoomInOut(static_cast<float>(-zDelta));
        return true;
    }
#endif

    g_pCamera->Add_Distance(static_cast<float>(-zDelta));
    return true;
}
//-------------------------------------------------------------------------------------------

void
CGameStateMain::Pick_POSITION(/*LPARAM	lParam*/) {
    D3DXVECTOR3 RayOrig;
    D3DXVECTOR3 RayDir;

    ::getRay(m_ScreenPOS.m_nX,
        m_ScreenPOS.m_nY,
        &RayOrig.x,
        &RayOrig.y,
        &RayOrig.z,
        &RayDir.x,
        &RayDir.y,
        &RayDir.z);

    CGame::GetInstance().SetRayDir(RayDir);
    CGame::GetInstance().SetRayOrig(RayOrig);

    D3DXVECTOR3 PosPICKTerrain;

    float fDistanceTerrain = g_fMaxDistance;
    float fDistanceObject = g_fMaxDistance;

    fDistanceTerrain = g_pTerrain->Pick_POSITION(PosPICKTerrain);

    {
        m_iPickedOBJ = g_pTerrain->Pick_OBJECT(m_PosPICK, fDistanceObject);
        if (m_iPickedOBJ) {
            m_bPickedPOS = true;
            /// ������ �浹�߰�, �������� �浹�Ÿ��� ������Ʈ���� �浹�Ÿ����� �� �����ٸ�..
            if ((fDistanceTerrain > 0) && (fDistanceObject > fDistanceTerrain)) {
                m_PosPICK = PosPICKTerrain;
                m_iPickedOBJ = 0;
            }
        } else {
            /// ������Ʈ�ʹ� �浹�� ���� �������� �浹�� ����.
            if (fDistanceTerrain > 0) {
                m_PosPICK = PosPICKTerrain;
                m_bPickedPOS = true;
            } else {
                /// SKY ���� �浹 üũ
                HNODE hSky = CSkyDOME::GetHNODE();
                float fDistanceSky = g_fMaxDistance;
                const float fDefaultSkyDistance = 3000.0f;

                m_iPickedOBJ = 0;

                if (g_pAVATAR && hSky && CGameOBJ::IsIntersect(hSky, fDistanceSky)) {
                    m_bPickedPOS = true;
                    CGameOBJ::AdjustPickPOSITION(fDefaultSkyDistance);
                    m_PosPICK = CGameOBJ::Get_PickPOSITION();
                } else {
                    m_bPickedPOS = false;
                }
            }
        }
    }
}

void
CGameStateMain::UpdateCheckFrame() {
    CGame& refGame = CGame::GetInstance();
    CCursor& refCursor = CCursor::GetInstance();

    // if( !refGame.GetUseUserMouseFlag() )
    //	return;

    if ((refCursor.GetCurrCursorType() == CCursor::CURSOR_DEFAULT
            && refGame.GetCheckFrame() >= MOUSE_STOP_CHECK_FRAME)
        || (refCursor.GetCurrCursorType() != CCursor::CURSOR_DEFAULT
            && refGame.GetCheckFrame() >= MOUSE_STOP_CHECK_FRAME / 2)) {
        Pick_POSITION(/*this->m_sCurMsg.lParam */);

        CTargetManager::GetSingleton().SetMouseTargetObject(0);

        /// ���� ���콺 Ŀ���� �����Ѵ�.

        // ��ŷ�� ����� ����������� �Բ� üũ.
        // this->m_iPickedOBJ �� 0�̶�� ��ȿ���� ���� �ǰ�?
        // - 2004.01.17.zho
        // - 2004.02.25.nAvy : ���� ���콺�� �������̽����� ������϶��� Default
        // - 2004.07.13 nAvy : ��������ϰ�� �׻� ����Ŀ����
        if (refGame.GetRepairMode()) {
            refCursor.SetCursorType(CCursor::CURSOR_REPAIR);
        } else if (g_itMGR.GetState() == IT_MGR::STATE_APPRAISAL) {
            refCursor.SetCursorType(CCursor::CURSOR_APPRAISAL);
        } else if (GetAsyncKeyState(VK_RBUTTON) < 0) {
            // Dagnarus
            POINT ptMouse;
            refGame.Get_MousePos(ptMouse);
            int iCheckFlag = CheckMouseRegion(ptMouse.x, ptMouse.y);

            if (!g_itMGR.IsMouseOnInterface())
                refCursor.SetCursorType(CCursor::CURSOR_WHEEL);

            else
                refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
        } else {

            POINT ptMouse;
            refGame.Get_MousePos(ptMouse);
            int iCheckFlag = CheckMouseRegion(ptMouse.x, ptMouse.y);

            if (iCheckFlag & SCREEN_LEFT)
                refCursor.SetCursorType(CCursor::CURSOR_LEFT);
            else if (iCheckFlag & SCREEN_RIGHT)
                refCursor.SetCursorType(CCursor::CURSOR_RIGHT);
            else if (g_itMGR.IsMouseOnInterface())
                refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
            else if (m_bPickedPOS && (m_iPickedOBJ > 0)) {
                CGameOBJ* pObj = g_pObjMGR->m_pOBJECTS[this->m_iPickedOBJ];
                if (pObj) {
                    switch (pObj->Get_TYPE()) {
                        case OBJ_MOB: {
                            CObjCHAR* character = reinterpret_cast<CObjCHAR*>(pObj);
                            if (character->CanClickable()) {
                                if (character->is_pvp_enabled()) {
                                    refCursor.SetCursorType(CCursor::CURSOR_ATTACK);
                                    {
                                        CTargetManager::GetSingleton().SetMouseTargetObject(
                                            this->m_iPickedOBJ);
                                    }
                                } else {
                                    refCursor.SetCursorType(CCursor::CURSOR_NPC);
                                }
                            }
                        } break;
                        case OBJ_NPC: {
                            CObjCHAR* character = reinterpret_cast<CObjCHAR*>(pObj);
                            if (character->CanClickable()) {
                                if (character->is_pvp_enabled()) {
                                    refCursor.SetCursorType(CCursor::CURSOR_ATTACK);
                                } else {
                                    refCursor.SetCursorType(CCursor::CURSOR_NPC);
                                }
                            }
                        } break;
                        case OBJ_AVATAR: {
                            CObjCHAR* character = reinterpret_cast<CObjCHAR*>(pObj);
                            if (character->CanClickable()) {
                                if (g_pAVATAR->is_pvp_enabled()
                                    && CUserInputState::IsEnemy(character)) {
                                    refCursor.SetCursorType(CCursor::CURSOR_ATTACK);
                                } else {
                                    refCursor.SetCursorType(CCursor::CURSOR_USER);
                                }
                            }
                        } break;
                        case OBJ_ITEM: {
                            CInfo MouseInfo;
                            MouseInfo.Clear();
                            CObjITEM* pItem = (CObjITEM*)g_pObjMGR->m_pOBJECTS[this->m_iPickedOBJ];
                            MouseInfo.AddString(pItem->Get_NAME(),
                                CItem::GetItemNameColor(pItem->m_ITEM.GetTYPE(),
                                    pItem->m_ITEM.GetItemNO()));
                            refCursor.SetCursorType(CCursor::CURSOR_ITEM_PICK, &MouseInfo);
                            break;
                        }
                        case OBJ_GROUND:
                        case OBJ_CNST:
                            refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
                            break;
                        default: {
                            refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
                        } break;
                    }
                }
            } else {
                refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
            }
        }
        refGame.ResetCheckFrame();
        return;
    }

    refGame.IncreseCheckFrame();
}
#define SCREEN_CHECK_WIDTH 1
int
CGameStateMain::CheckMouseRegion(int x, int y) {
    int iCheckFlag = 0;

    /// ����
    if (x < SCREEN_CHECK_WIDTH)
        iCheckFlag |= SCREEN_LEFT;

    /// ������
    if (x > (g_pCApp->GetWIDTH() - 1 - SCREEN_CHECK_WIDTH))
        iCheckFlag |= SCREEN_RIGHT;

    /*
    /// ����
    if( y < SCREEN_CHECK_WIDTH )
        iCheckFlag |= SCREEN_UP;

    /// �Ʒ���
    if( y > ( g_GameDATA.m_nScrHeight - SCREEN_CHECK_WIDTH ) )
        iCheckFlag |= SCREEN_DOWN;
    */

    return iCheckFlag;
}

void
CGameStateMain::UpdateCameraPositionByMouse() {
    POINT ptCurrMouse;
    CGame::GetInstance().Get_MousePos(ptCurrMouse);
    int iCheckFlag = CheckMouseRegion(ptCurrMouse.x, ptCurrMouse.y);

    if (iCheckFlag & SCREEN_LEFT) {
        g_pCamera->Add_YAW(-CAMERA_MOVE_SPEED);
        return;
    }

    if (iCheckFlag & SCREEN_RIGHT) {
        g_pCamera->Add_YAW(CAMERA_MOVE_SPEED);
        return;
    }
    /*
        if( iCheckFlag & SCREEN_UP )
            g_pCamera->Add_PITCH( -CAMERA_MOVE_SPEED );

        if( iCheckFlag & SCREEN_DOWN )
            g_pCamera->Add_PITCH( CAMERA_MOVE_SPEED );*/
}

int
CGameStateMain::ProcWndMsgInstant(unsigned uiMsg, WPARAM wParam, LPARAM lParam) {
    if (CGameState::ProcWndMsgInstant(uiMsg, wParam, lParam)) {
        return 1;
    }

    POINT ptMouse = {LOWORD(lParam), HIWORD(lParam)};
    switch (uiMsg) {
        case WM_MOUSEMOVE: {
#ifdef _DEBUG
            if (g_GameDATA.m_bObserverCameraMode) {
                if ((wParam & MK_RBUTTON)) {
                    ObserverCameraTransform((short)(ptMouse.x - m_PosRButtonClick.m_nX),
                        (short)(ptMouse.y - m_PosRButtonClick.m_nY));
                    /*if ( ptMouse.x - m_PosRButtonClick.m_nX )
                        g_pCamera->Add_YAW( (short)(ptMouse.x - m_PosRButtonClick.m_nX) );
                    if ( ptMouse.y - m_PosRButtonClick.m_nY )
                        g_pCamera->Add_PITCH( (short)(ptMouse.y - m_PosRButtonClick.m_nY) );*/

                    m_PosRButtonClick.m_nX = (short)ptMouse.x;
                    m_PosRButtonClick.m_nY = (short)ptMouse.y;
                }
            } else
#endif
            {
                if ((wParam & MK_RBUTTON)) {
                    if (ptMouse.x - m_PosRButtonClick.m_nX)
                        g_pCamera->Add_YAW((short)(ptMouse.x - m_PosRButtonClick.m_nX));
                    if (ptMouse.y - m_PosRButtonClick.m_nY)
                        g_pCamera->Add_PITCH((short)(ptMouse.y - m_PosRButtonClick.m_nY));

                    m_PosRButtonClick.m_nX = (short)ptMouse.x;
                    m_PosRButtonClick.m_nY = (short)ptMouse.y;

                    // Consume right-drag mousemoves synchronously so they never
                    // enter m_WndMsgQ. Windows delivers WM_MOUSEMOVE at 100-500 Hz
                    // during active drag; each queued message would otherwise walk
                    // every open dialog + icon for hit-testing in ProcMouseInput ->
                    // g_itMGR.MsgProc -> CITStateNormal::Process. No UI reacts to
                    // mousemove while RMB is held for camera control, so dropping
                    // the message is safe (m_ptCurrMouse is already updated in
                    // AddWndMsgQ before this call).
                    return 1;
                }
            }
            break;
        }
        case WM_RBUTTONDOWN:
            m_PosRButtonClick.m_nX = (short)ptMouse.x;
            m_PosRButtonClick.m_nY = (short)ptMouse.y;
            break;
        default:
            break;
    }
    return 0;
}
