#ifndef _ROSE_RML_OPTIONS_H_
#define _ROSE_RML_OPTIONS_H_

/**
 * UI2 options window: replaces COptionDlg ( DLG_TYPE_OPTION ), and the UI2
 * "Interface" window that /ui and the status panel's UI button opened ( now
 * this window's Interface tab ).
 *
 * Four tabs instead of five:
 *  - Graphics: fullscreen ( borderless ) or window, the window size, view
 *    distance ( LIST_CAMERA's three presets -- their names are Korean
 *    "low / mid / high", which the classic list printed as they were ),
 *    detail, shadows, antialiasing ( applies on restart: the device is built
 *    with it; classic had it commented out ).
 *  - Sound: music and effects, heard as you change them.
 *  - Gameplay: the mouse ( "Seven Hearts" = act on the first click, the
 *    default; the other = click to select, click again to act, with the
 *    arrow keys ), the chat keyboard mode, name / HP display ( incl. your
 *    own, which was ini-only ), and which requests you accept.
 *  - Interface: UI2 on / off, scale, panel lock, skill bar direction, and
 *    Reset positions ( at once, as the classic Initialize button ).
 *
 * The classic way: the form is edited, OK applies and saves, Cancel drops it
 * ( and puts back the volume you were hearing ). Unlike the classic dialog,
 * OK applies every tab -- classic applied only the tab on screen and dropped
 * the others' changes. Defaults puts the tab on screen back to the defaults
 * ( in the form, until OK ).
 *
 * Left in the ini on purpose: VSync, the frame cap, exclusive fullscreen,
 * background rendering and every diagnostic knob.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlOptions {
public:
    RoseRmlOptions();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    /// /ui and the status panel's UI button: the Interface tab ( or close ).
    void ToggleInterfaceTab();

    void Update();

    struct ChoiceVM {
        int value;
        Rml::String label;
        bool on;

        bool operator==(const ChoiceVM& o) const {
            return value == o.value && label == o.label && on == o.on;
        }
        bool operator!=(const ChoiceVM& o) const { return !(*this == o); }
    };

    struct BlockVM {
        int value;
        bool on;

        bool operator==(const BlockVM& o) const { return value == o.value && on == o.on; }
        bool operator!=(const BlockVM& o) const { return !(*this == o); }
    };

private:
    enum Group {
        G_DISPLAY,
        G_VIEW,
        G_DETAIL,
        G_SHADOW,
        G_AA,
        G_MOUSE,
        G_CHAT,
        G_SCALE,
        G_LOCK,
        G_BAR,
        G_COUNT
    };
    enum Toggle {
        T_PC_NAMES,
        T_NPC_NAMES,
        T_MOB_HP,
        T_MY_NAME,
        T_WHISPER,
        T_FRIEND,
        T_TRADE,
        T_PARTY,
        T_MESSENGER,
        T_UI2,
        T_COUNT
    };

    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void SetTab(int iTab);
    void Load();
    void Defaults();
    void Apply();
    void Cancel();
    void Choose(int iGroup, int iValue);
    void Flip(int iToggle);
    void SetVolume(bool bMusic, int iValue);
    void StepSize(int iStep);
    void RefreshView();
    void BuildSizes();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    int m_iBootAA; ///< the antialiasing the device was built with

    /// The form ( m_iGroup[G_*], m_bToggle[T_*] ).
    int m_iGroup[G_COUNT];
    bool m_bToggle[T_COUNT];
    int m_iMusic;
    int m_iEffects;
    std::vector<std::pair<int, int> > m_Sizes; ///< window sizes, ascending
    int m_iSize; ///< index in m_Sizes

    /// --- bound to options.rml ------------------------------------------------
    int m_iTab; ///< 0 graphics, 1 sound, 2 gameplay, 3 interface
    std::vector<ChoiceVM> m_Choices[G_COUNT];
    bool m_bBound[T_COUNT];
    std::vector<BlockVM> m_MusicBlocks;
    std::vector<BlockVM> m_EffectBlocks;
    Rml::String m_strSize; ///< "1280 x 720"
    bool m_bWindowed; ///< the form's choice ( the size applies )
    Rml::String m_strScreen; ///< the monitor, for fullscreen
    bool m_bAARestart;
};

#endif /// _ROSE_RML_OPTIONS_H_
