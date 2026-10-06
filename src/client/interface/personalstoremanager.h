#ifndef _PERSONAL_STORE_MANAGER_
#define _PERSONAL_STORE_MANAGER_

#include <list>

//----------------------------------------------------------------------------------------------------
/// class CPersonalStoreManager
/// 개인상점 타이틀을 관리&Draw 하기 위한 클래스
//----------------------------------------------------------------------------------------------------

class CObjCHAR;

class CPersonalStoreManager {
private:
    std::list<int> m_PersonalStoreList;
    HNODE m_ShopTitleBox;
    int m_iShopTitleBoxWidth;
    int m_iShopTitleBoxHeight;

    /// Height of the sign texture, for callers without the instance (speech bubbles).
    static int s_iSignHeight;

public:
    CPersonalStoreManager(void);
    ~CPersonalStoreManager(void);

    bool Init();
    void FreeResource();
    void ClearList();

    void AddStoreList(int iClientObjIDX) { m_PersonalStoreList.push_back(iClientObjIDX); }
    void SubStoreList(int iClientObjIDX) { m_PersonalStoreList.remove(iClientObjIDX); }

    void Draw();

    /// Screen y of the top of the shop sign over a player anchored at screen y:
    /// 70 px up, or higher when the name box (name + clan row) reaches past that.
    static float GetSignTop(CObjCHAR* pChar, float y);
};

#endif /// _PERSONAL_STORE_MANAGER_