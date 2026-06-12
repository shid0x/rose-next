#ifndef __CWORLDVAR_H
#define __CWORLDVAR_H

#include "nlohmann/json_fwd.hpp"

#include <cstdint>

#define WORLD_VAR "WORLD_VAR"

#define MAX_WORLD_VAR_CNT 40

#define WORLD_VAR_YEAR 1
#define WORLD_VAR_MONTH 2
#define WORLD_VAR_DAY 3
#define WORLD_VAR_TIME 4

#define WORLD_VAR_PRICES 11
#define WORLD_VAR_EXP 12
#define WORLD_VAR_DROP_I 13
#define WORLD_VAR_DROP_M 14
#define WORLD_VAR_QST_REWARD 15
#define WORLD_VAR_PRODUCT 16
#define WORLD_VAR_STAMINA 17

#define TIME_PER_YEAR 103680
#define TIME_PER_MONTH 8640

#define MONTH_PER_YEAR 12
#define DAY_PER_MONTH 54

struct tagWorldVAR {
    union {
        struct {
            uint32_t m_dwAccTIME;
            short m_nWorldVAR[MAX_WORLD_VAR_CNT];
        };
        uint8_t m_pVAR[1];
    };
};

class CWorldVAR: public tagWorldVAR {
public:
    CWorldVAR() {
        m_dwAccTIME = 0;
        std::memset(m_nWorldVAR, 0, sizeof(short) * MAX_WORLD_VAR_CNT);

        m_nWorldVAR[WORLD_VAR_DROP_M] = 100;
        m_nWorldVAR[WORLD_VAR_DROP_I] = 100;
        m_nWorldVAR[WORLD_VAR_EXP] = 100;
        m_nWorldVAR[WORLD_VAR_PRODUCT] = 100;
        m_nWorldVAR[WORLD_VAR_PRICES] = 100;
        m_nWorldVAR[WORLD_VAR_QST_REWARD] = 100;
        m_nWorldVAR[WORLD_VAR_STAMINA] = 100;
    }

    void Inc_WorldTIME(bool bWorldServer = false) {
        m_dwAccTIME++;
        if (++m_nWorldVAR[WORLD_VAR_TIME] > TIME_PER_MONTH) {
            m_nWorldVAR[WORLD_VAR_TIME] = 0;

            if (++m_nWorldVAR[WORLD_VAR_MONTH] > MONTH_PER_YEAR) {
                m_nWorldVAR[WORLD_VAR_MONTH] = 0;
                m_nWorldVAR[WORLD_VAR_YEAR]++;
            }
        }

        m_nWorldVAR[WORLD_VAR_DAY] =
            (short)(m_nWorldVAR[WORLD_VAR_TIME] / (TIME_PER_MONTH / DAY_PER_MONTH)) + 1;

        if (bWorldServer) {
            if (9 == (m_nWorldVAR[WORLD_VAR_TIME] % 10)) {
                this->Save_WorldVAR();
            }
        }
    }

    int Get_WorldVAR(short nVarIDX) { return m_nWorldVAR[nVarIDX]; }

    virtual void Set_WorldVAR(short nVarIDX, short nValue) { m_nWorldVAR[nVarIDX] = nValue; }
    virtual bool Save_WorldVAR() { return true; }

    nlohmann::json to_json() const;
    bool from_json(nlohmann::json& j);
};

#endif
