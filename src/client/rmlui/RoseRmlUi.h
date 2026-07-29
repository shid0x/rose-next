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

namespace RoseRmlUi {

/// Whether the spike is switched on for this run. Cheap; safe to call per frame.
bool IsEnabled();

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

/// Draw-call count of the last rendered frame, for the debug HUD.
int GetDrawCallCount();

} // namespace RoseRmlUi

#endif /// _ROSE_RML_UI_H_
