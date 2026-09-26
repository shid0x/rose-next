#ifndef _ROSE_RML_SEPARATE_H_
#define _ROSE_RML_SEPARATE_H_

/**
 * UI2 break-down window: replaces CSeparateDlg ( DLG_TYPE_SEPARATE ).
 *
 * Opened by the break-down skill ( SKILL_POWER 41: costs MP ) or an NPC
 * ( GF_openSeparate: costs zuly, closes when you walk away ). Drop an item
 * from the bag: a socketed item gives its gem back, anything with a recipe
 * breaks into materials. The window shows what you will get and what it
 * costs; Break down sends it.
 *
 * CSeparate is the model and CSeparateDlg, kept hidden, holds the slots
 * ( icons, the drag item that takes the item back off ) and the Start checks
 * ( CSeparateDlg::Start ). The server's answer is handled where it always was
 * ( Recv_gsv_CRAFT_ITEM_REPLY ); the items gained show in a UI2 box
 * ( RoseRmlUi::ItemsBox ).
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CIcon;
class CSeparateDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlSeparate {
public:
    RoseRmlSeparate();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    struct OutVM {
        int index;
        bool filled;
        Rml::String src;
        Rml::String rect;
        Rml::String name;
        Rml::String count; ///< "x 12" for a stack, else ""

        bool operator==(const OutVM& o) const {
            return index == o.index && filled == o.filled && src == o.src && rect == o.rect
                && name == o.name && count == o.count;
        }
        bool operator!=(const OutVM& o) const { return !(*this == o); }
    };

private:
    CSeparateDlg* Dlg() const;
    CIcon* IconAt(int iKind, int iIndex) const;
    bool IsInWorld() const;

    void SetVisible(bool bVisible);
    void Sample();
    void OnPress(int iKind, int iIndex);
    void UpdateDragStart();
    void UpdateTooltip();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    bool m_bPress; ///< a left press on the item, which may become a drag
    int m_iPressX;
    int m_iPressY;

    /// --- bound to separate.rml -----------------------------------------------
    int m_iDropType; ///< DLG_TYPE_SEPARATE
    Rml::String m_strWhere; ///< "With your skill" / "At <NPC>"
    bool m_bHasItem;
    Rml::String m_strItemSrc;
    Rml::String m_strItemRect;
    Rml::String m_strItemName;
    Rml::String m_strKind; ///< what happens to it
    std::vector<OutVM> m_Outs;
    Rml::String m_strCostLabel; ///< "MP" / "Zuly"
    Rml::String m_strCost;
    Rml::String m_strHave;
    bool m_bShort; ///< not enough MP / zuly
    bool m_bBusy; ///< sent, waiting for the server
    bool m_bCanStart;
};

#endif /// _ROSE_RML_SEPARATE_H_
