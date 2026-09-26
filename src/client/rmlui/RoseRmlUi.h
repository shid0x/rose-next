#ifndef _ROSE_RML_UI_H_
#define _ROSE_RML_UI_H_

/**
 * Spike 1 host for RmlUi: owns the context, the interfaces and the device
 * lifetime hooks. See doc/rmlui-evaluation.md.
 *
 * Everything here is inert unless explicitly enabled ( [UI] RMLUI=1 in
 * rose-next.ini, or ROSE_RMLUI=1 in the environment ), so a build with the
 * library linked behaves exactly like one without it until someone opts in.
 */

#include <windows.h>

class CTCommand;
class CInfo;

namespace RoseRmlUi {

/// Whether the spike is switched on for this run. Cheap; safe to call per frame.
bool IsEnabled();

/// Whether Initialise() succeeded, i.e. the overlay is actually drawing.
bool IsInitialised();

/// Creates the interfaces, the context and loads the spike document.
/// Call once, after the D3D device exists.
bool Initialise(HWND hWnd, void* pD3DDevice, int iWidth, int iHeight);
void Shutdown();

/// Per-frame update ( animations, transitions ). Call before Render.
void Update();

/// Renders the context. Must sit inside beginScene()/endScene() but OUTSIDE the
/// engine's beginSprite()/endSprite() block - the state guard assumes it owns
/// the device for the duration.
void Render();

/// --- device lifetime ----------------------------------------------------
/// Wire these to the same points the engine invalidates/restores its own
/// D3DPOOL_DEFAULT resources. The client still rebuilds the device on a
/// windowed frame drag even on 9Ex.
void OnBeforeDeviceRebuild();
void OnAfterDeviceRebuild(int iWidth, int iHeight);
void OnResize(int iWidth, int iHeight);

/// Returns true when RmlUi consumed the message and it must not reach the
/// legacy dialog chain. Call FIRST in CGameStateMain::ProcWndMsgInstant.
bool ProcessWndMsg(HWND hWnd, UINT uiMsg, WPARAM wParam, LPARAM lParam);

/// A classic tooltip ( CInfo ) for a UI2 panel, placed as the classic slots
/// place theirs: at the cursor, 20 px to its right -- to its left when that
/// runs off the screen -- and registered with CToolTipMgr. Under UI2 the
/// tooltip is drawn after the RmlUi pass, so it may sit over any window.
void PlaceTooltipAtCursor(CInfo& ToolTip);

/// Draw-call count of the last rendered frame, for the debug HUD.
int GetDrawCallCount();

/// Re-reads every stylesheet of every loaded .rml document from disk ( the
/// "/uireload" chat command ), so a skin can be tuned with the game running.
/// Styles only: markup and data bindings still need a restart. Returns the
/// number of documents reloaded.
int ReloadStyleSheets();

/// --- damage meter -------------------------------------------------------
/// The RmlUi-drawn damage meter. When the overlay is enabled, "/dps" opens
/// this instead of the legacy CDamageMeterPanel; both read the same
/// CDamageMeter data core, so they are interchangeable views.
void ToggleDamageMeter();

/// --- UI2 --------------------------------------------------------------------
/// The interface settings window ( scale, lock, reset layout ): "/ui" and the
/// status panel's UI button.
void ToggleInterfacePanel();

/// The UI2 skill bar's absolute hot-icon index under the point, in the row
/// standing for iDlgType ( DLG_TYPE_QUICKBAR / _EXT ), or -1. What
/// CQuickBAR::GetMouseClickSlot answers while UI2 replaces the quickbars.
short SkillBarSlotAt(int x, int y, int iDlgType);

/// UI2 windows standing in for legacy dialog types ( RoseUi2::OpenWindow ).
/// Unknown types are ignored / report closed.
void SetWindowOpen(int iDlgType, bool bOpen);
bool IsWindowOpen(int iDlgType);

/// Skill bar orientation ( Interface window ).
void SetSkillBarVertical(bool bVertical);
bool IsSkillBarVertical();
bool IsDamageMeterVisible();

/// A party request arrived ( Recv_gsv_PARTY_REQ ): show it as the UI2 prompt.
/// False when UI2 cannot, and the caller opens the legacy message box.
bool ShowPartyInvite(unsigned short wFromObjSvrIdx, const char* pszFrom, bool bMake);
/// A UI2 party invitation is waiting for an answer ( new requests are BUSY ).
bool IsPartyInvitePending();

/// A trade request arrived ( Recv_gsv_TRADE_P2P ): show it as the UI2 prompt.
/// False when UI2 cannot, and the caller opens the legacy message box.
bool ShowTradeInvite(unsigned short wFromObjSvrIdx, const char* pszFrom);
/// A UI2 trade request is waiting for an answer ( new requests are BUSY ).
bool IsTradeInvitePending();
/// The other side changed its offer while we were ready ( Recv_gsv_TRADE_P2P_ITEM ):
/// the UI2 trade window takes our readiness back and says why. False when no
/// UI2 trade is showing ( the caller warns the classic way ).
bool TradeOfferChangedWhileReady();

/// UI2 message box ( RoseRmlMessageBox ). A question with named answers, each
/// running its legacy CTCommand; ownership passes only on true. False when
/// UI2 cannot show it: open the legacy IT_MGR::OpenMsgBox instead.
bool ConfirmBox(const char* pszTitle,
    const char* pszText,
    const char* pszOk,
    const char* pszCancel,
    CTCommand* pOk,
    CTCommand* pCancel);
/// A notice that closes itself.
bool NoticeBox(const char* pszTitle, const char* pszText);

/// The UI2 inventory ( RoseRmlInventory ) answering CItemDlg's slot questions
/// while it stands in for the classic dialog. Screen pixels.
bool InventoryBagAt(int x, int y);
bool InventoryBagSlotAt(int x, int y, int& iPage, int& iSlot);
bool InventoryEquipAt(int x, int y);
int InventoryEquipSlotAt(int x, int y);
bool InventoryCostumeOpen();

/// The UI2 minimap ( RoseRmlMinimap ): the M and L keys.
void MinimapToggleCollapsed();
void MinimapCycleSize();

/// The UI2 conversation ( RoseRmlConversation ), fed by IT_MGR::OpenQueryDLG /
/// QueryDLG_AppendExam while UI2 replaces the conversation dialogs. iMode is
/// RoseRmlConversation::Mode ( 0 NPC, 1 choice, 2 event object ).
void ConversationBegin(int iMode, const char* pszText, int iOwnerClientIdx);
void ConversationAddAnswer(const char* pszText, int iEventID, void (*fpHandler)(int iEventID));

} // namespace RoseRmlUi

#endif /// _ROSE_RML_UI_H_
