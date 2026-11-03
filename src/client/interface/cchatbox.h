#ifndef _CHATBOX_
#define _CHATBOX_

#include <string>
#include <list>
#include <vector>

/// How long a player's bubble stays, in milliseconds (2026-11-03). It counted 270 frames:
/// 4.5 s at the 60 FPS the game was made for, under 1.5 s at today's frame rates.
const DWORD CHAT_DISPLAY_MS = 3000;
const int MAX_CHAT_BOX = 100;

class CChatBox;

enum {
    CHATBOX_BIG = 0,
    CHATBOX_SMALL,
    CHATBOX_MAX,
};

/// Chatbox manager
class CChatBoxManager {
private:
    std::list<CChatBox*> m_ChatBoxPool;
    std::list<CChatBox*> m_ActiveChatBoxList;

    /// Background
    HNODE m_BackGroundTex[CHATBOX_MAX];

public:
    CChatBoxManager();
    ~CChatBoxManager();

    bool Init();
    void FreeResource();
    void Clear();

    void AddChat(int iCharIndex, const char* Msg, DWORD Color);

    void Draw();
};

/// Avata 머리위 챗 박스
class CChatBox {
private:
    int m_iCharIndex;

    /// 챗 대화..
    std::string m_strText;
    DWORD m_Color;

    /// When the line was set (it stays CHAT_DISPLAY_MS, an NPC's line longer when long).
    DWORD m_dwShownAt;

    /// NPC and monster speech (DrawSpeech): its layout, wrapped once on the first draw.
    bool m_bLaidOut;
    std::vector<std::string> m_Lines;
    int m_iTextW;

    bool DrawSpeech(class CObjCHAR* pCHAR, const D3DVECTOR& PosSCR, HNODE* backTexture);

public:
    CChatBox();
    ~CChatBox();

    int GetChatIndex() { return m_iCharIndex; }

    void SetMember(int iCharIndex, const char* Msg, DWORD Color = 0);

    void SetText(const char* pStr) { this->SetText(std::string(pStr)); }
    void SetText(std::string str) {
        m_strText = str;
        m_dwShownAt = ::GetTickCount();
        m_bLaidOut = false;
        m_Lines.clear();
    }

    bool Draw(HNODE* backTexture);
};

#endif //_CHATBOX_
