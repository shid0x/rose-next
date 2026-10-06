#include "stdafx.h"

#include ".\personalstoremanager.h"

#include "../CObjCHAR.h"
#include "../Object.h"
#include "CNameBox.h"

#include "tgamectrl/SplitHangul.h"

const int TITLE_TEXT_WIDTH = 28;

const int TITLE_BOX_WIDTH = 200;
const int TITLE_BOX_HEIGHT = 20;

int CPersonalStoreManager::s_iSignHeight = 32;

//----------------------------------------------------------------------------------------------------
///
//----------------------------------------------------------------------------------------------------

CPersonalStoreManager::CPersonalStoreManager(void) {}

CPersonalStoreManager::~CPersonalStoreManager(void) {}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 리소스 로딩..
//----------------------------------------------------------------------------------------------------

bool
CPersonalStoreManager::Init() {
    m_ShopTitleBox = loadTexture("3DData\\Control\\Res\\shopbox01.dds",
        "3DData\\Control\\Res\\shopbox01.dds",
        1,
        0);
    if (m_ShopTitleBox == NULL) {
#ifdef _DEBUG
        ::MessageBox(NULL, "ShopTitleBox load failed !!", "File open error", MB_OK);
#endif
        //실패한 이유를 적어준다..
        return false;
    }

    getTextureSize(m_ShopTitleBox, m_iShopTitleBoxWidth, m_iShopTitleBoxHeight);
    s_iSignHeight = m_iShopTitleBoxHeight;

    return true;
}

float
CPersonalStoreManager::GetSignTop(CObjCHAR* pChar, float y) {
    /// The sign sits 70 px up, which clears the name but not the clan row
    /// stacked above it, so lift it over whatever the name box draws.
    float fSignY = y - 70;
    float fStackTop = CNameBox::GetPlayerNameStackTop(pChar, y);
    if (fSignY + s_iSignHeight > fStackTop)
        fSignY = fStackTop - s_iSignHeight;

    return fSignY;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 모든 리소스 해제
//-----------------------------
//-----------------------------------------------------------------------

void
CPersonalStoreManager::FreeResource() {
    if (m_ShopTitleBox != NULL) {
        ::unloadTexture(m_ShopTitleBox);
        m_ShopTitleBox = NULL;
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 모든 리스트 삭제
//----------------------------------------------------------------------------------------------------

void
CPersonalStoreManager::ClearList() {
    m_PersonalStoreList.clear();
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

void
CPersonalStoreManager::Draw() {
    int iObjIDX = 0;
    std::list<int>::iterator begin = m_PersonalStoreList.begin();

    for (; begin != m_PersonalStoreList.end(); ++begin) {
        iObjIDX = *begin;
        CObjAVT* pAVT = g_pObjMGR->Get_CharAVT(iObjIDX, true);
        if (pAVT) {
            D3DVECTOR PosSCR;
            pAVT->GetScreenPOS(PosSCR);

            // Transform
            D3DXMATRIX mat;
            D3DXMatrixTranslation(&mat,
                PosSCR.x - m_iShopTitleBoxWidth / 2,
                GetSignTop(pAVT, PosSCR.y),
                PosSCR.z);

            ::setTransformSprite(mat);
            ::drawSprite(m_ShopTitleBox,
                NULL,
                NULL,
                &D3DXVECTOR3(0, 0, 0),
                D3DCOLOR_RGBA(255, 255, 255, 255));

            if (const char* pszTitle = pAVT->GetPersonalStoreTitle()) {

                SIZE size = getFontTextExtent(g_GameDATA.m_hFONT[FONT_NORMAL], pszTitle);

                RECT rt = {0,
                    m_iShopTitleBoxHeight / 2 - size.cy / 2,
                    m_iShopTitleBoxWidth,
                    m_iShopTitleBoxHeight / 2 + size.cy / 2};
                ::drawFont(g_GameDATA.m_hFONT[FONT_NORMAL],
                    true,
                    &rt,
                    g_dwYELLOW,
                    DT_CENTER,
                    pszTitle);
            }
        }
    }
}