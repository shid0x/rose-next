#include "stdAFX.h"

#include "CThreadGUILD.h"
#include "CWS_Client.h"
#include "rose/io/stb.h"
#include "WS_ThreadSQL.h"
#include "WS_ZoneLIST.h"

#include "rose/common/game_config.h"
#include "rose/common/game_types.h"
#include "rose/common/util.h"
#include "rose/database/database.h"

#include "nlohmann/json.hpp"

#include <regex>

using namespace Rose;
using namespace Rose::Common;
using namespace Rose::Database;
using namespace Rose::Network;
using namespace Rose::Util;

using json = nlohmann::json;

CWS_ThreadSQL::CWS_ThreadSQL(): CSqlTHREAD(true) {
    COMPILE_TIME_ASSERT(sizeof(tagGrowAbility) <= 384);
    COMPILE_TIME_ASSERT(sizeof(tagGrowAbility) == 383);
    COMPILE_TIME_ASSERT(MAX_RIDING_PART == 5);

    m_pDefaultBE = NULL;
    m_pDefaultINV = NULL;

    m_nDefaultDataCNT = g_TblAVATAR.row_count;
    if (m_nDefaultDataCNT > 0) {
        m_pDefaultBE = new tagBasicETC[m_nDefaultDataCNT];
        m_pDefaultINV = new CInventory[m_nDefaultDataCNT];
        m_pDefaultBA = new tagBasicAbility[m_nDefaultDataCNT];

        short nR, nJ;
        for (nR = 0; nR < m_nDefaultDataCNT; nR++) {
            m_pDefaultBA[nR].m_nSTR = AVATAR_STR(nR);
            m_pDefaultBA[nR].m_nDEX = AVATAR_DEX(nR);
            m_pDefaultBA[nR].m_nINT = AVATAR_INT(nR);
            m_pDefaultBA[nR].m_nCON = AVATAR_CON(nR);
            m_pDefaultBA[nR].m_nCHARM = AVATAR_CHARM(nR);
            m_pDefaultBA[nR].m_nSENSE = AVATAR_SENSE(nR);

            m_pDefaultBE[nR].Init();
            m_pDefaultBE[nR].m_nZoneNO = AVATAR_ZONE(nR);

            _ASSERT(m_pDefaultBE[nR].m_nZoneNO > 0);

            m_pDefaultINV[nR].Clear();
            m_pDefaultINV[nR].m_i64Money = AVATAR_MONEY(nR);

            // 초기 장작 아이템...
            m_pDefaultINV[nR].SetInventory(EQUIP_IDX_FACE_ITEM, AVATAR_FACEITEM(nR));
            m_pDefaultINV[nR].SetInventory(EQUIP_IDX_HELMET, AVATAR_HELMET(nR));
            m_pDefaultINV[nR].SetInventory(EQUIP_IDX_ARMOR, AVATAR_ARMOR(nR));
            m_pDefaultINV[nR].SetInventory(EQUIP_IDX_KNAPSACK, AVATAR_BACKITEM(nR));
            m_pDefaultINV[nR].SetInventory(EQUIP_IDX_GAUNTLET, AVATAR_GAUNTLET(nR));
            m_pDefaultINV[nR].SetInventory(EQUIP_IDX_BOOTS, AVATAR_BOOTS(nR));
            m_pDefaultINV[nR].SetInventory(EQUIP_IDX_WEAPON_R, AVATAR_WEAPON(nR));
            m_pDefaultINV[nR].SetInventory(EQUIP_IDX_WEAPON_L, AVATAR_SUBWPN(nR));

            // 초기 장비 아이템
            for (nJ = 0; nJ < 10; nJ++)
                m_pDefaultINV[nR].SetInventory(
                    (INV_WEAPON * INVENTORY_PAGE_SIZE) + nJ + MAX_EQUIP_IDX,
                    AVATAR_ITEM_WEAPON(nR, nJ));

            // 초기 소모 아이템
            for (nJ = 0; nJ < 5; nJ++) {
                m_pDefaultINV[nR].SetInventory((INV_USE * INVENTORY_PAGE_SIZE) + nJ + MAX_EQUIP_IDX,
                    AVATAR_ITEM_USE(nR, nJ),
                    AVATAR_ITEM_USECNT(nR, nJ));
            }

            // 초기 기타 아이템
            for (nJ = 0; nJ < 5; nJ++) {
                m_pDefaultINV[nR].SetInventory((INV_ETC * INVENTORY_PAGE_SIZE) + nJ + MAX_EQUIP_IDX,
                    AVATAR_ITEM_ETC(nR, nJ),
                    AVATAR_ITEM_ETCCNT(nR, nJ));
            }

            //	m_pDefaultBE[ nR ].m_nPartItemIDX[ BODY_PART_FACE		] = m_pDefaultINV[ nR
            //].m_ItemEQUIP[ nI ].m_nItemNo; 	m_pDefaultBE[ nR ].m_nPartItemIDX[ BODY_PART_HAIR
            //] = m_pDefaultINV[ nR ].m_ItemEQUIP[ nI ].m_nItemNo;
            m_pDefaultBE[nR].SetPartITEM(BODY_PART_HELMET,
                m_pDefaultINV[nR].m_ItemEQUIP[EQUIP_IDX_HELMET]);
            m_pDefaultBE[nR].SetPartITEM(BODY_PART_ARMOR,
                m_pDefaultINV[nR].m_ItemEQUIP[EQUIP_IDX_ARMOR]);
            m_pDefaultBE[nR].SetPartITEM(BODY_PART_GAUNTLET,
                m_pDefaultINV[nR].m_ItemEQUIP[EQUIP_IDX_GAUNTLET]);
            m_pDefaultBE[nR].SetPartITEM(BODY_PART_BOOTS,
                m_pDefaultINV[nR].m_ItemEQUIP[EQUIP_IDX_BOOTS]);
            m_pDefaultBE[nR].SetPartITEM(BODY_PART_FACE_ITEM,
                m_pDefaultINV[nR].m_ItemEQUIP[EQUIP_IDX_FACE_ITEM]);
            m_pDefaultBE[nR].SetPartITEM(BODY_PART_KNAPSACK,
                m_pDefaultINV[nR].m_ItemEQUIP[EQUIP_IDX_KNAPSACK]);
            m_pDefaultBE[nR].SetPartITEM(BODY_PART_WEAPON_R,
                m_pDefaultINV[nR].m_ItemEQUIP[EQUIP_IDX_WEAPON_R]);
            m_pDefaultBE[nR].SetPartITEM(BODY_PART_WEAPON_L,
                m_pDefaultINV[nR].m_ItemEQUIP[EQUIP_IDX_WEAPON_L]);
        }
    }
}
__fastcall CWS_ThreadSQL::~CWS_ThreadSQL() {
    SAFE_DELETE_ARRAY(m_pDefaultBA);
    SAFE_DELETE_ARRAY(m_pDefaultBE);
    SAFE_DELETE_ARRAY(m_pDefaultINV);
}

void
CWS_ThreadSQL::Execute() {
    this->SetPriority(THREAD_PRIORITY_ABOVE_NORMAL); // Priority 1 point above the priority class

    CDLList<tagQueryDATA>::tagNODE* pSqlNODE;

    while (TRUE) {
        if (!this->Terminated) {
            m_pEVENT->WaitFor(INFINITE);
        } else {
            int iReaminCNT;
            this->m_CS.Lock();
            iReaminCNT = m_AddPACKET.GetNodeCount();
            this->m_CS.Unlock();

            if (iReaminCNT <= 0)
                break;
        }

        this->m_CS.Lock();
        m_RunPACKET.AppendNodeList(&m_AddPACKET);
        m_AddPACKET.Init();
        m_pEVENT->ResetEvent();
        this->m_CS.Unlock();

        for (pSqlNODE = m_RunPACKET.GetHeadNode(); pSqlNODE;) {
            if (Run_SqlPACKET(&pSqlNODE->m_VALUE))
                pSqlNODE = this->Del_SqlPACKET(pSqlNODE);
            else
                pSqlNODE = m_RunPACKET.GetNextNode(pSqlNODE);
        }

        this->tick();
    }

    int iCnt = m_AddPACKET.GetNodeCount();
    _ASSERT(iCnt == 0);
}

bool
CWS_ThreadSQL::Add_SqlPacketWithACCOUNT(CWS_Client* pUSER, t_PACKET* pPacket) {
    return CSqlTHREAD::Add_SqlPACKET((int)pUSER->m_iSocketIDX,
        pUSER->Get_ACCOUNT(),
        (BYTE*)pPacket,
        pPacket->m_HEADER.m_nSize);
}

bool
CWS_ThreadSQL::Add_SqlPacketWithAVATAR(CWS_Client* pUSER, t_PACKET* pPacket) {
    return CSqlTHREAD::Add_SqlPACKET((int)pUSER->m_iSocketIDX,
        pUSER->Get_NAME(),
        (BYTE*)pPacket,
        pPacket->m_HEADER.m_nSize);
}

bool
CWS_ThreadSQL::Run_SqlPACKET(tagQueryDATA* pSqlPACKET) {
    switch (pSqlPACKET->m_pPacket->m_wType) {
        case CLI_CHAR_LIST:
            Proc_cli_CHAR_LIST(pSqlPACKET);
            break;

        case CLI_SELECT_CHAR:
            Proc_cli_SELECT_CHAR(pSqlPACKET);
            break;

        case CLI_DELETE_CHAR:
            Proc_cli_DELETE_CHAR(pSqlPACKET);
            break;

        case CLI_MEMO:
            Proc_cli_MEMO(pSqlPACKET);
            break;

        case SQL_ZONE_DATA: {
            sql_ZONE_DATA* pSqlZONE = (sql_ZONE_DATA*)pSqlPACKET->m_pPacket;
            switch (pSqlZONE->m_btDataTYPE) {
                case SQL_ZONE_DATA_WORLDVAR_SAVE:
                    Proc_SAVE_WORLDVAR(pSqlZONE);
                    break;
            }
            break;
        }

        default:
            g_LOG.CS_ODS(0xffff,
                "Undefined sql packet Type: %x, Size: %d \n",
                pSqlPACKET->m_pPacket->m_wType,
                pSqlPACKET->m_pPacket->m_nSize);
    }

    return true;
}

bool
CWS_ThreadSQL::Proc_cli_CHAR_LIST(tagQueryDATA* pSqlPACKET) {

    const char* stmt = "SELECT id, name, level, job_id, gender_id, face_id, hair_id, delete_by "
                       "FROM character WHERE "
                       "account_email=$1";

    std::string username = pSqlPACKET->m_Name.Get();
    QueryResult res = this->db.query(stmt, {username});
    if (!res.is_ok()) {
        std::string msg = this->db.last_error_message();
        LOG_ERROR("Error getting character list for account {}: {}", username.c_str(), msg.c_str());
        return false;
    }

    classPACKET pCPacket = classPACKET();
    pCPacket.m_HEADER.m_wType = WSV_CHAR_LIST;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_CHAR_LIST);

    size_t char_count = min(res.row_count, GameStaticConfig::MAX_CHARACTERS);
    pCPacket.m_wsv_CHAR_LIST.m_btCharCNT = 0;

    std::vector<std::string> delete_list;
    DateTime now = std::chrono::system_clock::now();

    for (size_t idx = 0; idx < char_count; ++idx) {
        std::string char_id = res.get_string(idx, 0);
        std::string char_name = res.get_string(idx, 1);
        int level = res.get_int32(idx, 2);
        int job_id = res.get_int32(idx, 3);
        int gender_id = res.get_int32(idx, 4);
        int face_id = res.get_int32(idx, 5);
        int hair_id = res.get_int32(idx, 6);
        DateTime delete_by = res.get_datetime(idx, 7);

        int64_t delete_seconds_remaining = 0;

        bool delete_by_null = res.get_null(idx, 7);
        if (!delete_by_null) {
            if (delete_by <= now) {
                delete_list.push_back(char_id);
                continue;
            }

            delete_seconds_remaining =
                std::chrono::duration_cast<std::chrono::seconds>(delete_by - now).count();
        }

        tagCHARINFO char_info;
        char_info.m_btCharRACE = gender_id;
        char_info.m_nJOB = job_id;
        char_info.m_nLEVEL = level;
        char_info.m_dwRemainSEC = delete_seconds_remaining;
        char_info.m_btIsPlatinumCHAR = false;

        tagPartITEM equipment[MAX_BODY_PART];
        std::fill_n(equipment, MAX_BODY_PART, tagPartITEM{});

        equipment[BODY_PART_FACE].m_nItemNo = face_id;
        equipment[BODY_PART_HAIR].m_nItemNo = hair_id;

        QueryResult equip_res =
            this->db.query("SELECT inventory.slot, item.game_data_id "
                              "FROM inventory INNER JOIN item ON inventory.item_id = item.id "
                              "WHERE inventory.owner_id = $1",
                {char_id});

        if (!equip_res.is_ok()) {
            LOG_DEBUG("Inventory query failed: {}", equip_res.error_message());
            continue;
        }

        for (size_t row_idx = 0; row_idx < equip_res.row_count; ++row_idx) {
            int slot = equip_res.get_int32(row_idx, 0);
            int game_data_id = equip_res.get_int32(row_idx, 1);

            size_t part_idx = inventory2part(slot);
            if (part_idx < MAX_BODY_PART) {
                equipment[part_idx].m_nItemNo = game_data_id;
            }

            if (slot >= INVENTORY_COSTUME_ITEM0 && slot <= INVENTORY_COSTUME_ITEM0 + MAX_COSTUME_IDX) {
                part_idx = inventory2part(slot - INVENTORY_COSTUME_ITEM0);
                if (part_idx >= BODY_PART_HELMET && part_idx < MAX_BODY_PART) {
                    equipment[part_idx].m_nItemNo = game_data_id;
                }
            }
        }

        pCPacket.AppendString((char*)char_name.c_str());
        pCPacket.AppendData(&char_info, sizeof(tagCHARINFO));
        pCPacket.AppendData(equipment, sizeof(tagPartITEM) * MAX_BODY_PART);
        pCPacket.m_wsv_CHAR_LIST.m_btCharCNT += 1;
    }

    CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pSqlPACKET->m_iTAG);
    if (pFindUSER) {
        pFindUSER->m_nPlatinumCharCNT = 0;
    }
    g_pUserLIST->SendPacketToSocketIDX(pSqlPACKET->m_iTAG, pCPacket);

    if (delete_list.size() > 0) {
        std::string del_item = fmt::format(
            "DELETE FROM item WHERE id IN (SELECT item_id FROM inventory WHERE owner_id IN ({}))", param_list(delete_list.size()));

        QueryResult del_item_res = this->db.query(del_item, delete_list);
        if (!del_item_res.is_ok()) {
            LOG_ERROR("Failed to delete items for character(s): {}", del_item_res.error_message());
        }

        std::string q =
            fmt::format("DELETE FROM character WHERE id IN ({})", param_list(delete_list.size()));

        QueryResult delete_res = this->db.query(q, delete_list);
        if (!delete_res.is_ok()) {
            LOG_ERROR("Failed to delete character(s): {}", delete_res.error_message());
        }
    }

    return true;
}

bool
CWS_ThreadSQL::Proc_cli_SELECT_CHAR(tagQueryDATA* pSqlPACKET) {
    t_PACKET* packet = (t_PACKET*)pSqlPACKET->m_pPacket;

    short offset = sizeof(cli_SELECT_CHAR);
    short strlen = 0;
    char* char_name = Packet_GetStringPtr(packet, offset, strlen);
    if (!char_name) {
        return false;
    }

    if (strlen == 0 || strlen > GameStaticConfig::MAX_CHARACTER_NAME) {
        return false;
    }

    const char* stmt = "SELECT id, account_email, map_id FROM character WHERE name=$1";
    QueryResult res = this->db.query(stmt, {char_name});
    if (!res.is_ok()) {
        LOG_ERROR("Failed to select character with name {}: {}", char_name, res.error_message());
        return false;
    }

    if (!res.row_count == 1) {
        return false;
    }

    CWS_Client* user = (CWS_Client*)g_pUserLIST->GetSOCKET(pSqlPACKET->m_iTAG);
    if (!user) {
        return false;
    }

    std::string char_id = res.get_string(0, 0);
    std::string account_email = res.get_string(0, 1);
    int map_id = res.get_int32(0, 2);

    if (_strcmpi(user->Get_ACCOUNT(), account_email.c_str()) != 0) {
        return false;
    }

    user->m_dwDBID = std::stoi(char_id);
    user->ClanINIT();

    if (!user->Send_wsv_MOVE_SERVER(map_id)) {
        return false;
    }

    const char* mail_stmt = "SELECT COUNT(*) FROM mail WHERE recipient_id=$1";
    QueryResult mail_res = this->db.query(mail_stmt, {char_id});
    if (!mail_res.is_ok()) {
        LOG_ERROR("Failed to get mail count for character {}: {}",
            char_name,
            mail_res.error_message());
        return false;
    }

    g_pUserLIST->Add_CHAR(user, char_name);
    g_pUserLIST->Send_wsv_MEMO(pSqlPACKET->m_iTAG, MEMO_REQ_RECEIVED_CNT, mail_res.get_int32(0, 0));

    return true;
}

bool
CWS_ThreadSQL::Proc_cli_DELETE_CHAR(tagQueryDATA* pSqlPACKET) {
    t_PACKET* pPacket = (t_PACKET*)pSqlPACKET->m_pPacket;

    short offset = sizeof(cli_DELETE_CHAR);
    short str_len = 0;
    std::string char_name = Packet_GetStringPtr(pPacket, offset, str_len);
    if (char_name.empty()) {
        return false;
    }

    if (char_name.size() > GameStaticConfig::MAX_CHARACTER_NAME) {
        return false;
    }

    DateTime delete_by =
        std::chrono::system_clock::now() + std::chrono::seconds(GameStaticConfig::DELETE_TIME_SEC);

    QueryParam delete_by_param;
    int delete_remaining_sec = 0;

    if (pPacket->m_cli_DELETE_CHAR.m_bDelete) {
        delete_by_param = to_datetime_str(delete_by);
        delete_remaining_sec = GameStaticConfig::DELETE_TIME_SEC;
    }

    const char* stmt = "UPDATE character SET delete_by=$1 WHERE account_email=$2 AND name=$3";
    QueryResult res =
        this->db.queryb(stmt, {delete_by_param, pSqlPACKET->m_Name.Get(), char_name});

    if (!res.is_ok()) {
        LOG_ERROR("Failed to delete character {}: {}", char_name.c_str(), res.error_message());
        return false;
    }

    CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pSqlPACKET->m_iTAG);
    if (pFindUSER) {
        classPACKET pCPacket = classPACKET();
        pCPacket.m_HEADER.m_wType = WSV_DELETE_CHAR;
        pCPacket.m_HEADER.m_nSize = sizeof(wsv_DELETE_CHAR);

        pCPacket.m_wsv_DELETE_CHAR.m_dwDelRemainTIME = delete_remaining_sec;
        pCPacket.AppendString(const_cast<char*>(char_name.c_str()));

        pFindUSER->Send_Start(pCPacket);
    }

    return true;
}

bool
CWS_ThreadSQL::Load_WORLDVAR(int16_t* buffer, size_t count) {
    const char* stmt = "SELECT data FROM worldvar WHERE name = $1";

    QueryResult res = this->db.query(stmt, {WORLD_VAR});
    if (!res.is_ok()) {
        std::string msg = this->db.last_error_message();
        LOG_ERROR("Failed to load world var: {}", msg.c_str());
        return false;
    }

    if (res.row_count > 1) {
        LOG_ERROR("Found too many world var rows: {}", res.row_count);
        return false;
    }

    if (res.row_count == 0) {
        // Pad by 4-bytes which is "time" in CWorldVar
        int16_t* new_buffer = new int16_t[count + 2];
        new_buffer[0] = 0;
        new_buffer[1] = 0;
        std::memcpy(&new_buffer[2], buffer, count);
        this->Save_WorldVAR(reinterpret_cast<uint8_t*>(new_buffer), (count + 2) * sizeof(int16_t));

        LOG_INFO("World var not found, default values saved");
        return true;
    }

    json j = json::parse(res.get_string(0, 0));
    size_t c = min(j.size(), count);

    for (size_t i = 0; i < c; ++i) {
        buffer[i] = j[i].get<int16_t>();
    }

    return true;
}

bool
CWS_ThreadSQL::Save_WorldVAR(BYTE* pVarBUFF, short nBuffLEN) {
    sql_ZONE_DATA* pPacket;

    pPacket = (sql_ZONE_DATA*)new BYTE[sizeof(sql_ZONE_DATA) + nBuffLEN];

    pPacket->m_nDataSIZE = nBuffLEN;

    pPacket->m_wType = SQL_ZONE_DATA;
    pPacket->m_nSize = sizeof(sql_ZONE_DATA) + pPacket->m_nDataSIZE;

    pPacket->m_btDataTYPE = SQL_ZONE_DATA_WORLDVAR_SAVE;
    ::CopyMemory(pPacket->m_btZoneDATA, pVarBUFF, pPacket->m_nDataSIZE);

    CSqlTHREAD::Add_SqlPACKET(0, (char*)WORLD_VAR, (BYTE*)pPacket, pPacket->m_nSize);
    SAFE_DELETE_ARRAY(pPacket);

    return true;
}

bool
CWS_ThreadSQL::Proc_SAVE_WORLDVAR(sql_ZONE_DATA* pSqlZONE) {
    // First bytes are "time" in CWorldVar, we want to get just the var list
    size_t offset = 4;

    size_t count = (pSqlZONE->m_nDataSIZE - offset) / sizeof(int16_t);
    if (count <= 0) {
        return false;
    }

    if (count > MAX_WORLD_VAR_CNT) {
        count = MAX_WORLD_VAR_CNT;
    }

    int16_t* buffer = reinterpret_cast<int16_t*>(pSqlZONE->m_btZoneDATA + offset);

    json j = json::array();
    for (size_t i = 0; i < count; ++i) {
        j.push_back(buffer[i]);
    }

    const char* stmt = "INSERT INTO worldvar (name, data) VALUES ($1, $2)"
                       "ON CONFLICT (name) DO UPDATE SET data=$2";

    QueryResult res = this->db.query(stmt, {WORLD_VAR, j.dump()});
    if (!res.is_ok()) {
        std::string msg = this->db.last_error_message();
        LOG_ERROR("Failed to save WORLDVAR: {}", msg.c_str());
        return false;
    }

    return true;
}

bool
CWS_ThreadSQL::Proc_cli_MEMO(tagQueryDATA* pSqlPACKET) {
    t_PACKET* pPacket = (t_PACKET*)pSqlPACKET->m_pPacket;

    std::string char_name = pSqlPACKET->m_Name.Get();
    if (char_name.empty()) {
        return false;
    }

    CWS_Client* user = static_cast<CWS_Client*>(g_pUserLIST->GetSOCKET(pSqlPACKET->m_iTAG));
    if (!user) {
        return false;
    }
    std::string user_id = std::to_string(user->Get_DBID());

    switch (pPacket->m_cli_MEMO.m_btTYPE) {
        case MEMO_REQ_RECEIVED_CNT: {
            const char* stmt = "SELECT COUNT(*) FROM character, mail WHERE "
                               "character.id=$1 AND character.id=mail.recipient_id";

            QueryResult res = this->db.query(stmt, {user_id});
            if (!res.is_ok()) {
                LOG_ERROR("Failed to get mail count for character {}: {}",
                    char_name.c_str(),
                    res.error_message());
                return false;
            }
            g_pUserLIST->Send_wsv_MEMO(pSqlPACKET->m_iTAG,
                MEMO_REPLY_RECEIVED_CNT,
                res.get_int32(0, 0));
            return true;
        }

        case MEMO_REQ_CONTENTS: {
            const char* stmt = "SELECT cm.id, cm.sent, cm.message, "
                               "from_char.name as from_character_name "
                               "FROM mail AS cm "
                               "LEFT JOIN character as from_char ON from_char.id = cm.sender_id "
                               "LEFT JOIN character as to_char ON to_char.id = cm.recipient_id "
                               "WHERE to_char.id = $1";

            QueryResult res = this->db.query(stmt, {user_id});
            if (!res.is_ok()) {
                LOG_ERROR("Failed to get mail for character {}: {}",
                    user->Get_DBID(),
                    res.error_message());
                return false;
            }

            classPACKET packet = classPACKET();
            packet.m_HEADER.m_wType = WSV_MEMO;
            packet.m_HEADER.m_nSize = sizeof(wsv_MEMO);
            packet.m_wsv_MEMO.m_btTYPE = MEMO_REPLY_CONTENTS;

            std::vector<std::string> delete_list;
            for (size_t idx = 0; idx < res.row_count; ++idx) {
                std::string mail_id = res.get_string(idx, 0);
                DateTime sent = res.get_datetime(idx, 1);
                std::string message = res.get_string(idx, 2);
                std::string from = res.get_string(idx, 3);

                int32_t* pDW = reinterpret_cast<int32_t*>(&packet.m_pDATA[packet.m_HEADER.m_nSize]);
                packet.m_HEADER.m_nSize += 4;
                *pDW = time_since_win_epoch(sent).count();
                packet.AppendString(const_cast<char*>(from.c_str()));
                packet.AppendString(const_cast<char*>(message.c_str()));

                if (packet.m_HEADER.m_nSize > MAX_PACKET_SIZE - 270) {
                    g_pUserLIST->SendPacketToSocketIDX(pSqlPACKET->m_iTAG, packet);

                    packet = classPACKET();
                    packet.m_HEADER.m_wType = WSV_MEMO;
                    packet.m_HEADER.m_nSize = sizeof(wsv_MEMO);
                    packet.m_wsv_MEMO.m_btTYPE = MEMO_REPLY_CONTENTS;
                }

                delete_list.push_back(mail_id);
            }

            g_pUserLIST->SendPacketToSocketIDX(pSqlPACKET->m_iTAG, packet);

            // Delete mail sent to the client from the database
            if (delete_list.size() > 0) {
                std::string delete_stmt = fmt::format("DELETE FROM mail WHERE id IN ({})",
                    param_list(delete_list.size()));
                QueryResult delete_res = this->db.query(delete_stmt, {delete_list});
                if (!delete_res.is_ok()) {
                    LOG_ERROR("Failed to delete mail for character {}: {}",
                        char_name.c_str(),
                        delete_res.error_message());
                    return false;
                }
            }
            return true;
        }

        case MEMO_REQ_SEND: {
            short offset = sizeof(cli_MEMO);
            char* target_char = Packet_GetStringPtr(pPacket, offset);

            if (!target_char || strlen(target_char) < 1) {
                g_pUserLIST->Send_wsv_MEMO(pSqlPACKET->m_iTAG, MEMO_REPLY_SEND_INVALID_TARGET);
                return true;
            }

            const char* stmt = "SELECT id FROM character WHERE character.name=$1";
            QueryResult res = this->db.query(stmt, {target_char});
            if (!res.is_ok()) {
                LOG_ERROR("Failed to get character id for character {}: {}",
                    target_char,
                    res.error_message());
                return false;
            }

            if (res.row_count == 0) {
                g_pUserLIST->Send_wsv_MEMO(pSqlPACKET->m_iTAG, MEMO_REPLY_SEND_INVALID_TARGET);
                return true;
            }

            std::string target_char_id = res.get_string(0, 0);

            char* message = Packet_GetStringPtr(pPacket, offset);
            if (!message || strlen(message) < 2) {
                g_pUserLIST->Send_wsv_MEMO(pSqlPACKET->m_iTAG, MEMO_REPLY_SEND_INVALID_CONTENT);
                return true;
            }

            const char* count_stmt = "SELECT COUNT(*) FROM character, mail WHERE "
                                     "character.name=$1 AND character.id=mail.recipient_id";

            QueryResult count_res = this->db.query(count_stmt, {target_char});
            if (!count_res.is_ok()) {
                LOG_ERROR("Failed to get mail count for character {}: {}",
                    target_char,
                    count_res.error_message());
                return false;
            }

            if (count_res.get_int32(0, 0) > GameStaticConfig::MAX_MAIL_MESSAGES) {
                g_pUserLIST->Send_wsv_MEMO(pSqlPACKET->m_iTAG, MEMO_REPLY_SEND_FULL_MEMO);
                return true;
            }

            const char* add_stmt = "INSERT INTO mail (recipient_id, "
                                   "sender_id, message) VALUES ($1, $2, $3)";

            QueryResult add_res = this->db.query(stmt, {target_char_id, user_id, message});
            if (!add_res.is_ok()) {
                LOG_ERROR("Failed to add character mail from {} to {}: {}",
                    pSqlPACKET->m_Name.Get(),
                    target_char_id,
                    add_res.error_message());
                return false;
            }

            g_pUserLIST->Send_wsv_MEMO(pSqlPACKET->m_iTAG, MEMO_REPLY_SEND_OK);
            return true;
        }
    }

    return true;
}

void
CWS_ThreadSQL::handle_queued_packet(QueuedPacket& p) {
    Packets::PacketType packet_type = p.packet.packet_data()->data_type();
    switch (packet_type) {
        case Packets::PacketType::CharacterCreateRequest: {
            this->handle_char_create_req(p);
            break;
        }
    }
}

bool
CWS_ThreadSQL::handle_char_create_req(QueuedPacket& p) {
    const Packets::CharacterCreateRequest* req =
        p.packet.packet_data()->data_as_CharacterCreateRequest();

    if (!req) {
        return false;
    }

    if (!req->name()) {
        return false;
    }

    std::regex name_re("^[a-zA-Z0-9_-]+$");
    bool name_valid = req->name()->size() > 3
        && req->name()->size() <= GameStaticConfig::MAX_CHARACTER_NAME
        && std::regex_match(req->name()->c_str(), name_re);

    bool face_valid = req->face_id() > 0 && req->face_id() < g_TblFACE.row_count;
    bool hair_valid = req->hair_id() > 0 && req->hair_id() < g_TblHAIR.row_count;

    const char* char_name = req->name()->c_str();
    Gender gender = gender_from(req->gender_id());
    Job job = job_from(req->job_id());

    bool job_valid = (job == Job::Visitor) || is_first_job(job);

    if (!(name_valid && face_valid && hair_valid && job_valid)) {
        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_FAILED);
        return false;
    }

    // Check if the name already exists
    const char* stmt = "SELECT COUNT(*) FROM character WHERE name=$1";
    QueryResult res = this->db.query(stmt, {char_name});
    if (!res.is_ok() || res.row_count != 1) {
        LOG_ERROR("Failed to count characters with name '{}': {}", char_name, res.error_message());
        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_FAILED);
        return false;
    }

    if (res.get_int32(0, 0) != 0) {
        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_DUP_NAME);
        return false;
    }

    CWS_Client* client = g_pUserLIST->find_client(p.socket_id);
    if (!client) {
        return false;
    }

    std::string account_email = client->Get_ACCOUNT();

    // Check the user has enough slots for a new character
    const char* count_stmt = "SELECT COUNT (*) from character WHERE account_email=$1";
    QueryResult count_res = this->db.query(stmt, {account_email});
    if (!count_res.is_ok() || count_res.row_count != 1) {
        LOG_ERROR("Failed to count characters for account '{}': {}",
            account_email.c_str(),
            res.error_message());
        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_FAILED);
        return false;
    }

    int char_count = res.get_int32(0, 0);
    if (char_count >= GameStaticConfig::MAX_CHARACTERS) {
        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_NO_MORE_SLOT);
        return false;
    };

    const int gender_id = static_cast<int>(gender);
    const int job_id = static_cast<int>(job);
    const int start_map_id = AVATAR_ZONE(gender_id);
    tPOINTF start_pos = g_ZoneLIST.Get_StartPOS(start_map_id);

    std::vector<std::string> params{
        account_email,
        char_name,
        std::to_string(gender_id),
        std::to_string(job_id),
        std::to_string(req->face_id()),
        std::to_string(req->hair_id()),
        std::to_string(AVATAR_MONEY(gender_id)),
        // Stats
        std::to_string(AVATAR_STR(gender_id)),
        std::to_string(AVATAR_DEX(gender_id)),
        std::to_string(AVATAR_INT(gender_id)),
        std::to_string(AVATAR_CON(gender_id)),
        std::to_string(AVATAR_CHARM(gender_id)),
        std::to_string(AVATAR_SENSE(gender_id)),
        // Start Spawn
        std::to_string(start_map_id),
        std::to_string(start_pos.x),
        std::to_string(start_pos.y),
        // Town Respawn
        std::to_string(start_map_id),
        std::to_string(start_pos.x),
        std::to_string(start_pos.y),
    };

    CInventory& inv = m_pDefaultINV[gender_id];

    QueryResult trans_res = this->db.query("BEGIN", {});
    if (!trans_res.is_ok()) {
        LOG_ERROR("Failed to begin transaction when creating '{}': {}",
            char_name,
            trans_res.error_message());
        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_FAILED);
        return false;
    }

    std::string char_stmt = fmt::format(
        "INSERT INTO character (account_email, name, "
        "gender_id, job_id, face_id, hair_id, money, "
        "str, dex, intt, con, cha, sen, "
        "map_id, respawn_x, respawn_y, town_respawn_id, town_respawn_x, town_respawn_y) "
        "VALUES ({}) "
        "RETURNING id",
        param_list(params.size()));

    QueryResult char_res = this->db.query(char_stmt, params);
    if (!char_res.is_ok()) {
        LOG_ERROR("Failed to create character '{}' for username '{}': {}",
            char_name,
            account_email.c_str(),
            char_res.error_message());

        trans_res = this->db.query("ROLLBACK", {});
        if (!trans_res.is_ok()) {
            LOG_ERROR("Failed to rollback transaction when creating '{}': {}",
                char_name,
                trans_res.error_message());
        }

        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_FAILED);
        return false;
    }

    if (!char_res.row_count == 1) {
        LOG_ERROR("No ID returned for inserted character '{}'", char_name);

        trans_res = this->db.query("ROLLBACK", {});
        if (!trans_res.is_ok()) {
            LOG_ERROR("Failed to rollback transaction when creating '{}': {}",
                char_name,
                trans_res.error_message());
        }

        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_FAILED);
        return false;
    }

    std::string char_id = char_res.get_string(0, 0);

    std::string bulk;
    for (size_t i = 0; i < INVENTORY_TOTAL_SIZE; ++i) {
        tagITEM& item = m_pDefaultINV[gender_id].m_ItemLIST[i];
        if (item.GetTYPE() == 0) {
            continue;
        }

        // Reset the UUID so that the every created char gets a unique item
        item.uuid = Rose::Util::UUID::generate();

        uint16_t gem_id = 0;
        uint16_t grade = 0;
        uint16_t durability = 0;
        uint16_t lifespan = 0;
        bool is_appraisal = false;
        bool has_socket = false;
        bool is_crafted = false;
        uint16_t quantity = 1;

        if (item.IsEnableDupCNT()) {
            quantity = item.GetQuantity();
        } else {
            gem_id = item.GetGemNO();
            grade = item.GetGrade();
            durability = item.GetDurability();
            lifespan = item.GetLife();
            is_appraisal = item.IsAppraisal();
            has_socket = item.HasSocket();
            is_crafted = item.IsCreated();
        }

        bulk += fmt::format(
            "INSERT INTO item (uuid, game_data_id, type_id, stat_id, grade, durability, "
            "lifespan, appraisal, socket, crafted) "
            "VALUES ('{}', {}, {}, {}, {}, {}, {}, {}, {}, {});",
            item.uuid.to_string(),
            std::to_string(item.GetItemNO()),
            std::to_string(item.GetTYPE()),
            std::to_string(gem_id),
            std::to_string(grade),
            std::to_string(durability),
            std::to_string(lifespan),
            PG_BOOL(is_appraisal),
            PG_BOOL(has_socket),
            PG_BOOL(is_crafted));

        bulk += fmt::format("INSERT INTO inventory (owner_id, slot, quantity, item_id) "
                            "VALUES ({}, {}, {}, (SELECT id FROM item WHERE uuid='{}'));",
            char_id,
            i,
            quantity,
            item.uuid.to_string());
    }

    QueryResult bulk_res = this->db.batch(bulk);
    if (!bulk_res.is_ok()) {
        LOG_ERROR("Failed to insert default items for character '{}': {}",
            char_name,
            bulk_res.error_message());

        trans_res = this->db.query("ROLLBACK", {});
        if (!trans_res.is_ok()) {
            LOG_ERROR("Failed to rollback transaction when creating '{}': {}",
                char_name,
                trans_res.error_message());
        }
        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_FAILED);
        return false;
    }

    trans_res = this->db.query("COMMIT", {});
    if (!trans_res.is_ok()) {
        LOG_ERROR("Failed to commit transaction when creating '{}': {}",
            char_name,
            trans_res.error_message());
        g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_FAILED);
        return false;
    }

    g_pUserLIST->Send_wsv_CREATE_CHAR(p.socket_id, RESULT_CREATE_CHAR_OK, char_count + 1);
    return true;
}
