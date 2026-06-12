#include "stdafx.h"

#include "exuicommand.h"
#include "externalui/externaluilobby.h"
#include "tgamectrl/tdialog.h"
#include "../../Network/CNetwork.h"

#include "rose/common/game_types.h"

using namespace Rose::Common;

CTCmdDeleteCharacter::CTCmdDeleteCharacter(int iSelectListID, const char* pszName) {
    m_iSelectListID = iSelectListID;
    m_strName = pszName;
}

bool
CTCmdDeleteCharacter::Exec(CTObject* pObj) {
    g_pNet->Send_cli_DELETE_CHAR(m_iSelectListID, true, (char*)m_strName.c_str());
    //	g_pNet->Send_cli_CHAR_LIST();
    return true;
}

CTCmdReviveCharacter::CTCmdReviveCharacter(int iSelectListID, const char* pszName) {
    m_iSelectListID = iSelectListID;
    m_strName = pszName;
}

bool
CTCmdReviveCharacter::Exec(CTObject* pObj) {
    g_pNet->Send_cli_DELETE_CHAR(m_iSelectListID, false, (char*)m_strName.c_str());
    //	g_pNet->Send_cli_CHAR_LIST();
    return true;
}

CTCmdCreateAvatar::CTCmdCreateAvatar(const std::string& name,
    int gender_id,
    int hair_id,
    int face_id,
    int job_id) {
    this->name = name;
    this->gender_id = gender_id;
    this->hair_id = hair_id;
    this->face_id = face_id;
    this->job_id = job_id;
}

bool
CTCmdCreateAvatar::Exec(CTObject* object) {
    if (name.empty()) {
        return false;
    }
    
    g_pNet->send_char_create_req(name, face_id, hair_id, gender_from(gender_id), job_from(job_id));
    g_EUILobby.ShowWaitMsgBox();
    
    return true;
}