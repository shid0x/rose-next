#ifndef _ROSE_RML_TRADE_H_
#define _ROSE_RML_TRADE_H_

/**
 * UI2 player-to-player trade: replaces CExchangeDLG ( DLG_TYPE_EXCHANGE ).
 *
 * A VIEW over the hidden CExchangeDLG, which observes CExchange: its slots
 * hold the icons of both offers, and its icons, drag items and commands are
 * what this window acts through --
 *
 * - an item dragged from the bag onto the window goes on your offer ( the
 *   window is drop-target DLG_TYPE_EXCHANGE, as the classic dialog was );
 * - your item dragged off it, or double-clicked, comes back;
 * - their item can be dragged onto a private shop's wishlist, as before.
 *
 * Money: the classic window could only take money back ( the bag's money
 * button offered it ). Here your amount is set directly ( Set asks how much,
 * x clears it ); the bag's money button still adds.
 *
 * Ready and Trade, as the classic double confirmation, with two additions:
 * Ready can be taken back ( the server always had RESULT_TRADE_UNCHECK_READY;
 * the classic window never sent it ), and when the other side changes its
 * offer while you are ready, your readiness is taken back for you and the
 * window says why -- the offer you agreed to is not the one on the table.
 *
 * Closing ends the trade ( CExchangeDLG::Hide's job, which a routed close
 * skips ); Cancel or the X also tells the other side.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CSlot;
class CDragItem;
class CInfo;
class CExchangeDLG;
class CTCmdNumberInput;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlTrade {
public:
    RoseRmlTrade();
    ~RoseRmlTrade();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog. Closing ends the trade locally; it
    /// does not tell the other side ( the server-driven closes -- cancelled,
    /// done -- must not echo a cancel back ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    /// The other side changed its offer while we were ready: take our
    /// readiness back and show why. False when no UI2 trade is showing ( the
    /// caller warns the classic way ).
    bool OfferChangedWhileReady();

    void Update();

    enum Kind {
        KIND_MINE = 0,
        KIND_THEIRS = 1,
    };

    struct CellVM {
        int kind;
        int index;
        bool filled;
        Rml::String src;
        Rml::String rect;
        int count;
        int socket; ///< 0 none, 1 empty, 2 gem
        Rml::String gem_src;
        Rml::String gem_rect;

        bool operator==(const CellVM& o) const {
            return kind == o.kind && index == o.index && filled == o.filled && src == o.src
                && rect == o.rect && count == o.count && socket == o.socket
                && gem_src == o.gem_src && gem_rect == o.gem_rect;
        }
        bool operator!=(const CellVM& o) const { return !(*this == o); }
    };

private:
    CExchangeDLG* ExchangeDlg() const;
    CSlot* SlotFor(int iKind, int iIndex) const;
    CDragItem* DragItemFor(int iKind) const;
    bool IsInWorld() const;

    void SetVisible(bool bVisible);
    void Sample();
    void OnPress(int iKind, int iIndex);
    void OnUse(int iKind, int iIndex);
    void OnReady();
    void OnTrade();
    void OnCancel();
    void OnSetMoney();
    void OnClearMoney();
    void UpdateDragStart();
    void UpdateTooltip();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    /// Trade pressed: waiting for the other side's. Reset when either side
    /// is no longer ready ( the server clears it then too ).
    bool m_bDonePressed;

    /// Set My Money's command ( the quantity popup runs it ).
    CTCmdNumberInput* m_pCmdSetMoney;

    /// A press on a filled cell: a drag once it moves past the slop.
    int m_iPressKind;
    int m_iPressIndex; ///< -1: none
    int m_iPressX;
    int m_iPressY;

    /// --- bound to trade.rml -------------------------------------------------
    int m_iDropType; ///< DLG_TYPE_EXCHANGE
    Rml::String m_strMe;
    Rml::String m_strOther;
    std::vector<CellVM> m_Mine;
    std::vector<CellVM> m_Theirs;
    Rml::String m_strMyMoney;
    Rml::String m_strTheirMoney;
    bool m_bMeReady;
    bool m_bTheyReady;
    bool m_bDone; ///< bound copy of m_bDonePressed
    bool m_bChanged; ///< their offer changed while we were ready
};

#endif /// _ROSE_RML_TRADE_H_
