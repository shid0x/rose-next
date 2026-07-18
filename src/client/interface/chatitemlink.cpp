#include "stdafx.h"

#include "chatitemlink.h"

#include "../GameCommon/Item.h"
#include "rose/io/stb.h"

namespace {

const char CHAT_ITEM_LINK_PREFIX[] = "<il:";
const int CHAT_ITEM_LINK_PREFIX_LEN = 4;
const int CHAT_ITEM_LINK_HEX_LEN = 12;

int
HexNibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

///p가 유효한 토큰의 시작('<')이면 아이템 재구성 + 검증 후 true
bool
ParseToken(const char* p, tagITEM& sOut) {
    if (strncmp(p, CHAT_ITEM_LINK_PREFIX, CHAT_ITEM_LINK_PREFIX_LEN) != 0)
        return false;

    unsigned char byBytes[6];
    for (int i = 0; i < CHAT_ITEM_LINK_HEX_LEN; i += 2) {
        int iHi = HexNibble(p[CHAT_ITEM_LINK_PREFIX_LEN + i]);
        int iLo = HexNibble(p[CHAT_ITEM_LINK_PREFIX_LEN + i + 1]);
        if (iHi < 0 || iLo < 0)
            return false;
        byBytes[i / 2] = (unsigned char)((iHi << 4) | iLo);
    }
    if (p[CHAT_ITEM_LINK_PREFIX_LEN + CHAT_ITEM_LINK_HEX_LEN] != '>')
        return false;

    return ChatItemLink_ItemFromBytes(byBytes, sOut);
}

} // namespace

std::string
ChatItemLink_Encode(tagITEM& sItem) {
    unsigned short wHeader = sItem.GetHEADER();
    unsigned int dwBody = sItem.m_dwBody;

    char szToken[CHAT_ITEM_LINK_TOKEN_LEN + 1];
    sprintf(szToken,
        "%s%02X%02X%02X%02X%02X%02X>",
        CHAT_ITEM_LINK_PREFIX,
        wHeader & 0xFF,
        (wHeader >> 8) & 0xFF,
        dwBody & 0xFF,
        (dwBody >> 8) & 0xFF,
        (dwBody >> 16) & 0xFF,
        (dwBody >> 24) & 0xFF);
    return std::string(szToken);
}

std::string
ChatItemLink_DisplayName(tagITEM& sItem) {
    const char* szName = NULL;
    if (tagBaseITEM::IsValidITEM(sItem.GetTYPE(), sItem.GetItemNO()))
        szName = ITEM_NAME(sItem.GetTYPE(), sItem.GetItemNO());
    if (szName == NULL || szName[0] == '\0')
        return std::string();

    char szDisplay[128];
    if (!tagBaseITEM::is_stackable(sItem.GetTYPE()) && sItem.GetGrade() > 0)
        _snprintf(szDisplay, sizeof(szDisplay) - 1, "[%s (%d)]", szName, sItem.GetGrade());
    else
        _snprintf(szDisplay, sizeof(szDisplay) - 1, "[%s]", szName);
    szDisplay[sizeof(szDisplay) - 1] = '\0';
    return std::string(szDisplay);
}

void
ChatItemLink_BytesFromItem(tagITEM& sItem, unsigned char* pOut6) {
    unsigned short wHeader = sItem.GetHEADER();
    unsigned int dwBody = sItem.m_dwBody;
    pOut6[0] = (unsigned char)(wHeader & 0xFF);
    pOut6[1] = (unsigned char)((wHeader >> 8) & 0xFF);
    pOut6[2] = (unsigned char)(dwBody & 0xFF);
    pOut6[3] = (unsigned char)((dwBody >> 8) & 0xFF);
    pOut6[4] = (unsigned char)((dwBody >> 16) & 0xFF);
    pOut6[5] = (unsigned char)((dwBody >> 24) & 0xFF);
}

bool
ChatItemLink_ItemFromBytes(const unsigned char* pBytes, tagITEM& sOut) {
    sOut.Clear();
    sOut.m_wHeader = (unsigned short)(pBytes[0] | (pBytes[1] << 8));
    sOut.m_dwBody = (unsigned int)pBytes[2] | ((unsigned int)pBytes[3] << 8)
        | ((unsigned int)pBytes[4] << 16) | ((unsigned int)pBytes[5] << 24);

    if (!tagBaseITEM::IsValidITEM(sOut.GetTYPE(), sOut.GetItemNO()))
        return false;

    ///이름이 비어있는 STB row( 삭제된 아이템 )는 링크로 취급하지 않는다
    const char* szName = ITEM_NAME(sOut.GetTYPE(), sOut.GetItemNO());
    if (szName == NULL || szName[0] == '\0')
        return false;

    return true;
}

int
ChatItemLink_Decode(const char* szMsg,
    char* szOutDisplay,
    int iOutCap,
    ChatItemLinkRange* pOutLinks,
    int iMaxLinks) {
    int iLinkCount = 0;
    int iOut = 0;

    if (szOutDisplay == NULL || iOutCap <= 0)
        return 0;

    const char* p = szMsg;
    while (p && *p && iOut < iOutCap - 1) {
        tagITEM sItem;
        if (*p == '<' && iLinkCount < iMaxLinks && ParseToken(p, sItem)) {
            std::string strDisplay = ChatItemLink_DisplayName(sItem);
            if (!strDisplay.empty() && iOut + (int)strDisplay.size() < iOutCap - 1) {
                if (pOutLinks) {
                    ChatItemLinkRange& sLink = pOutLinks[iLinkCount];
                    sLink.iBegin = iOut;
                    sLink.iEnd = iOut + (int)strDisplay.size();
                    sLink.dwColor = CItem::GetItemNameColor(sItem.GetTYPE(), sItem.GetItemNO());
                    ChatItemLink_BytesFromItem(sItem, sLink.Item);
                }
                memcpy(szOutDisplay + iOut, strDisplay.c_str(), strDisplay.size());
                iOut += (int)strDisplay.size();
                ++iLinkCount;
                p += CHAT_ITEM_LINK_TOKEN_LEN;
                continue;
            }
        }
        szOutDisplay[iOut++] = *p++;
    }
    szOutDisplay[iOut] = '\0';

    return iLinkCount;
}

std::string
ChatItemLink_Strip(const char* szMsg) {
    char szDisplay[512];
    ChatItemLink_Decode(szMsg, szDisplay, sizeof(szDisplay), NULL, CHAT_ITEM_LINK_MAX_PER_MSG);
    return std::string(szDisplay);
}
