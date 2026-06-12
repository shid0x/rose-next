#include "stdafx.h"

#include "CUIMediator.h"
#include "..\\Game.h"

#include "System/CGame.h"

CUIMediator g_UIMed;

CUIMediator::CUIMediator() {}

CUIMediator::~CUIMediator() {
    // Destruct
}

bool
CUIMediator::InitMediator() {
    setDelayedLoad(0);

    if (m_ChatBoxMgr.Init() == false)
        return false;

    if (m_DigitEffect.Init() == false)
        return false;

    if (m_PersonalStoreManager.Init() == false)
        return false;

    setDelayedLoad(1);

    return true;
}

void
CUIMediator::FreeResource() {
    m_ChatBoxMgr.FreeResource();

    m_DigitEffect.ClearAll();
    m_PersonalStoreManager.FreeResource();
}

void
CUIMediator::DrawAvataName(CObjCHAR* pCharOBJ) {
    if (pCharOBJ == NULL)
        return;

    D3DVECTOR PosSCR;

    int iType = pCharOBJ->Get_TYPE();
    if (iType == OBJ_MOB) {
        int a = 10;
    }
    pCharOBJ->GetScreenPOS(PosSCR);

    if (PosSCR.x < 0 || PosSCR.y < 0)
        return;

    if (PosSCR.x > g_pCApp->GetWIDTH() || PosSCR.y > g_pCApp->GetHEIGHT())
        return;

    m_NameBox.Draw(pCharOBJ, PosSCR.x, PosSCR.y, PosSCR.z);
}

void
CUIMediator::CreateDamageDigit(int iPoint, float x, float y, float z, bool bIsUSER) {
    m_DigitEffect.CreateDigitEffect(iPoint, x, y, z, bIsUSER);
}

void
CUIMediator::Update() {
    m_DigitEffect.Refresh();
}

void
CUIMediator::Draw() {
    DrawChatBox();
    m_PersonalStoreManager.Draw();
}

void
CUIMediator::AddChatMsg(int iCharIndex, const char* pMsg, DWORD Color) {
    m_ChatBoxMgr.AddChat(iCharIndex, pMsg, Color);
}

void
CUIMediator::AddPersonalStoreIndex(int iClientObjIDX) {
    m_PersonalStoreManager.AddStoreList(iClientObjIDX);
}

void
CUIMediator::SubPersonalStoreIndex(int iClientObjIDX) {
    m_PersonalStoreManager.SubStoreList(iClientObjIDX);
}

void
CUIMediator::ResetPersonalStore() {
    m_PersonalStoreManager.ClearList();
}