#include "stdafx.h"

#include "RoseRmlOptions.h"
#include "RoseRmlLayout.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../CClientStorage.h"
#include "../CObjUSER.h"
#include "../Game.h"
#include "../JCommandState.h"
#include "../Sound/MusicMgr.h"
#include "../System/CGame.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Dlgs/COptionDlg.h"
#include "tgamectrl/tcontrolmgr.h"
#include "tgamectrl/teditbox.h"
#include "tgamectrl/tgamectrl.h"

#include "rose/common/log.h"

#include <algorithm>
#include <math.h>

namespace {

enum { TAB_GRAPHICS = 0, TAB_SOUND = 1, TAB_GAMEPLAY = 2, TAB_INTERFACE = 3 };

const int kScalePresets[] = {90, 100, 110, 125, 150, 175, 200};

/// The form's defaults ( the classic ones, CClientStorage.h ).
const int kDefaultMouse = c_iDefaultControlType;
const int kDefaultChat = 0;

const char* kGroupVars[] = {"c_display", "c_view", "c_detail", "c_shadow", "c_aa", "c_mouse",
    "c_chat", "c_scale", "c_lock", "c_bar"};
const char* kToggleVars[] = {"t_pc_names", "t_npc_names", "t_mob_hp", "t_my_name", "t_whisper",
    "t_friend", "t_trade", "t_party", "t_messenger", "t_ui2"};

} // namespace

RoseRmlOptions::RoseRmlOptions():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iBootAA(0),
    m_iMusic(DEFAULT_BGM_VOLUME),
    m_iEffects(DEFAULT_EFFECT_VOLUME),
    m_iSize(0),
    m_iTab(TAB_GRAPHICS),
    m_bWindowed(false),
    m_bAARestart(false) {
    for (int g = 0; g < G_COUNT; ++g)
        m_iGroup[g] = 0;
    for (int t = 0; t < T_COUNT; ++t)
        m_bToggle[t] = m_bBound[t] = false;
}

bool
RoseRmlOptions::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// The choices each group offers ( labels fixed, "on" follows the form ).
    struct { int iGroup; int iValue; const char* pszLabel; } kChoices[] = {
        {G_DISPLAY, 1, "Fullscreen"},
        {G_DISPLAY, 0, "Window"},
        {G_VIEW, 0, "Near"},
        {G_VIEW, 1, "Normal"},
        {G_VIEW, 2, "Far"},
        /// iPerformance: index into c_iPeformances ( engine level 5 .. 1 ).
        {G_DETAIL, 0, "Lowest"},
        {G_DETAIL, 1, "Low"},
        {G_DETAIL, 2, "Medium"},
        {G_DETAIL, 3, "High"},
        {G_DETAIL, 4, "Highest"},
        {G_SHADOW, 0, "Low"},
        {G_SHADOW, 1, "Medium"},
        {G_SHADOW, 2, "High"},
        {G_AA, 0, "Off"},
        {G_AA, 2, "2x"},
        {G_AA, 4, "4x"},
        {G_AA, 8, "8x"},
        {G_MOUSE, 1, "Act on click"},
        {G_MOUSE, 0, "Select, then act"},
        {G_CHAT, 0, "Press Enter to type"},
        {G_CHAT, 1, "Always typing"},
        {G_LOCK, 0, "Movable"},
        {G_LOCK, 1, "Locked"},
        {G_BAR, 0, "Horizontal"},
        {G_BAR, 1, "Vertical"},
    };
    for (size_t i = 0; i < sizeof(kChoices) / sizeof(kChoices[0]); ++i) {
        ChoiceVM vm = {kChoices[i].iValue, kChoices[i].pszLabel, false};
        m_Choices[kChoices[i].iGroup].push_back(vm);
    }
    for (int iPreset : kScalePresets) {
        ChoiceVM vm = {iPreset, Rml::String(CStr::Printf("%d%%", iPreset)), false};
        m_Choices[G_SCALE].push_back(vm);
    }
    for (int i = 0; i < MAX_BGM_VOLUME; ++i) {
        BlockVM vm = {i, false};
        m_MusicBlocks.push_back(vm);
    }
    for (int i = 0; i < MAX_EFFECT_VOLUME; ++i) {
        BlockVM vm = {i, false};
        m_EffectBlocks.push_back(vm);
    }

    t_OptionVideo Video;
    g_ClientStorage.GetVideoOption(Video);
    m_iBootAA = (int)Video.iAntiAlising;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("options");
    if (!constructor)
        return false;

    if (auto choice = constructor.RegisterStruct<ChoiceVM>()) {
        choice.RegisterMember("value", &ChoiceVM::value);
        choice.RegisterMember("label", &ChoiceVM::label);
        choice.RegisterMember("on", &ChoiceVM::on);
    }
    constructor.RegisterArray<std::vector<ChoiceVM>>();
    if (auto block = constructor.RegisterStruct<BlockVM>()) {
        block.RegisterMember("value", &BlockVM::value);
        block.RegisterMember("on", &BlockVM::on);
    }
    constructor.RegisterArray<std::vector<BlockVM>>();

    constructor.Bind("tab", &m_iTab);
    for (int g = 0; g < G_COUNT; ++g)
        constructor.Bind(kGroupVars[g], &m_Choices[g]);
    for (int t = 0; t < T_COUNT; ++t)
        constructor.Bind(kToggleVars[t], &m_bBound[t]);
    constructor.Bind("music", &m_MusicBlocks);
    constructor.Bind("effects", &m_EffectBlocks);
    constructor.Bind("win_size", &m_strSize); /// not "size": reserved ( arrays' .size )
    constructor.Bind("windowed", &m_bWindowed);
    constructor.Bind("screen", &m_strScreen);
    constructor.Bind("aa_restart", &m_bAARestart);

    constructor.BindEventCallback("set_tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                SetTab(args[0].Get<int>());
        });
    constructor.BindEventCallback("choose",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.size() >= 2)
                Choose(args[0].Get<int>(), args[1].Get<int>());
        });
    constructor.BindEventCallback("flip",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                Flip(args[0].Get<int>());
        });
    constructor.BindEventCallback("volume",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.size() >= 2)
                SetVolume(args[0].Get<int>() == 0, args[1].Get<int>());
        });
    constructor.BindEventCallback("size_step",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                StepSize(args[0].Get<int>());
        });
    constructor.BindEventCallback("reset_positions",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            /// At once, as the classic Initialize button ( the classic windows
            /// still used under UI2 too ).
            RoseRmlLayout::Reset();
            g_itMGR.InitInterfacePos();
        });
    constructor.BindEventCallback("defaults",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Defaults(); });
    constructor.BindEventCallback("ok",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            Apply();
            g_itMGR.CloseDialog(DLG_TYPE_OPTION);
            /// Last: switching UI2 off closes every UI2 window, this one too.
            if (!m_bToggle[T_UI2] && RoseUi2::IsChosen())
                RoseUi2::SetActive(false);
        });
    constructor.BindEventCallback("cancel",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_OPTION);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "options.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load options document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("options");
    RoseRmlLayout::Track(m_pPanel, "options");

    LOG_INFO("[rmlui] options document loaded");
    return true;
}

void
RoseRmlOptions::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

bool
RoseRmlOptions::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlOptions::SetOpen(bool bOpen) {
    if (bOpen == m_bOpen)
        return;
    RoseUi2::PlayWindowSound(DLG_TYPE_OPTION, bOpen);
    m_bOpen = bOpen;
    if (bOpen)
        Load(); /// the form starts from what is in use
    else
        Cancel(); /// every close but OK drops the form ( OK applied it first )
}

void
RoseRmlOptions::ToggleInterfaceTab() {
    if (m_bOpen && m_iTab == TAB_INTERFACE) {
        g_itMGR.CloseDialog(DLG_TYPE_OPTION);
        return;
    }
    SetTab(TAB_INTERFACE);
    if (!m_bOpen)
        g_itMGR.OpenDialog(DLG_TYPE_OPTION, false);
}

void
RoseRmlOptions::SetTab(int iTab) {
    if (iTab < TAB_GRAPHICS || iTab > TAB_INTERFACE || iTab == m_iTab)
        return;
    m_iTab = iTab;
    m_Model.DirtyVariable("tab");
}

void
RoseRmlOptions::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- the form ---------------------------------------------------------------------

void
RoseRmlOptions::BuildSizes() {
    /// The window sizes the display offers ( one per width x height, the
    /// deepest colour ), plus the one in use if the list lacks it.
    m_Sizes.clear();
    std::set<ApplicationVideoMode> modes = g_pCApp->get_video_modes();
    for (std::set<ApplicationVideoMode>::const_iterator it = modes.begin(); it != modes.end(); ++it) {
        if (it->depth < 32 || it->width < 800 || it->height < 600)
            continue;
        const std::pair<int, int> size(it->width, it->height);
        if (std::find(m_Sizes.begin(), m_Sizes.end(), size) == m_Sizes.end())
            m_Sizes.push_back(size);
    }
    t_OptionVideo Video;
    g_ClientStorage.GetVideoOption(Video);
    const std::pair<int, int> cur(Video.tResolution.iWidth, Video.tResolution.iHeight);
    if (std::find(m_Sizes.begin(), m_Sizes.end(), cur) == m_Sizes.end())
        m_Sizes.push_back(cur);
    std::sort(m_Sizes.begin(), m_Sizes.end());
    m_iSize = (int)(std::find(m_Sizes.begin(), m_Sizes.end(), cur) - m_Sizes.begin());
}

void
RoseRmlOptions::Load() {
    t_OptionVideo Video;
    t_OptionSound Sound;
    t_OptionPlay Play;
    t_OptionCommunity Community;
    t_OptionKeyboard Keyboard;
    g_ClientStorage.GetVideoOption(Video);
    g_ClientStorage.GetSoundOption(Sound);
    g_ClientStorage.GetPlayOption(Play);
    g_ClientStorage.GetCommunityOption(Community);
    g_ClientStorage.GetKeyboardOption(Keyboard);

    BuildSizes();
    /// The window shape in use ( Alt+Enter changes it behind the storage ).
    m_iGroup[G_DISPLAY] = g_pCApp->IsWindowedFrame() ? 0 : 1;
    m_iGroup[G_VIEW] = min(2, (int)Video.iCamera);
    m_iGroup[G_DETAIL] = min(MAX_PERFORMANCE_COUNT - 1, (int)Video.iPerformance);
    m_iGroup[G_SHADOW] = min(g_iMaxShadowQuality, (int)Video.iShadowQuality);
    m_iGroup[G_AA] = (int)Video.iAntiAlising;
    m_iGroup[G_MOUSE] = Play.uiControlType ? 1 : 0;
    m_iGroup[G_CHAT] = Keyboard.iChattingMode ? 1 : 0;
    m_iGroup[G_SCALE] = RoseRmlLayout::GetScale();
    m_iGroup[G_LOCK] = RoseRmlLayout::IsLocked() ? 1 : 0;
    m_iGroup[G_BAR] = RoseRmlUi::IsSkillBarVertical() ? 1 : 0;

    m_iMusic = (Sound.iBgmVolume < MAX_BGM_VOLUME) ? (int)Sound.iBgmVolume : DEFAULT_BGM_VOLUME;
    m_iEffects =
        (Sound.iEffectVolume < MAX_EFFECT_VOLUME) ? (int)Sound.iEffectVolume : DEFAULT_EFFECT_VOLUME;

    m_bToggle[T_PC_NAMES] = Play.iShowPcName != 0;
    m_bToggle[T_NPC_NAMES] = Play.iShowNpcName != 0;
    m_bToggle[T_MOB_HP] = g_ClientStorage.IsShowMobHp();
    m_bToggle[T_MY_NAME] = Play.iShowMyName;
    m_bToggle[T_WHISPER] = Community.iWhisper != 0;
    m_bToggle[T_FRIEND] = Community.iAddFriend != 0;
    m_bToggle[T_TRADE] = Community.iExchange != 0;
    m_bToggle[T_PARTY] = Community.iParty != 0;
    m_bToggle[T_MESSENGER] = Community.iMessanger != 0;
    m_bToggle[T_UI2] = RoseUi2::IsChosen();

    RefreshView();
}

/// The tab on screen back to the defaults, in the form only ( until OK ).
/// Graphics keeps the display shape and size: a default window of 1024 x 768
/// is a surprise, not a fix.
void
RoseRmlOptions::Defaults() {
    switch (m_iTab) {
        case TAB_GRAPHICS:
            m_iGroup[G_VIEW] = g_iDefaultCamera;
            m_iGroup[G_DETAIL] = g_iDefaultPerfromance;
            m_iGroup[G_SHADOW] = g_iDefaultShadowQuality;
            m_iGroup[G_AA] = g_iDefaultAntiAlising;
            break;
        case TAB_SOUND:
            SetVolume(true, DEFAULT_BGM_VOLUME);
            SetVolume(false, DEFAULT_EFFECT_VOLUME);
            break;
        case TAB_GAMEPLAY:
            m_iGroup[G_MOUSE] = kDefaultMouse;
            m_iGroup[G_CHAT] = kDefaultChat;
            m_bToggle[T_PC_NAMES] = m_bToggle[T_NPC_NAMES] = c_iDefaultShowName != 0;
            m_bToggle[T_MOB_HP] = true;
            m_bToggle[T_MY_NAME] = false;
            m_bToggle[T_WHISPER] = m_bToggle[T_FRIEND] = m_bToggle[T_TRADE] = m_bToggle[T_PARTY] =
                m_bToggle[T_MESSENGER] = c_iDefaultCommunityOption != 0;
            break;
        case TAB_INTERFACE:
            m_iGroup[G_SCALE] = 100;
            m_iGroup[G_LOCK] = 0;
            m_iGroup[G_BAR] = 0;
            m_bToggle[T_UI2] = true;
            break;
        default:
            break;
    }
    RefreshView();
}

void
RoseRmlOptions::Choose(int iGroup, int iValue) {
    if (iGroup < 0 || iGroup >= G_COUNT)
        return;
    m_iGroup[iGroup] = iValue;
    RefreshView();
}

void
RoseRmlOptions::Flip(int iToggle) {
    if (iToggle < 0 || iToggle >= T_COUNT)
        return;
    m_bToggle[iToggle] = !m_bToggle[iToggle];
    RefreshView();
}

/// Heard at once, as the classic sliders ( Cancel puts the saved one back ).
void
RoseRmlOptions::SetVolume(bool bMusic, int iValue) {
    if (bMusic) {
        m_iMusic = max(0, min(MAX_BGM_VOLUME - 1, iValue));
        CMusicMgr::GetSingleton().SetVolume(g_ListBgmVolume[m_iMusic]);
    } else {
        m_iEffects = max(0, min(MAX_EFFECT_VOLUME - 1, iValue));
        g_pSoundLIST->SetVolume(g_ListEffectVolume[m_iEffects]);
    }
    RefreshView();
}

void
RoseRmlOptions::StepSize(int iStep) {
    if (m_Sizes.empty() || m_iGroup[G_DISPLAY] != 0)
        return;
    m_iSize = max(0, min((int)m_Sizes.size() - 1, m_iSize + iStep));
    RefreshView();
}

/// The bound view from the form.
void
RoseRmlOptions::RefreshView() {
    for (int g = 0; g < G_COUNT; ++g) {
        bool bChanged = false;
        for (size_t i = 0; i < m_Choices[g].size(); ++i) {
            const bool bOn = m_Choices[g][i].value == m_iGroup[g];
            if (m_Choices[g][i].on != bOn) {
                m_Choices[g][i].on = bOn;
                bChanged = true;
            }
        }
        if (bChanged)
            m_Model.DirtyVariable(kGroupVars[g]);
    }
    for (int t = 0; t < T_COUNT; ++t) {
        if (m_bBound[t] != m_bToggle[t]) {
            m_bBound[t] = m_bToggle[t];
            m_Model.DirtyVariable(kToggleVars[t]);
        }
    }
    struct { std::vector<BlockVM>* pBlocks; int iValue; const char* pszVar; } vols[] = {
        {&m_MusicBlocks, m_iMusic, "music"},
        {&m_EffectBlocks, m_iEffects, "effects"},
    };
    for (int v = 0; v < 2; ++v) {
        bool bChanged = false;
        for (size_t i = 0; i < vols[v].pBlocks->size(); ++i) {
            BlockVM& block = (*vols[v].pBlocks)[i];
            const bool bOn = block.value <= vols[v].iValue;
            if (block.on != bOn) {
                block.on = bOn;
                bChanged = true;
            }
        }
        if (bChanged)
            m_Model.DirtyVariable(vols[v].pszVar);
    }

    const bool bWindowed = m_iGroup[G_DISPLAY] == 0;
    if (bWindowed != m_bWindowed) {
        m_bWindowed = bWindowed;
        m_Model.DirtyVariable("windowed");
    }
    Rml::String strSize;
    if (m_iSize >= 0 && m_iSize < (int)m_Sizes.size())
        strSize = CStr::Printf("%d x %d", m_Sizes[m_iSize].first, m_Sizes[m_iSize].second);
    if (strSize != m_strSize) {
        m_strSize = strSize;
        m_Model.DirtyVariable("win_size");
    }
    /// Borderless fullscreen always covers the whole monitor.
    const Rml::String strScreen = CStr::Printf("Your screen, %d x %d",
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (strScreen != m_strScreen) {
        m_strScreen = strScreen;
        m_Model.DirtyVariable("screen");
    }
    const bool bRestart = m_iGroup[G_AA] != m_iBootAA;
    if (bRestart != m_bAARestart) {
        m_bAARestart = bRestart;
        m_Model.DirtyVariable("aa_restart");
    }
}

/// Every close but OK: the sounds go back to the saved volumes.
void
RoseRmlOptions::Cancel() {
    t_OptionSound Sound;
    g_ClientStorage.GetSoundOption(Sound);
    CMusicMgr::GetSingleton().SetVolume(g_ClientStorage.GetBgmVolumeByIndex(Sound.iBgmVolume));
    g_pSoundLIST->SetVolume(g_ClientStorage.GetEffectVolumeByIndex(Sound.iEffectVolume));
}

/// OK: the whole form, every tab, then saved ( COptionDlg's steps ).
void
RoseRmlOptions::Apply() {
    t_OptionVideo Video;
    t_OptionSound Sound;
    t_OptionPlay Play;
    t_OptionCommunity Community;
    t_OptionKeyboard Keyboard;
    g_ClientStorage.GetVideoOption(Video);
    g_ClientStorage.GetSoundOption(Sound);
    g_ClientStorage.GetPlayOption(Play);
    g_ClientStorage.GetCommunityOption(Community);
    g_ClientStorage.GetKeyboardOption(Keyboard);

    /// --- graphics
    if ((int)Video.iCamera != m_iGroup[G_VIEW]) {
        g_ClientStorage.ApplyCameraOption((short)m_iGroup[G_VIEW]);
        Video.iCamera = m_iGroup[G_VIEW];
    }
    const bool bShadow = (int)Video.iShadowQuality != m_iGroup[G_SHADOW];
    const bool bDetail = (int)Video.iPerformance != m_iGroup[G_DETAIL];
    Video.iShadowQuality = m_iGroup[G_SHADOW];
    Video.iPerformance = m_iGroup[G_DETAIL];
    if (bShadow)
        setShadowmapSizeOverride(ShadowQualityToShadowmapSize(Video.iShadowQuality));
    /// Also what re-creates the render targets ( the shadow map's size ).
    if (bShadow || bDetail)
        setDisplayQualityLevel(c_iPeformances[Video.iPerformance]);
    Video.iAntiAlising = m_iGroup[G_AA]; /// the device is built with it: on restart
    g_ClientStorage.SetVideoOption(Video);

    /// The display: the shape first ( Alt+Enter's toggle sizes a window from
    /// the stored size, so that is stored before ), then a new window size.
    const bool bWantFull = m_iGroup[G_DISPLAY] == 1;
    const bool bIsFull = !g_pCApp->IsWindowedFrame();
    t_OptionResolution Size = Video.tResolution;
    if (m_iSize >= 0 && m_iSize < (int)m_Sizes.size()) {
        Size.iWidth = m_Sizes[m_iSize].first;
        Size.iHeight = m_Sizes[m_iSize].second;
    }
    const bool bNewSize =
        Size.iWidth != Video.tResolution.iWidth || Size.iHeight != Video.tResolution.iHeight;
    if (bWantFull != bIsFull) {
        if (!bWantFull && bNewSize) {
            Video.tResolution = Size;
            g_ClientStorage.SetVideoOption(Video);
        }
        CGame::GetInstance().ChangeScreenMode();
        g_ClientStorage.SetVideoFullScreen(bWantFull ? 1 : 0);
    } else if (!bWantFull && bNewSize) {
        COptionDlg::ApplyResolution(Size, true, false);
    }

    /// --- sound
    Sound.iBgmVolume = m_iMusic;
    Sound.iEffectVolume = m_iEffects;
    CMusicMgr::GetSingleton().SetVolume(g_ClientStorage.GetBgmVolumeByIndex(Sound.iBgmVolume));
    g_pSoundLIST->SetVolume(g_ClientStorage.GetEffectVolumeByIndex(Sound.iEffectVolume));
    g_ClientStorage.SetSoundOption(Sound);

    /// --- gameplay
    if ((Play.uiControlType ? 1 : 0) != m_iGroup[G_MOUSE]) {
        Play.uiControlType = m_iGroup[G_MOUSE];
        g_UserInputSystem.ChangeUserInputStyle(
            Play.uiControlType ? SEVENHEARTS_USER_INPUT_STYLE : DEFAULT_USER_INPUT_STYLE);
    }
    Play.iShowPcName = m_bToggle[T_PC_NAMES] ? 1 : 0;
    Play.iShowNpcName = m_bToggle[T_NPC_NAMES] ? 1 : 0;
    Play.iShowMyName = m_bToggle[T_MY_NAME];
    g_ClientStorage.m_bShowMobHp = m_bToggle[T_MOB_HP];
    g_ClientStorage.SetPlayOption(Play);

    if ((Keyboard.iChattingMode ? 1 : 0) != m_iGroup[G_CHAT]) {
        Keyboard.iChattingMode = m_iGroup[G_CHAT];
        if (Keyboard.iChattingMode) {
            it_SetKeyboardInputType(CTControlMgr::INPUTTYPE_AUTOENTER);
            /// As COptionDlg: with nothing focused, the chat box takes the keys.
            CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_CHAT);
            if (CTEditBox::s_pFocusEdit == NULL && pDlg != NULL)
                pDlg->Show();
        } else {
            it_SetKeyboardInputType(CTControlMgr::INPUTTYPE_NORMAL);
        }
    }
    g_ClientStorage.SetKeyboardOption(Keyboard);

    Community.iWhisper = m_bToggle[T_WHISPER] ? 1 : 0;
    Community.iAddFriend = m_bToggle[T_FRIEND] ? 1 : 0;
    Community.iExchange = m_bToggle[T_TRADE] ? 1 : 0;
    Community.iParty = m_bToggle[T_PARTY] ? 1 : 0;
    Community.iMessanger = m_bToggle[T_MESSENGER] ? 1 : 0;
    g_ClientStorage.SetCommunityOption(Community);

    g_ClientStorage.Save();

    /// --- interface ( each saves its own [VIDEO] key )
    if (RoseRmlLayout::GetScale() != m_iGroup[G_SCALE])
        RoseRmlLayout::SetScale(m_iGroup[G_SCALE]);
    if (RoseRmlLayout::IsLocked() != (m_iGroup[G_LOCK] != 0))
        RoseRmlLayout::SetLocked(m_iGroup[G_LOCK] != 0);
    if (RoseRmlUi::IsSkillBarVertical() != (m_iGroup[G_BAR] != 0))
        RoseRmlUi::SetSkillBarVertical(m_iGroup[G_BAR] != 0);
    /// UI2 off is the caller's last step ( it closes this window ).
}

void
RoseRmlOptions::PlaceDefault() {
    /// Centred, a little high -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fLeft = floorf(max(0.0f, ((float)view.x - size.x) * 0.5f));
    const float fTop = floorf(max(0.0f, ((float)view.y - size.y) * 0.3f));
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlOptions::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen && !IsInWorld())
        SetOpen(false);

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    PlaceDefault();
    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
