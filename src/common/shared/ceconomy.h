#pragma once

#include "CItem.h"

struct tagECONOMY {
    // 입력 데이터...
    union {
        struct {
#if defined(__SERVER) || defined(__VIRTUAL_SERVER)
            DWORD m_dwTown_COUNTER;

            short m_nTown_POP_BASE;
            short m_nTown_DEV_BASE;
            short m_nTown_CONSUM[MAX_PRICE_TYPE];

            short m_nTownDEV;
            int m_iTownPOP;
            int m_iTownITEM[MAX_PRICE_TYPE];

            DWORD m_dwCheckTIME;
#endif
            DWORD m_dwUpdateTIME;
            union {
                struct {
                    BYTE m_btTOWN_RATE;
                    BYTE m_btItemRATE[MAX_PRICE_TYPE];
                    short m_nCur_WorldPROD;
                    short m_nCur_WorldRATE;
                };
                BYTE m_btCur_DATA[MAX_PRICE_TYPE + sizeof(BYTE) + sizeof(short) * 2];
            };
        };

        BYTE m_pEconomy[1];
    };
};

class CEconomy: public tagECONOMY {
private:
    int m_iTownCounter;

public:
    // 마을 물가...
    static bool IsEssentialGoods(int iItemTYPE);

    CEconomy();
    ~CEconomy();

    void Init(void);

    void SetTownRATE(int iValue);
    void SetItemRATE(int iPriceType, int iValue);

    BYTE Get_TownRATE(void) { return m_btTOWN_RATE; }
    int Get_ItemRATE(short nItemTYPE) {
        return nItemTYPE < MAX_PRICE_TYPE ? m_btItemRATE[nItemTYPE] : -1;
    }

    int Get_ItemBuyPRICE(short nItemTYPE, short nItemNO, short nBuySkillVALUE);
    int Get_ItemSellPRICE(tagITEM& sITEM, short nSellSkillVALUE);

    int Get_EconomyVAR(short nVarIDX) {
        switch (nVarIDX) {
            case 1:
                return Get_TownRATE();
#if defined(__SERVER) || defined(__VIRTUAL_SERVER)
            case 2:
                return m_iTownPOP;
            case 3:
                return m_nTownDEV;
#endif
        }

#if defined(__SERVER) || defined(__VIRTUAL_SERVER)
        if (nVarIDX >= 11 && nVarIDX <= 30) {
            if (nVarIDX <= 20) {
                // 보유량
                return m_iTownITEM[MIN_PRICE_TYPE + nVarIDX - 11];
            } else {
                // 물가..
                return m_btItemRATE[MIN_PRICE_TYPE + nVarIDX - 21];
            }
        }
#endif
        return 0;
    }

#if defined(__SERVER) || defined(__VIRTUAL_SERVER)
    #ifdef __SERVER
    bool Load(FILE* fp);
    #else
    bool Load(CFileSystem* pFileSystem);
    #endif

    void BuyITEM(tagITEM& sITEM);
    void SellITEM(tagITEM& sITEM, int iQuantity);

    int Get_TownITEM(short nItemTYPE) {
        return nItemTYPE < MAX_PRICE_TYPE ? m_iTownITEM[nItemTYPE] : -1;
    }

    bool Proc(DWORD dwCurTIME);
#endif
};

extern short Get_WorldRATE();
extern void Set_WorldRATE(short nWorldRate);

extern short Get_WorldPROD();
extern void Set_WorldPROD(short nWorldProd);

void to_json(nlohmann::json& j, const tagECONOMY& e);
void from_json(const nlohmann::json& j, tagECONOMY& s);
