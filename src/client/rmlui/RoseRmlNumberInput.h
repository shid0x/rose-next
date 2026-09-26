#ifndef _ROSE_RML_NUMBER_INPUT_H_
#define _ROSE_RML_NUMBER_INPUT_H_

/**
 * UI2 "how many?": replaces CNumberInputDlg ( DLG_TYPE_N_INPUT ), the
 * quantity question behind buying, selling, dropping, banking and trading a
 * stack, or handing over money.
 *
 * The hidden CNumberInputDlg stays the model: whoever asks
 * ( CTCmdOpenNumberInputDlg ) gives it the command, the item and the maximum
 * and then opens it through IT_MGR, which lands here. The answer goes back
 * through it ( Submit / Cancel ), so the command runs exactly as the classic
 * OK button ran it.
 *
 * The keypad, the Max button ( which answers with the maximum at once, as
 * the classic one did ), and the keyboard while it is open: digits, the
 * numeric keypad, Backspace, Enter and Escape. The first digit typed
 * replaces the suggested 1. It opens by the mouse, where the drag ended.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>

#include <string>

class CNumberInputDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlNumberInput {
public:
    RoseRmlNumberInput();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog. Opening while open asks the new
    /// question; closing drops the command, as the classic close button.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    /// WM_KEYDOWN / WM_CHAR while open. True when the key was the popup's --
    /// it must not also reach the chat or the hotkeys.
    bool ProcessKey(UINT uiMsg, WPARAM wParam);

    void Update();

private:
    CNumberInputDlg* Dlg() const;
    void SetVisible(bool bVisible);
    void Describe();
    void Type(int iDigit);
    void Erase();
    void Answer(bool bOk, bool bMax);
    void SetValue(const Rml::String& strValue);
    __int64 Value() const;
    void PlaceAtMouse();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    bool m_bPlace; ///< put it by the mouse once it is laid out
    POINT m_ptOpen; ///< the mouse when it was asked
    WPARAM m_SwallowChar; ///< the WM_CHAR of an Enter / Escape we answered
    __int64 m_iMax;

    /// --- bound to numinput.rml ---------------------------------------------
    Rml::String m_strTitle;
    bool m_bHasItem;
    Rml::String m_strItemName;
    Rml::String m_strItemSrc;
    Rml::String m_strItemRect;
    Rml::String m_strMax;
    Rml::String m_strValue; ///< digits
    bool m_bFresh; ///< the suggested value, replaced by the first digit
};

#endif /// _ROSE_RML_NUMBER_INPUT_H_
