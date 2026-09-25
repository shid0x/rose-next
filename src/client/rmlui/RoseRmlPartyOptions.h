#ifndef _ROSE_RML_PARTY_OPTIONS_H_
#define _ROSE_RML_PARTY_OPTIONS_H_

/**
 * UI2 party options: replaces CPartyOptionDlg ( DLG_TYPE_PARTYOPTION ).
 *
 * A window opened from the party frames' Options button ( routed through
 * IT_MGR like every UI2 window ). How EXP is shared, how loot is claimed,
 * and whether members show an HP gauge over their heads.
 *
 * The legacy dialog made the leader tick options and press OK; here a
 * choice applies as it is clicked. Only the leader can change the party's
 * rules ( the server's rule too ); everyone sees them, and the HP gauge
 * is each player's own setting. The hidden CPartyOptionDlg still writes the
 * "party settings changed" chat lines, as CParty's observer.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlPartyOptions {
public:
    RoseRmlPartyOptions();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// The player's intent ( OpenDialog / CloseDialog ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    /// Where to open: beside this element ( the party frames ).
    void SetAnchor(Rml::Element* pAnchor) { m_pAnchor = pAnchor; }

    void Update();

private:
    void SetVisible(bool bVisible);
    void Sample();
    void PlaceBesideAnchor();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::Element* m_pAnchor;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    /// Frames left to re-place beside the anchor after opening: the window's
    /// size is only known once RmlUi has laid it out.
    int m_iPlaceFrames;

    /// --- bound to partyoptions.rml -----------------------------------------
    bool m_bLeader; ///< you can change the rules
    bool m_bExpByLevel; ///< BIT_PARTY_RULE_EXP_PER_PLAYER
    bool m_bLootInTurn; ///< BIT_PARTY_RULE_ITEM_TO_ORDER
    bool m_bHpGauge; ///< CClientStorage: members' overhead HP gauge
};

#endif /// _ROSE_RML_PARTY_OPTIONS_H_
