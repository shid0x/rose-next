#ifndef _CHATITEMLINK_H_
#define _CHATITEMLINK_H_

#include <string>

#include "citem.h"

/// Chat item links: an item is carried in chat text as the wire token
/// "<il:XXXXXXXXXXXX>" — 12 uppercase hex chars = the 6 packed bytes of
/// tagBaseITEM (header word LE + body dword LE), 17 chars total.
/// The receiver reconstructs name/color/tooltip locally from the STBs, so
/// the name is never transmitted and cannot be spoofed.

/// One decoded inline link inside a chat display string.
struct ChatItemLinkRange {
    int iBegin; ///char offset in the display string (inclusive)
    int iEnd; ///char offset (exclusive)
    DWORD dwColor; ///item rarity name color
    unsigned char Item[6]; ///packed tagBaseITEM wire bytes
};

enum {
    CHAT_ITEM_LINK_MAX_PER_MSG = 3,
    CHAT_ITEM_LINK_TOKEN_LEN = 17,
    ///서버 Recv_cli_CHAT/SHOUT 는 129 byte 초과를 IS_HACKING 처리한다 — 절대 초과 금지
    CHAT_MSG_WIRE_MAX = 129,
};

///"<il:HEX12>" wire token for an item
std::string ChatItemLink_Encode(tagITEM& sItem);

///"[Name]" / "[Name (g)]" — the display form, also inserted into the edit box
std::string ChatItemLink_DisplayName(tagITEM& sItem);

///rebuild + validate an item from the 6 wire bytes (STB bounds + name check)
bool ChatItemLink_ItemFromBytes(const unsigned char* pBytes, tagITEM& sOut);

///pack an item into its 6 wire bytes (header LE + body LE)
void ChatItemLink_BytesFromItem(tagITEM& sItem, unsigned char* pOut6);

///scan szMsg for tokens; writes the display string (tokens replaced by display
///names) and up to iMaxLinks decoded ranges. Invalid tokens stay verbatim text.
///Returns the number of links decoded.
int ChatItemLink_Decode(const char* szMsg,
    char* szOutDisplay,
    int iOutCap,
    ChatItemLinkRange* pOutLinks,
    int iMaxLinks);

///tokens -> plain "[Name]" (for overhead speech bubbles)
std::string ChatItemLink_Strip(const char* szMsg);

#endif ///_CHATITEMLINK_H_
