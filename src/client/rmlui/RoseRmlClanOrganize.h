#ifndef _ROSE_RML_CLAN_ORGANIZE_H_
#define _ROSE_RML_CLAN_ORGANIZE_H_

/**
 * UI2 found-a-clan window: replaces CClanOrganizeDlg ( DLG_TYPE_CLAN_ORGANIZE,
 * opened by the clan manager's GF_organizeClan ).
 *
 * The name ( 16 characters ) and slogan ( 64 ), the emblem picked from the
 * two sprite sheets ( a background and a symbol, stacked in the preview,
 * random to start with as in the classic dialog ), and the two conditions --
 * level 30 and 1,000,000 zuly -- shown up front with whether you meet them.
 * Found runs CClanOrganizeDlg::RequestOrganize, the classic checks and
 * request; the server's answer closes the window ( RESULT_CLAN_CREATE_OK ).
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

class RoseRmlClanOrganize {
public:
    RoseRmlClanOrganize();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    struct MarkVM {
        int index; ///< sprite index in its sheet
        Rml::String src;
        Rml::String rect;
        bool on;

        bool operator==(const MarkVM& o) const {
            return index == o.index && src == o.src && rect == o.rect && on == o.on;
        }
        bool operator!=(const MarkVM& o) const { return !(*this == o); }
    };

private:
    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void BuildSheets();
    void Pick(bool bBack, int iIndex);
    void Sample();
    void OnFound();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    bool m_bFocusName; ///< put the caret in the name field once shown

    /// --- bound to clanorganize.rml -------------------------------------------
    std::vector<MarkVM> m_Backs;
    std::vector<MarkVM> m_Centers;
    int m_iBack;
    int m_iCenter;
    Rml::String m_strBackSrc;
    Rml::String m_strBackRect;
    Rml::String m_strCenterSrc;
    Rml::String m_strCenterRect;
    int m_iMyLevel;
    Rml::String m_strMyMoney;
    bool m_bLevelOk;
    bool m_bMoneyOk;
};

#endif /// _ROSE_RML_CLAN_ORGANIZE_H_
