#ifndef _ROSE_RML_DAMAGE_METER_H_
#define _ROSE_RML_DAMAGE_METER_H_

/**
 * Damage meter view, built on RmlUi.
 *
 * Phase 2 of doc/rmlui-evaluation.md: the same data as CDamageMeterPanel, drawn
 * from damagemeter.rml + .rcss instead of hard-coded C++ constants.
 *
 * CDamageMeter ( the data core ) is NOT touched. It stays a read-only observer
 * of the authoritative combat event stream; this is purely a second consumer of
 * BuildSnapshot(), exactly as the legacy panel is. Both can coexist -- the INI
 * toggle decides which one /dps opens.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

#include "../gamedata/CDamageMeter.h"

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

class RoseRmlDamageMeter {
public:
    RoseRmlDamageMeter();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void Toggle();
    void Show();
    void Hide();
    bool IsVisible() const {
        return m_bVisible;
    }

    /// Re-aggregates and pushes to the data model. Rate-limited internally.
    void Update();

private:
    /// One display row, flattened for the data model. Mirrors what the RCSS
    /// needs and nothing more -- the view never sees CDamageMeter::Row.
    struct RowVM {
        Rml::String name;
        Rml::String value; ///< preformatted "184,220 (2192/s, 46%)"
        float pct;         ///< bar width, 0..100 relative to the top row
        bool is_self;
    };

    void RebuildRows();
    void CycleView();
    void ResetData();

    static Rml::String FormatThousands(__int64 value);

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::DataModelHandle m_Model;

    /// --- bound data ---------------------------------------------------------
    Rml::String m_strTitle;
    Rml::String m_strFight;
    Rml::String m_strFooter;
    bool m_bLive;
    std::vector<RowVM> m_Rows;

    int m_iView; ///< matches CDamageMeterPanel's VIEW_* ordering
    bool m_bVisible;
    DWORD m_dwLastRefresh;
    CDamageMeter::FightSnapshot m_Snapshot;
};

#endif /// _ROSE_RML_DAMAGE_METER_H_
