#include "stdafx.h"

#include "CChatBox.h"
#include "..\\Object.h"
#include "..\\Game.h"

#include "CNameBox.h"
#include "PersonalStoreManager.h"
#include "cinfo.h"
#include "../util/Localizing.h"
#include "tgamectrl/SplitHangul.h"
#include <algorithm>

#define CHATBOX_WIDTH 179
#define CHATBOX_HEIGHT_BIG 50
#define CHATBOX_HEIGHT_SMALL 30

#define CHAT_TEXT_WIDTH 28

namespace {
/// NPC and monster speech (the Wisdom Tree escort's lines, AI speech): a bubble that
/// grows with its text, word-wrapped. The player bubble's 28 characters and two lines
/// cut those sentences mid-word and dropped the rest.
const int kSpeechTextMaxWidth = 300; // px
const int kSpeechMaxLines = 6;
const int kSpeechPad = 6;
const int kSpeechMinWidth = 60;
/// CHATBOX01.TGA: a 179 x 41 panel with 3 px rounded corners, the tail below it in
/// rows 41-49 (its tip at column 63). Drawn in nine slices so it can take any size.
const int kTexW = 179;
const int kTexBodyH = 41;
const int kCorner = 3;
const int kTexTailX = 60;
const int kTexTailW = 15;
const int kTexTailH = 9;

/// How long an NPC's line stays: the player bubble's CHAT_DISPLAY_MS, and longer for a
/// line too long to read in that (about 50 ms a character past the first 30).
DWORD
SpeechDurationMs(size_t len) {
    const DWORD ms = 1500 + 50 * (DWORD)len;
    return (std::min)((std::max)(ms, CHAT_DISPLAY_MS), (DWORD)10000);
}

/// One slice of the bubble texture, scaled to w x h at (x, y).
void
DrawSlice(HNODE hTex, int sx0, int sy0, int sx1, int sy1, float x, float y, float z, float w, float h) {
    RECT rc = {sx0, sy0, sx1, sy1};
    D3DXMATRIX scale, move;
    D3DXMatrixScaling(&scale, w / (float)(sx1 - sx0), h / (float)(sy1 - sy0), 1.f);
    D3DXMatrixTranslation(&move, x, y, z);
    D3DXMATRIX mat = scale * move;
    ::setTransformSprite(mat);
    ::drawSprite(hTex, &rc, NULL, &D3DXVECTOR3(0, 0, 0), D3DCOLOR_RGBA(255, 255, 255, 255));
}
} // namespace

void
SweepChatBox(CChatBox* pBox) {
    if (pBox != NULL) {
        delete pBox;
        pBox = NULL;
    }
}

CChatBoxManager::CChatBoxManager() {
    for (int i = 0; i < CHATBOX_MAX; i++)
        m_BackGroundTex[i] = NULL;
}

CChatBoxManager::~CChatBoxManager() {
    Clear();
}

bool
CChatBoxManager::Init() {
    m_BackGroundTex[CHATBOX_SMALL] = loadTexture("3DData\\Control\\Res\\Chatbox01.tga",
        "3DData\\Control\\Res\\Chatbox01.tga",
        1,
        0);
    if (m_BackGroundTex[CHATBOX_SMALL] == NULL) {
#ifdef _DEBUG
        ::MessageBox(NULL, "Chatbox load failed !!", "File open error", MB_OK);
#endif
        //실패한 이유를 적어준다..
        return false;
    }

    m_BackGroundTex[CHATBOX_BIG] = loadTexture("3DData\\Control\\Res\\Chatbox02.tga",
        "3DData\\Control\\Res\\Chatbox02.tga",
        1,
        0);
    if (m_BackGroundTex[CHATBOX_BIG] == NULL) {
#ifdef _DEBUG
        g_pCApp->ErrorBOX("Chatbox load failed !!", "File open error");
#endif
        //실패한 이유를 적어준다..
        return false;
    }

    m_ChatBoxPool.clear();
    m_ActiveChatBoxList.clear();

    for (int i = 0; i < MAX_CHAT_BOX; i++) {
        CChatBox* pBox = new CChatBox();
        m_ChatBoxPool.push_back(pBox);
    }

    return true;
}

void
CChatBoxManager::FreeResource() {
    if (m_BackGroundTex[CHATBOX_BIG] != NULL) {
        ::unloadTexture(m_BackGroundTex[CHATBOX_BIG]);
        m_BackGroundTex[CHATBOX_BIG] = NULL;
    }

    if (m_BackGroundTex[CHATBOX_SMALL] != NULL) {
        ::unloadTexture(m_BackGroundTex[CHATBOX_SMALL]);
        m_BackGroundTex[CHATBOX_SMALL] = NULL;
    }
}

void
CChatBoxManager::Clear() {
    std::for_each(m_ChatBoxPool.begin(), m_ChatBoxPool.end(), SweepChatBox);
    std::for_each(m_ActiveChatBoxList.begin(), m_ActiveChatBoxList.end(), SweepChatBox);
}

void
CChatBoxManager::AddChat(int iCharIndex, const char* Msg, D3DCOLOR Color) {
    if (m_ChatBoxPool.empty())
        return;

    CChatBox* pBox = NULL;

    std::list<CChatBox*>::iterator begin = m_ActiveChatBoxList.begin();

    for (; begin != m_ActiveChatBoxList.end(); ++begin) {
        pBox = *begin;

        /// 이미 다른 말중이라면.. 갱신..
        if (iCharIndex == pBox->GetChatIndex()) {
            pBox->SetMember(iCharIndex, Msg, Color);
            return;
        }
    }

    pBox = m_ChatBoxPool.back();
    m_ChatBoxPool.pop_back();

    pBox->SetMember(iCharIndex, Msg, Color);
    m_ActiveChatBoxList.push_back(pBox);
}

void
CChatBoxManager::Draw() {

    // reinterpret_cast<LPD3DDEVICE>( getDevice () )->SetRenderState( D3DRS_ZWRITEENABLE, false );

    CChatBox* pBox = NULL;

    std::list<CChatBox*>::iterator begin = m_ActiveChatBoxList.begin();

    for (; begin != m_ActiveChatBoxList.end();) {
        pBox = *begin;

        if (pBox->Draw(m_BackGroundTex) == false) {
            m_ChatBoxPool.push_back(pBox);
            begin = m_ActiveChatBoxList.erase(begin);
        } else
            ++begin;
    }

    // reinterpret_cast<LPD3DDEVICE>( getDevice () )->SetRenderState( D3DRS_ZWRITEENABLE, true );
}

CChatBox::CChatBox() {
    m_iCharIndex = 0;
    m_Color = D3DCOLOR_ARGB(255, 0, 0, 0);
    m_dwShownAt = 0;
    m_bLaidOut = false;
    m_iTextW = 0;
}

CChatBox::~CChatBox() {
    // Destruct
}

void
CChatBox::SetMember(int iCharIndex, const char* Msg, DWORD Color) {
    m_iCharIndex = iCharIndex;
    SetText(Msg);

    m_Color = Color;
}

bool
CChatBox::Draw(HNODE* backTexture) {
    CObjCHAR* pCHAR = g_pObjMGR->Get_ClientCharOBJ(m_iCharIndex, false);

    if (pCHAR == NULL)
        return false;

    const bool bPlayer = pCHAR->Get_TYPE() == OBJ_USER || pCHAR->Get_TYPE() == OBJ_AVATAR;
    const DWORD dwLife = bPlayer ? CHAT_DISPLAY_MS : SpeechDurationMs(m_strText.size());
    if (::GetTickCount() - m_dwShownAt > dwLife)
        return false;

    D3DVECTOR PosSCR;
    pCHAR->GetScreenPOS(PosSCR);

    if (!bPlayer)
        return DrawSpeech(pCHAR, PosSCR, backTexture);

    CSplitHangul splitHAN((char*)m_strText.c_str(),
        CHAT_TEXT_WIDTH,
        CLocalizing::GetSingleton().GetCurrentCodePageNO());
    short nCNT = splitHAN.GetLineCount();
    int iAdjustY = 0;
    if (nCNT >= 2) {
        nCNT = 1;
        iAdjustY = 70;
    } else {
        nCNT = 0;
        iAdjustY = 50;
    }

    /// A player (NPCs and monsters went to DrawSpeech).
    float fPosY = PosSCR.y - iAdjustY - NAMEBOX_HEIGHT;

    /// The bubble's bottom sits at a fixed height, which is where the shop
    /// sign and the clan row end up once they are lifted for each other.
    /// Keep it above whichever of them is highest.
    int iBoxHeight = nCNT ? CHATBOX_HEIGHT_BIG : CHATBOX_HEIGHT_SMALL;
    float fBelowTop = ((CObjAVT*)pCHAR)->IsPersonalStoreMode()
        ? CPersonalStoreManager::GetSignTop(pCHAR, PosSCR.y)
        : CNameBox::GetPlayerNameStackTop(pCHAR, PosSCR.y);
    if (fPosY + iBoxHeight > fBelowTop)
        fPosY = fBelowTop - iBoxHeight;

    // Transform
    D3DXMATRIX mat;
    D3DXMatrixTranslation(&mat, PosSCR.x - CHATBOX_WIDTH / 2, fPosY, PosSCR.z);

    ::setTransformSprite(mat);
    ::drawSprite(backTexture[nCNT],
        NULL,
        NULL,
        &D3DXVECTOR3(0, 0, 0),
        D3DCOLOR_RGBA(255, 255, 255, 255));

    RECT rt = {4, 4, CHATBOX_WIDTH, CHATBOX_HEIGHT_BIG};
    int iCharHeight = ::getFontHeight(g_GameDATA.m_hFONT[FONT_NORMAL]);
    for (int i = 0; i <= nCNT; i++) {
        SetRect(&rt,
            4,
            4 + (iCharHeight + 2) * i,
            CHATBOX_WIDTH,
            4 + (iCharHeight + 2) * i + iCharHeight);
        if (strlen(splitHAN.GetString(i)) > 0)
            ::drawFont(g_GameDATA.m_hFONT[FONT_NORMAL],
                true,
                &rt,
                m_Color,
                DT_LEFT,
                (char*)splitHAN.GetString(i));
    }

    return true;
}

/// An NPC's or a monster's line: wrapped by words to kSpeechTextMaxWidth, the panel
/// sized to the text, the tail where the player bubble has it (29 px left of centre).
bool
CChatBox::DrawSpeech(CObjCHAR* pCHAR, const D3DVECTOR& PosSCR, HNODE* backTexture) {
    HNODE hFont = g_GameDATA.m_hFONT[FONT_NORMAL];
    if (!m_bLaidOut) {
        m_bLaidOut = true;
        m_Lines = WrapTextToWidth(m_strText.c_str(),
            hFont,
            kSpeechTextMaxWidth,
            CLocalizing::GetSingleton().GetCurrentCodePageNO());
        if ((int)m_Lines.size() > kSpeechMaxLines)
            m_Lines.resize(kSpeechMaxLines);
        m_iTextW = 0;
        for (const std::string& strLine: m_Lines)
            m_iTextW = (std::max)(m_iTextW, (int)::getFontTextExtent(hFont, strLine.c_str()).cx);
    }
    if (m_Lines.empty())
        return true;

    HNODE hTex = backTexture[CHATBOX_SMALL]; // CHATBOX01.TGA, the 50 px one
    const int iFontH = ::getFontHeight(hFont);
    const int iLineH = iFontH + 2;
    const int iW = (std::max)(m_iTextW + 2 * kSpeechPad, kSpeechMinWidth);
    const int iBodyH = (int)m_Lines.size() * iLineH - 2 + 2 * kSpeechPad;

    // the tail's tip 20 px above the head, as the player bubble; an NPC's name sits there
    float fX = floorf(PosSCR.x - iW / 2);
    float fY = floorf(PosSCR.y - (iBodyH + kTexTailH) - 20);
    if (pCHAR->Get_TYPE() == OBJ_NPC)
        fY -= NAMEBOX_HEIGHT;
    const float fZ = PosSCR.z;

    const int sxR = kTexW - kCorner, syB = kTexBodyH - kCorner;
    const float fMidW = (float)(iW - 2 * kCorner), fMidH = (float)(iBodyH - 2 * kCorner);
    const float fX1 = fX + kCorner, fX2 = fX1 + fMidW;
    const float fY1 = fY + kCorner, fY2 = fY1 + fMidH;
    DrawSlice(hTex, 0, 0, kCorner, kCorner, fX, fY, fZ, kCorner, kCorner);
    DrawSlice(hTex, kCorner, 0, sxR, kCorner, fX1, fY, fZ, fMidW, kCorner);
    DrawSlice(hTex, sxR, 0, kTexW, kCorner, fX2, fY, fZ, kCorner, kCorner);
    DrawSlice(hTex, 0, kCorner, kCorner, syB, fX, fY1, fZ, kCorner, fMidH);
    DrawSlice(hTex, kCorner, kCorner, sxR, syB, fX1, fY1, fZ, fMidW, fMidH);
    DrawSlice(hTex, sxR, kCorner, kTexW, syB, fX2, fY1, fZ, kCorner, fMidH);
    DrawSlice(hTex, 0, syB, kCorner, kTexBodyH, fX, fY2, fZ, kCorner, kCorner);
    DrawSlice(hTex, kCorner, syB, sxR, kTexBodyH, fX1, fY2, fZ, fMidW, kCorner);
    DrawSlice(hTex, sxR, syB, kTexW, kTexBodyH, fX2, fY2, fZ, kCorner, kCorner);
    DrawSlice(hTex,
        kTexTailX,
        kTexBodyH,
        kTexTailX + kTexTailW,
        kTexBodyH + kTexTailH,
        floorf(fX + iW / 2 - 29),
        fY + iBodyH,
        fZ,
        kTexTailW,
        kTexTailH);

    D3DXMATRIX mat;
    D3DXMatrixTranslation(&mat, fX, fY, fZ);
    ::setTransformSprite(mat);
    for (size_t i = 0; i < m_Lines.size(); i++) {
        RECT rt;
        SetRect(&rt,
            kSpeechPad,
            kSpeechPad + iLineH * (int)i,
            iW,
            kSpeechPad + iLineH * (int)i + iFontH);
        ::drawFont(hFont, true, &rt, m_Color, DT_LEFT, (char*)m_Lines[i].c_str());
    }
    return true;
}
