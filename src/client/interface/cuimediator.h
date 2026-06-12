#ifndef _UIMEDIATOR_
#define _UIMEDIATOR_

#include "CChatBox.h"
#include "CNameBox.h"
#include "CDigitEffect.h"
#include "PersonalStoreManager.h"

#include "CObjCHAR.h"

class CUIMediator {
private:
    CChatBoxManager m_ChatBoxMgr;
    CNameBox m_NameBox;
    CDigitEffect m_DigitEffect;
    CPersonalStoreManager m_PersonalStoreManager;

public:
    CUIMediator();
    ~CUIMediator();

    bool InitMediator();
    void FreeResource();

    void Update();
    void Draw();
    void DrawAvataName(CObjCHAR* pCharOBJ);

    void AddChatMsg(int iCharIndex, const char* pMsg, DWORD Color = D3DCOLOR_ARGB(255, 0, 0, 0));
    void DrawChatBox() { m_ChatBoxMgr.Draw(); };
    void CreateDamageDigit(int iPoint, float x, float y, float z, bool bIsUSER);

    void AddPersonalStoreIndex(int iObjIDX);
    void SubPersonalStoreIndex(int iObjIDX);
    void ResetPersonalStore();
};

extern CUIMediator g_UIMed;

#endif //_UIMEDIATOR_