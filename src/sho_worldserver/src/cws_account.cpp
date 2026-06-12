#include "stdAFX.h"

#include "CWS_Account.h"
#include "CWS_Client.h"
#include "SHO_WS_LIB.h"
#include "WS_SocketLSV.h"

CWS_Account::~CWS_Account() {
    if (0 != m_btLoginBIT) {
        const char* szAccount = this->m_Account.Get() ? this->m_Account.Get() : "unknown user";
        LOG_WARN("Invalid logout process found for {}", szAccount);
    }
}