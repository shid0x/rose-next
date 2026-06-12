#ifndef __CWS_ACCOUNT_H
#define __CWS_ACCOUNT_H
//-------------------------------------------------------------------------------------------------

#define BIT_LOGIN_WS 0x01
#define BIT_LOGIN_GS 0x02

// #define	__SHOW_USER_LISTVIEW

class CWS_Client;

class CWS_Account {
private:
public:
    DWORD m_dwLSID;
    DWORD m_dwWSID;
    DWORD m_dwGSID;

    DWORD m_dwLoginTIME;
    BYTE m_btChannelNO;

    BYTE m_btLoginBIT;

    CStrVAR m_Account;
    CWS_Client* m_pCLIENT;

    CWS_Account() {
        m_btLoginBIT = 0;
        m_pCLIENT = NULL;
    }
    ~CWS_Account();

    void
    Add_WSBit(CWS_Client* pClient, char* szAccount, DWORD dwLSID, DWORD dwWSID, DWORD dwLoginTime) {
        m_pCLIENT = pClient;
        m_Account.Set(szAccount);
        m_dwLSID = dwLSID;
        m_dwWSID = dwWSID;
        m_dwLoginTIME = dwLoginTime;

        m_btLoginBIT |= BIT_LOGIN_WS;
    }
    void Add_GSBit(DWORD dwGSID) {
        // GS에서 인증 요청오면 ...
        m_dwGSID = dwGSID;
        m_btLoginBIT |= BIT_LOGIN_GS;
    }
    BYTE Sub_LoginBIT(BYTE btBIT) {
        m_btLoginBIT &= ~btBIT;
        return m_btLoginBIT;
    }

    char* Get_ACCOUNT() { return m_Account.Get(); }
};

#endif