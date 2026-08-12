/*
    $Header: /7HeartsOnline/Server/SHO_GS/Sho_gs_lib/GS_ThreadSQL.cpp 117   05-08-25 5:24p Icarus $
*/
#include "stdafx.h"

#include "CSLList.h"
#include "CThreadGUILD.h"
#include "GS_ListUSER.h"
#include "GS_SocketLSV.h"
#include "GS_ThreadSQL.h"
#include "LIB_gsMAIN.h"
#include "ZoneLIST.h"
#include "classTIME.h"

#include "rose/common/game_config.h"
#include "rose/database/database.h"

#include "nlohmann/json.hpp"

using namespace Rose;
using namespace Rose::Common;
using namespace Rose::Database;

using json = nlohmann::json;

GS_CThreadSQL::GS_CThreadSQL(): CSqlTHREAD(true), m_csUserLIST(4000), m_bWaiting(false) {}

bool
GS_CThreadSQL::Add_SqlPacketWithACCOUNT(classUSER* pUSER, t_PACKET* pPacket) {
    return CSqlTHREAD::Add_SqlPACKET((int)pUSER->m_iSocketIDX,
        pUSER->Get_ACCOUNT(),
        (BYTE*)pPacket,
        pPacket->m_HEADER.m_nSize);
}
bool
GS_CThreadSQL::Add_SqlPacketWithAVATAR(classUSER* pUSER, t_PACKET* pPacket) {
    return CSqlTHREAD::Add_SqlPACKET((int)pUSER->m_iSocketIDX,
        pUSER->Get_NAME(),
        (BYTE*)pPacket,
        pPacket->m_HEADER.m_nSize);
}

bool
GS_CThreadSQL::Add_BackUpUSER(classUSER* pUSER, BYTE btLogOutMODE) {
    if (pUSER->m_btLogOutMODE != btLogOutMODE) {
        return true;
    }

    m_csUserLIST.Lock();
    {
        classDLLNODE<tagSqlUSER>* pNewNODE;
        pNewNODE = new classDLLNODE<tagSqlUSER>;

        pNewNODE->DATA.m_btLogOutMODE = btLogOutMODE;
        pNewNODE->DATA.m_pUSER = pUSER;
        m_AddUserLIST.AppendNode(pNewNODE);
    }
    m_csUserLIST.Unlock();

    m_pEVENT->SetEvent();

    return true;
}

bool
GS_CThreadSQL::UpdateUserRECORD(classUSER* user) {
    const char* char_name = user->Get_NAME();

    // Begin the database transaction
    QueryResult trans_res = this->db.query("BEGIN", {});
    if (!trans_res.is_ok()) {
        LOG_ERROR("Failed to begin transaction when saving character '{}': {}",
            char_name,
            trans_res.error_message());
        return false;
    }

    // Update the character info
    std::string query(
        "UPDATE character SET gender_id=$1, job_id=$2, face_id=$3, hair_id=$4, level=$5, exp=$6, "
        "hp=$7, mp=$8, stamina=$9, max_hp=$10, max_mp=$11, max_stamina=$12, str=$13, dex=$14, "
        "intt=$15, con=$16, cha=$17, sen=$18, stat_points=$19, skill_points=$20 , money=$21, "
        "storage_money=$22, map_id=$23, respawn_x=$24, respawn_y=$25, town_respawn_id=$26, "
        "town_respawn_x=$27, town_respawn_y=$28, union_id=$29, skills=$30, quests=$31, "
        "hotbar=$32, wishlist=$33 "
        "WHERE id=$34");

    QueryResult char_res = this->db.query(query,
        {
            std::to_string(user->m_nCharRACE),
            std::to_string(user->m_BasicINFO.m_nClass),
            std::to_string(user->m_BasicINFO.m_cFaceIDX),
            std::to_string(user->m_BasicINFO.m_cHairIDX),
            std::to_string(user->m_GrowAbility.m_nLevel),
            std::to_string(user->m_GrowAbility.m_lEXP),
            std::to_string(user->m_GrowAbility.m_nHP),
            std::to_string(user->m_GrowAbility.m_nMP),
            std::to_string(user->m_GrowAbility.m_nSTAMINA),
            std::to_string(user->Get_MaxHP()),
            std::to_string(user->Get_MaxMP()),
            std::to_string(MAX_STAMINA),
            std::to_string(user->m_BasicAbility.m_nSTR),
            std::to_string(user->m_BasicAbility.m_nDEX),
            std::to_string(user->m_BasicAbility.m_nINT),
            std::to_string(user->m_BasicAbility.m_nCON),
            std::to_string(user->m_BasicAbility.m_nCHARM),
            std::to_string(user->m_BasicAbility.m_nSENSE),
            std::to_string(user->m_GrowAbility.m_nBonusPoint),
            std::to_string(user->m_GrowAbility.m_nSkillPoint),
            std::to_string(user->m_Inventory.m_i64Money),
            std::to_string(user->m_Bank.m_i64ZULY),
            std::to_string(user->m_nZoneNO),
            std::to_string(user->m_PosCUR.x),
            std::to_string(user->m_PosCUR.y),
            std::to_string(user->m_nReviveZoneNO),
            std::to_string(user->m_PosREVIVE.x),
            std::to_string(user->m_PosREVIVE.y),
            std::to_string(user->m_BasicINFO.m_cUnion),
            json(user->m_Skills).dump(),
            json(user->m_Quests).dump(),
            json(user->m_HotICONS).dump(),
            json(user->m_WishLIST).dump(),
            std::to_string(user->m_dwDBID),
        });

    if (!char_res.is_ok()) {
        LOG_ERROR("Failed to save character info for '{}': {}",
            user->Get_NAME(),
            char_res.error_message());

        trans_res = this->db.query("ROLLBACK", {});
        if (!trans_res.is_ok()) {
            LOG_ERROR("Failed to rollback transaction when saving character '{}': {}",
                char_name,
                trans_res.error_message());
        }

        return false;
    }

    // Update union points
    std::string union_query =
        "UPDATE union_points SET union1=$1, union2=$2, union3=$3, union4=$4, union5=$5, union6=$6, "
        "union7=$7, union8=$8, union9=$9, union10=$10 "
        "WHERE character_id=$11";

    QueryResult union_res = this->db.query(union_query,
        {
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[0]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[1]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[2]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[3]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[4]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[5]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[6]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[7]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[8]),
            std::to_string(user->m_GrowAbility.m_nUnionPOINT[9]),
            std::to_string(user->m_dwDBID),
        });

    if (!union_res.is_ok()) {
        LOG_ERROR("Failed to save union info for '{}': {}",
            user->Get_NAME(),
            union_res.error_message());

        trans_res = this->db.query("ROLLBACK", {});
        if (!trans_res.is_ok()) {
            LOG_ERROR("Failed to rollback transaction when saving character '{}': {}",
                char_name,
                trans_res.error_message());
        }

        return false;
    }

    std::vector<std::string> valid_items;

    // Update inventory info
    std::vector<int> inventory_delete_list;
    std::string inventory_query;

    for (size_t i = 0; i < INVENTORY_TOTAL_SIZE; ++i) {
        tagITEM& item = user->m_Inventory.m_ItemLIST[i];
        if (item.GetTYPE() == 0 || item.IsEmpty() || !item.IsValidITEM()) {
            inventory_delete_list.push_back(i);
            continue;
        }

        valid_items.push_back(item.uuid.to_string());

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

        inventory_query += fmt::format(
            "INSERT INTO item (uuid, game_data_id, type_id, stat_id, grade, durability, "
            "lifespan, appraisal, socket, crafted) "
            "VALUES ('{0}', {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}) "
            "ON CONFLICT (uuid) "
            "DO UPDATE SET uuid='{0}', game_data_id={1}, type_id={2}, stat_id={3}, grade={4}, "
            "durability={5}, lifespan={6}, appraisal={7}, socket={8}, crafted={9};",
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

        inventory_query += fmt::format(
            "INSERT INTO inventory (owner_id, slot, quantity, item_id) "
            "VALUES ({0}, {1}, {2}, (SELECT id FROM item WHERE uuid='{3}')) "
            "ON CONFLICT (owner_id, slot) "
            "DO UPDATE SET quantity={2}, item_id=(SELECT id FROM item WHERE uuid='{3}');",
            user->m_dwDBID,
            i,
            quantity,
            item.uuid.to_string());
    }

    // Update storage info
    std::vector<int> storage_delete_list;
    std::string storage_query;

    for (size_t i = 0; i < BANKSLOT_TOTAL; ++i) {
        tagITEM& item = user->m_Bank.m_ItemLIST[i];
        if (item.GetTYPE() == 0 || item.IsEmpty() || !item.IsValidITEM()) {
            storage_delete_list.push_back(i);
            continue;
        }

        valid_items.push_back(item.uuid.to_string());

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

        storage_query += fmt::format(
            "INSERT INTO item (uuid, game_data_id, type_id, stat_id, grade, durability, "
            "lifespan, appraisal, socket, crafted) "
            "VALUES ('{0}', {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}) "
            "ON CONFLICT (uuid) "
            "DO UPDATE SET uuid='{0}', game_data_id={1}, type_id={2}, stat_id={3}, grade={4}, "
            "durability={5}, lifespan={6}, appraisal={7}, socket={8}, crafted={9};",
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

        storage_query += fmt::format(
            "INSERT INTO storage (owner_id, slot, quantity, item_id) "
            "VALUES ({0}, {1}, {2}, (SELECT id FROM item WHERE uuid='{3}')) "
            "ON CONFLICT (owner_id, slot) "
            "DO UPDATE SET quantity={2}, item_id=(SELECT id FROM item WHERE uuid='{3}');",
            user->m_dwDBID,
            i,
            quantity,
            item.uuid.to_string());
    }

    std::string delete_invalid_query =
        fmt::format("DELETE FROM item WHERE "
                    "(id IN (SELECT item_id FROM inventory WHERE owner_id={0}) OR "
                    "id IN (SELECT item_id FROM storage WHERE owner_id={0})) AND "
                    "uuid NOT IN ({1});",
            user->m_dwDBID,
            value_list(valid_items, true));

    std::string delete_inventory_query = fmt::format("DELETE FROM inventory WHERE "
                                                     "owner_id={0} AND slot IN ({1});",
        user->m_dwDBID,
        value_list(inventory_delete_list));

    std::string delete_storage_query = fmt::format("DELETE FROM storage WHERE "
                                                   "owner_id={0} AND slot in ({1});",
        user->m_dwDBID,
        value_list(storage_delete_list));

    std::string bulk = delete_invalid_query;
    bulk += delete_inventory_query + inventory_query;
    bulk += delete_storage_query + storage_query;

    QueryResult bulk_res = this->db.batch(bulk);
    if (!bulk_res.is_ok()) {
        LOG_ERROR("Failed to update items for character '{}': {}",
            char_name,
            bulk_res.error_message());

        trans_res = this->db.query("ROLLBACK", {});
        if (!trans_res.is_ok()) {
            LOG_ERROR("Failed to rollback transaction when creating '{}': {}",
                char_name,
                trans_res.error_message());
        }
        return false;
    }

    // Commit the transaction
    trans_res = this->db.query("COMMIT", {});
    if (!trans_res.is_ok()) {
        LOG_ERROR("Failed to commit transaction when saving character '{}': {}",
            char_name,
            trans_res.error_message());
        return false;
    }

    // TODO: Wish list
    // JSON

    return true;
}

void
GS_CThreadSQL::Execute() {
    //	this->SetPriority( THREAD_PRIORITY_ABOVE_NORMAL );	// Priority 1 point above the priority
    // class
    this->SetPriority(THREAD_PRIORITY_HIGHEST); // Priority 2 point above the priority class

    CDLList<tagQueryDATA>::tagNODE* pSqlNODE;
    classDLLNODE<tagSqlUSER>* pUsrNODE;

    while (TRUE) {
        if (!this->Terminated) {
            m_bWaiting = true;
            m_pEVENT->WaitFor(INFINITE);
            m_bWaiting = false;
        } else {
            int iCnt1, iCnt2;
            this->m_CS.Lock();
            iCnt1 = m_AddPACKET.GetNodeCount();
            this->m_CS.Unlock();

            this->m_csUserLIST.Lock();
            iCnt2 = m_AddUserLIST.GetNodeCount();
            this->m_csUserLIST.Unlock();
            if (iCnt1 <= 0 && iCnt2 <= 0) {
                m_bWaiting = true;
                break;
            }
        }

        this->m_CS.Lock();
        m_RunPACKET.AppendNodeList(&m_AddPACKET);
        m_AddPACKET.Init();
        m_pEVENT->ResetEvent();
        this->m_CS.Unlock();

        m_dwCurTIME = classTIME::GetCurrentAbsSecond();
        for (pSqlNODE = m_RunPACKET.GetHeadNode(); pSqlNODE;) {
            if (Run_SqlPACKET(&pSqlNODE->m_VALUE))
                pSqlNODE = this->Del_SqlPACKET(pSqlNODE);
            else
                pSqlNODE = m_RunPACKET.GetNextNode(pSqlNODE);
        }

        m_csUserLIST.Lock();
        m_RunUserLIST.AppendNodeList(&m_AddUserLIST);
        m_AddUserLIST.Init();
        m_csUserLIST.Unlock();

        while (pUsrNODE = m_RunUserLIST.GetHeadNode()) {
            if (pUsrNODE->DATA.m_pUSER->Get_NAME()) {
                this->UpdateUserRECORD(pUsrNODE->DATA.m_pUSER);
            }

            if (pUsrNODE->DATA.m_btLogOutMODE) {
                if (pUsrNODE->DATA.m_pUSER->Get_ACCOUNT()) {
                    switch (pUsrNODE->DATA.m_btLogOutMODE) {
                        case LOGOUT_MODE_CHARLIST:
                            g_pSockLSV->Send_gsv_CHANGE_CHAR(pUsrNODE->DATA.m_pUSER);
                            break;

                        default:
                            if (pUsrNODE->DATA.m_pUSER->GetPARTY()) {
                                // ºñÁ¤»ó Á¾·á...
                                pUsrNODE->DATA.m_pUSER->m_pPartyBUFF
                                    ->Lock(); // 2004. 9. 19 »õº® 7½Ã10ºÐ :: »ª¼·..
                                pUsrNODE->DATA.m_pUSER->m_pPartyBUFF->OnDisconnect(
                                    pUsrNODE->DATA.m_pUSER);
                                pUsrNODE->DATA.m_pUSER->m_pPartyBUFF->Unlock();
                            }

                            g_pSockLSV->Send_zws_SUB_ACCOUNT(pUsrNODE->DATA.m_pUSER->m_dwWSID,
                                pUsrNODE->DATA.m_pUSER->Get_ACCOUNT());
                    }
                }
                g_pUserLIST->FreeClientSOCKET(pUsrNODE->DATA.m_pUSER);
            }

            m_RunUserLIST.DeleteNFree(pUsrNODE);
        }
    }

    int iCnt = m_AddPACKET.GetNodeCount();
    assert(iCnt == 0);
}

bool
GS_CThreadSQL::Run_SqlPACKET(tagQueryDATA* pSqlPACKET) {
    switch (pSqlPACKET->m_pPacket->m_wType) {
        case CLI_SELECT_CHAR:
            Proc_cli_SELECT_CHAR(pSqlPACKET);
            break;

        case CLI_BANK_LIST_REQ:
            Proc_cli_BANK_LIST_REQ(pSqlPACKET);
            break;

        default:
            LOG_WARN("Unknown sql packet Type: {:#x}, Size: {}",
                pSqlPACKET->m_pPacket->m_wType,
                pSqlPACKET->m_pPacket->m_nSize);
    }

    return true;
}

bool
GS_CThreadSQL::Proc_cli_SELECT_CHAR(tagQueryDATA* pSqlPACKET) {
    t_PACKET* pPacket = (t_PACKET*)pSqlPACKET->m_pPacket;

    short nOffset = sizeof(cli_SELECT_CHAR);
    short nOutStrLen = 0;
    char* pCharName = Packet_GetStringPtr(pPacket, nOffset, nOutStrLen);
    if (!pCharName || nOutStrLen < 4) {
        return false;
    }

    if (nOutStrLen > GameStaticConfig::MAX_CHARACTER_NAME) {
        return false;
    }

    std::string char_name(pCharName);
    if (char_name.empty() || char_name.size() > GameStaticConfig::MAX_CHARACTER_NAME) {
        return false;
    }

    classUSER* user = (classUSER*)g_pUserLIST->GetSOCKET(pSqlPACKET->m_iTAG);
    if (!user) {
        return false;
    }

    std::string account_email = user->Get_ACCOUNT();

    std::string query =
        "SELECT character.id, gender_id, job_id, face_id, hair_id, level, exp, hp, mp, stamina, "
        "max_hp, max_mp, max_stamina, str, dex, intt, con, cha, sen, stat_points, skill_points, "
        "money, storage_money, map_id, respawn_x, respawn_y, town_respawn_id, town_respawn_x, "
        "town_respawn_y, union_id, skills, quests, hotbar, wishlist, "
        "coalesce(union1, 0), coalesce(union2, 0), coalesce(union3, 0), coalesce(union4, 0), "
        "coalesce(union5, 0), coalesce(union6, 0), coalesce(union7, 0), coalesce(union8, 0), "
        "coalesce(union9, 0), coalesce(union10, 0) "
        "FROM character "
        "LEFT JOIN union_points ON union_points.character_id = character.id "
        "WHERE account_email=$1 AND name=$2;";

    enum CharCol {
        COL_ID = 0,
        COL_GENDER_ID,
        COL_JOB_ID,
        COL_FACE_ID,
        COL_HAIR_ID,
        COL_LEVEL,
        COL_EXP,
        COL_HP,
        COL_MP,
        COL_STAMINA,
        COL_MAX_HP,
        COL_MAX_MP,
        COL_MAX_STAMINA,
        COL_STR,
        COL_DEX,
        COL_INT,
        COL_CON,
        COL_CHA,
        COL_SEN,
        COL_STAT_POINTS,
        COL_SKILL_POINTS,
        COL_MONEY,
        COL_STORAGE_MONEY,
        COL_MAP_ID,
        COL_RESPAWN_X,
        COL_RESPAWN_Y,
        COL_TOWN_RESPAWN_ID,
        COL_TOWN_RESPAWN_X,
        COL_TOWN_RESPAWN_Y,
        COL_UNION_ID,
        COL_SKILLS,
        COL_QUESTS,
        COL_HOTBAR,
        COL_WISHLIST,
        COL_UNION1,
        COL_UNION2,
        COL_UNION3,
        COL_UNION4,
        COL_UNION5,
        COL_UNION6,
        COL_UNION7,
        COL_UNION8,
        COL_UNION9,
        COL_UNION10,
    };

    QueryResult char_res = this->db.query(query, {account_email, char_name});
    if (!char_res.is_ok()) {
        LOG_ERROR("Failed to get character '{}' for account '{}': {}",
            account_email.c_str(),
            char_name.c_str(),
            char_res.error_message());
        return false;
    }

    if (char_res.row_count != 1) {
        LOG_ERROR("No characters returned for account '{} and character name '{}",
            account_email,
            char_name);
        return false;
    }

    QueryResult item_res = this->db.query(
        "SELECT inventory.slot, inventory.quantity, item.uuid, "
        "item.game_data_id, item.type_id, item.stat_id, item.grade, "
        "item.durability, item.lifespan, item.appraisal, item.socket, item.crafted "
        "FROM inventory INNER JOIN item ON inventory.item_id = item.id "
        "WHERE inventory.owner_id = $1",
        {char_res.get_string(0, COL_ID)});

    enum InvCol {
        INV_COL_SLOT,
        INV_COL_QUANTITY,
        INV_COL_UUID,
        INV_COL_GAME_DATA_ID,
        INV_COL_TYPE_ID,
        INV_COL_STAT_ID,
        INV_COL_GRADE,
        INV_COL_DURABILITY,
        INV_COL_LIFESPAN,
        INV_COL_APPRAISAL,
        INV_COL_SOCKET,
        INV_COL_CRAFTED,
    };

    if (!item_res.is_ok()) {
        LOG_ERROR("Inventory query failed for character '{}': {}",
            char_name.c_str(),
            item_res.error_message());
        return false;
    }

    tagBasicINFO basic_info;
    basic_info.Init(0, 0, 0);
    basic_info.m_cFaceIDX = char_res.get_int32(0, COL_FACE_ID);
    basic_info.m_cHairIDX = char_res.get_int32(0, COL_HAIR_ID);
    basic_info.m_nClass = char_res.get_int32(0, COL_JOB_ID);
    basic_info.m_cUnion = char_res.get_int32(0, COL_UNION_ID);

    tagBasicETC basic_etc;
    basic_etc.Init();
    basic_etc.m_btCharRACE = char_res.get_int32(0, COL_GENDER_ID);
    basic_etc.m_nZoneNO = char_res.get_int32(0, COL_MAP_ID);
    basic_etc.m_PosSTART.x = char_res.get_float(0, COL_RESPAWN_X);
    basic_etc.m_PosSTART.y = char_res.get_float(0, COL_RESPAWN_Y);
    basic_etc.m_nReviveZoneNO = char_res.get_int32(0, COL_TOWN_RESPAWN_ID);
    basic_etc.m_PosREVIVE.x = char_res.get_float(0, COL_TOWN_RESPAWN_X);
    basic_etc.m_PosREVIVE.y = char_res.get_float(0, COL_TOWN_RESPAWN_Y);

    basic_etc.m_PartITEM[BODY_PART_FACE].m_nItemNo = basic_info.m_cFaceIDX;
    basic_etc.m_PartITEM[BODY_PART_HAIR].m_nItemNo = basic_info.m_cHairIDX;

    tagBasicAbility basic_ability;
    basic_ability.Init();
    basic_ability.m_nSTR = char_res.get_int32(0, COL_STR);
    basic_ability.m_nDEX = char_res.get_int32(0, COL_DEX);
    basic_ability.m_nINT = char_res.get_int32(0, COL_INT);
    basic_ability.m_nCON = char_res.get_int32(0, COL_CON);
    basic_ability.m_nCHARM = char_res.get_int32(0, COL_CHA);
    basic_ability.m_nSENSE = char_res.get_int32(0, COL_SEN);

    tagGrowAbility grow_ability;
    grow_ability.Init();
    grow_ability.m_nLevel = char_res.get_int32(0, COL_LEVEL);
    grow_ability.m_lEXP = char_res.get_int64(0, COL_EXP);
    grow_ability.m_nHP = char_res.get_int32(0, COL_HP);
    grow_ability.m_nMP = char_res.get_int32(0, COL_MP);
    grow_ability.m_nBonusPoint = char_res.get_int32(0, COL_STAT_POINTS);
    grow_ability.m_nSkillPoint = char_res.get_int32(0, COL_SKILL_POINTS);
    grow_ability.m_nSTAMINA = char_res.get_int32(0, COL_STAMINA);
    grow_ability.m_nUnionPOINT[0] = char_res.get_int32(0, COL_UNION1);
    grow_ability.m_nUnionPOINT[1] = char_res.get_int32(0, COL_UNION2);
    grow_ability.m_nUnionPOINT[2] = char_res.get_int32(0, COL_UNION3);
    grow_ability.m_nUnionPOINT[3] = char_res.get_int32(0, COL_UNION4);
    grow_ability.m_nUnionPOINT[4] = char_res.get_int32(0, COL_UNION5);
    grow_ability.m_nUnionPOINT[5] = char_res.get_int32(0, COL_UNION6);
    grow_ability.m_nUnionPOINT[6] = char_res.get_int32(0, COL_UNION7);
    grow_ability.m_nUnionPOINT[7] = char_res.get_int32(0, COL_UNION8);
    grow_ability.m_nUnionPOINT[8] = char_res.get_int32(0, COL_UNION9);
    grow_ability.m_nUnionPOINT[9] = char_res.get_int32(0, COL_UNION10);

    // TODO: In grow_ability
    // m_btBodySIZE / m_btHeadSIZE -- Would be fun to get these working!
    // m_lPenalEXP -- What is this?
    // m_nPKFlag -- What is this?

    tagSkillAbility skill_ability;
    skill_ability.Init();
    json skill_json = json::parse(char_res.get_string(0, COL_SKILLS));
    from_json(skill_json, skill_ability);

    tagQuestData quest_data;
    quest_data.Init();
    quest_data.CheckExpiredTIME();
    json quest_json = json::parse(char_res.get_string(0, COL_QUESTS));
    from_json(quest_json, quest_data);

    CInventory inventory;
    inventory.Clear();
    inventory.m_i64Money = char_res.get_int64(0, COL_MONEY);

    tagWishLIST wish_list;
    wish_list.Init();
    json wish_json = json::parse(char_res.get_string(0, COL_WISHLIST));
    from_json(wish_json, wish_list);

    CHotICONS hotbar_icons;
    hotbar_icons.Init();
    json hotbar_json = json::parse(char_res.get_string(0, COL_HOTBAR));
    from_json(hotbar_json, hotbar_icons);

    for (size_t row_idx = 0; row_idx < item_res.row_count; ++row_idx) {
        int slot = item_res.get_int32(row_idx, INV_COL_SLOT);

        if (slot <= 0) {
            continue;
        }

        size_t part_idx = inventory2part(slot);
        if (part_idx >= BODY_PART_HELMET && part_idx < MAX_BODY_PART) {
            tagPartITEM& part = basic_etc.m_PartITEM[part_idx];
            part.m_nItemNo = item_res.get_int32(row_idx, INV_COL_GAME_DATA_ID);
            part.m_cGrade = item_res.get_int32(row_idx, INV_COL_GRADE);
            part.m_nGEM_OP = item_res.get_int32(row_idx, INV_COL_STAT_ID);
            part.m_bHasSocket = item_res.get_bool(row_idx, INV_COL_SOCKET) ? 1 : 0;
        }

        if (slot >= INVENTORY_COSTUME_ITEM0 && slot <= INVENTORY_COSTUME_ITEM0 + MAX_COSTUME_IDX) {
            part_idx = inventory2part(slot - INVENTORY_COSTUME_ITEM0);
            if (part_idx >= BODY_PART_HELMET && part_idx < MAX_BODY_PART) {
                tagPartITEM& part = basic_etc.costume[part_idx];
                part.m_nItemNo = item_res.get_int32(row_idx, INV_COL_GAME_DATA_ID);
                part.m_cGrade = item_res.get_int32(row_idx, INV_COL_GRADE);
                part.m_nGEM_OP = item_res.get_int32(row_idx, INV_COL_STAT_ID);
                part.m_bHasSocket = item_res.get_bool(row_idx, INV_COL_SOCKET) ? 1 : 0;
            }
        }

        const size_t ride_part_idx = inventory2ride(slot);
        if (ride_part_idx >= RIDE_PART_BODY && ride_part_idx < MAX_RIDING_PART) {
            tagPartITEM& part = basic_etc.m_RideITEM[ride_part_idx];
            part.m_nItemNo = item_res.get_int32(row_idx, INV_COL_GAME_DATA_ID);
        }

        tagITEM item;
        item.Init(0, 1);
        item.uuid = Rose::Util::UUID::from_string(item_res.get_string(row_idx, INV_COL_UUID));
        item.m_nItemNo = item_res.get_int32(row_idx, INV_COL_GAME_DATA_ID);
        item.m_cType = item_res.get_int32(row_idx, INV_COL_TYPE_ID);

        if (item.IsEnableDupCNT()) {
            item.m_uiQuantity = item_res.get_int32(row_idx, INV_COL_QUANTITY);
        } else {
            item.m_bCreated = item_res.get_bool(row_idx, INV_COL_CRAFTED);
            item.m_bHasSocket = item_res.get_bool(row_idx, INV_COL_SOCKET);
            item.m_bIsAppraisal = item_res.get_bool(row_idx, INV_COL_APPRAISAL);
            item.m_cDurability = item_res.get_int32(row_idx, INV_COL_DURABILITY);
            item.m_cGrade = item_res.get_int32(row_idx, INV_COL_GRADE);
            item.m_nLife = item_res.get_int32(row_idx, INV_COL_LIFESPAN);
            item.m_nGEM_OP = item_res.get_int32(row_idx, INV_COL_STAT_ID);
        }
        inventory.m_ItemLIST[slot] = item;
    }

    CZoneTHREAD* zone = g_pZoneLIST->GetZONE(basic_etc.m_nZoneNO);
    if (zone) {
        if (basic_etc.m_PosSTART.x == 0.0f || basic_etc.m_PosSTART.y == 0.0f) {
            basic_etc.m_PosSTART = zone->Get_StartRevivePOS();
        } else {
            basic_etc.m_PosSTART = zone->Get_RevivePOS(basic_etc.m_PosSTART);
            basic_etc.m_PosSTART.x += (RANDOM(1001) - 500);
            basic_etc.m_PosSTART.y += (RANDOM(1001) - 500);
        }
    } else {
        basic_etc.m_nZoneNO = AVATAR_ZONE(basic_etc.m_btCharRACE);
        CZoneTHREAD* zone2 = g_pZoneLIST->GetZONE(basic_etc.m_nZoneNO);
        if (!zone) {
            basic_etc.m_nZoneNO = 1;
            zone = g_pZoneLIST->GetZONE(basic_etc.m_nZoneNO);
        }
        basic_etc.m_PosSTART = zone->Get_StartPOS();
    }

    user->Set_NAME(pCharName);
    user->Set_RNAME(pCharName);
    user->m_btPlatinumCHAR = false;
    user->m_dwDBID = char_res.get_int32(0, COL_ID);
    user->m_i64StartMoney = char_res.get_int32(0, COL_MONEY);
    user->m_nCharRACE = basic_etc.m_btCharRACE;
    user->m_nZoneNO = basic_etc.m_nZoneNO;
    user->m_PosCUR = basic_etc.m_PosSTART;
    user->m_nReviveZoneNO = basic_etc.m_nReviveZoneNO;
    user->m_PosREVIVE = basic_etc.m_PosREVIVE;
    user->m_dwBackUpTIME = zone->GetCurrentTIME();
    user->m_dwItemSN = std::chrono::system_clock::now().time_since_epoch().count();

    g_pUserLIST->Add_CHAR(user);

    std::copy(&basic_etc.m_PartITEM[0],
        &basic_etc.m_PartITEM[0] + MAX_BODY_PART,
        &user->m_PartITEM[0]);

    std::copy(&basic_etc.m_RideITEM[0],
        &basic_etc.m_RideITEM[0] + MAX_RIDING_PART,
        &user->m_RideITEM[0]);

    std::copy(&basic_etc.costume[0], 
        &basic_etc.costume[0] + MAX_BODY_PART, 
        &user->costume[0]);

    user->m_BasicINFO.Init(0, 0, 0);
    user->m_BasicAbility.Init();
    user->m_GrowAbility.Init();
    user->m_Skills.Init();
    user->m_Quests.Init();
    user->m_Inventory.Clear();
    user->m_WishLIST.Init();
    user->m_HotICONS.Init();

    std::memcpy(&user->m_BasicINFO, &basic_info, sizeof(tagBasicINFO));
    std::memcpy(&user->m_BasicAbility, &basic_ability, sizeof(tagBasicAbility));
    std::memcpy(&user->m_GrowAbility, &grow_ability, sizeof(tagGrowAbility));
    std::memcpy(&user->m_Skills, &skill_ability, sizeof(tagSkillAbility));
    std::memcpy(&user->m_Quests, &quest_data, sizeof(tagQuestData));
    std::memcpy(&user->m_Inventory, &inventory, sizeof(CInventory));
    std::memcpy(&user->m_WishLIST, &wish_list, sizeof(tagWishLIST));
    std::memcpy(&user->m_HotICONS, &hotbar_icons, sizeof(CHotICONS));

    user->InitPassiveSkill();
    user->UpdateAbility();
    user->Set_ShotITEM();

    if (user->GetCur_STAMINA() > MAX_STAMINA) {
        user->SetCur_STAMINA(MAX_STAMINA);
    }

    if (user->Get_HP() <= 0) {
        user->Set_HP(3 * user->Get_MaxHP() / 10);
    } else if (user->Get_HP() > user->Get_MaxHP()) {
        user->Set_HP(user->Get_MaxHP());
    }

    std::shared_ptr<classPACKET> p = std::make_shared<classPACKET>();
    if (pPacket->m_cli_SELECT_CHAR.m_btCharNO) {
        p->m_HEADER.m_wType = GSV_SELECT_CHAR;
        p->m_HEADER.m_nSize = sizeof(gsv_SELECT_CHAR);

        p->m_gsv_SELECT_CHAR.m_btCharRACE = basic_etc.m_btCharRACE;
        p->m_gsv_SELECT_CHAR.m_nZoneNO = user->m_nZoneNO;
        p->m_gsv_SELECT_CHAR.m_PosSTART = user->m_PosCUR;
        p->m_gsv_SELECT_CHAR.m_nReviveZoneNO = user->m_nReviveZoneNO;
        p->m_gsv_SELECT_CHAR.m_dwUniqueTAG = user->m_dwDBID;

        std::copy(&basic_etc.m_PartITEM[0],
            &basic_etc.m_PartITEM[0] + MAX_BODY_PART,
            &p->m_gsv_SELECT_CHAR.m_PartITEM[0]);

        std::copy(&basic_etc.costume[0],
            &basic_etc.costume[0] + MAX_BODY_PART,
            &p->m_gsv_SELECT_CHAR.costume[0]);

        std::memcpy(&p->m_gsv_SELECT_CHAR.m_BasicINFO, &basic_info, sizeof(tagBasicINFO));
        std::memcpy(&p->m_gsv_SELECT_CHAR.m_BasicAbility, &basic_ability, sizeof(tagBasicAbility));
        std::memcpy(&p->m_gsv_SELECT_CHAR.m_GrowAbility, &grow_ability, sizeof(tagGrowAbility));
        std::memcpy(&p->m_gsv_SELECT_CHAR.m_Skill, &skill_ability, sizeof(tagSkillAbility));
        std::memcpy(&p->m_gsv_SELECT_CHAR.m_HotICONS, &hotbar_icons, sizeof(CHotICONS));

        p->AppendString(user->Get_NAME());

        user->SendPacket(p.get());
        user->Send_gsv_INVENTORYnQUEST_DATA();
    } else {
        user->m_bRunMODE = pPacket->m_cli_SELECT_CHAR.m_btRunMODE;
        user->m_btRideMODE = pPacket->m_cli_SELECT_CHAR.m_btRideMODE;
        user->Send_gsv_TELEPORT_REPLY(user->m_PosCUR, user->m_nZoneNO);
    }

    user->m_Bank.Init();
    user->m_Bank.m_i64ZULY = char_res.get_int64(0, COL_STORAGE_MONEY);
    user->m_btBankData = BANK_UNLOADED;

    user->m_dwLoginTIME = std::chrono::system_clock::now().time_since_epoch().count();

    return true;
}

bool
GS_CThreadSQL::Proc_cli_BANK_LIST_REQ(tagQueryDATA* pSqlPACKET) {
    t_PACKET* pPacket = reinterpret_cast<t_PACKET*>(pSqlPACKET->m_pPacket);
    if (!pPacket) {
        return false;
    }

    classUSER* user = reinterpret_cast<classUSER*>(g_pUserLIST->GetSOCKET(pSqlPACKET->m_iTAG));
    if (!user || !user->Get_ACCOUNT())
        return false;

    QueryResult storage_res = this->db.query(
        "SELECT storage.slot, storage.quantity, item.uuid, "
        "item.game_data_id, item.type_id, item.stat_id, item.grade, "
        "item.durability, item.lifespan, item.appraisal, item.socket, item.crafted "
        "FROM storage INNER JOIN item ON storage.item_id = item.id "
        "WHERE storage.owner_id = $1",
        {std::to_string(user->m_dwDBID)});

    enum StorageCol {
        STORAGE_COL_SLOT,
        STORAGE_COL_QUANTITY,
        STORAGE_COL_UUID,
        STORAGE_COL_GAME_DATA_ID,
        STORAGE_COL_TYPE_ID,
        STORAGE_COL_STAT_ID,
        STORAGE_COL_GRADE,
        STORAGE_COL_DURABILITY,
        STORAGE_COL_LIFESPAN,
        STORAGE_COL_APPRAISAL,
        STORAGE_COL_SOCKET,
        STORAGE_COL_CRAFTED,
    };

    if (!storage_res.is_ok()) {
        LOG_ERROR("Storage query failed for character '{}': {}",
            user->Get_NAME(),
            storage_res.error_message());
        return false;
    }

    if (storage_res.row_count == 0) {
        return user->Send_gsv_BANK_ITEM_LIST(true);
    }

    for (size_t row_idx = 0; row_idx < storage_res.row_count; ++row_idx) {
        int slot = storage_res.get_int32(row_idx, STORAGE_COL_SLOT);
        if (slot < 0 || slot >= BANKSLOT_TOTAL) {
            continue;
        }

        tagITEM* item = &user->m_Bank.m_ItemLIST[slot];
        if (!item) {
            item = new tagITEM();
            item->init();
        }
        item->uuid =
            Rose::Util::UUID::from_string(storage_res.get_string(row_idx, STORAGE_COL_UUID));
        item->m_nItemNo = storage_res.get_int32(row_idx, STORAGE_COL_GAME_DATA_ID);
        item->m_cType = storage_res.get_int32(row_idx, STORAGE_COL_TYPE_ID);

        if (item->IsEnableDupCNT()) {
            item->m_uiQuantity = storage_res.get_int32(row_idx, STORAGE_COL_QUANTITY);
        } else {
            item->m_bCreated = storage_res.get_bool(row_idx, STORAGE_COL_CRAFTED);
            item->m_bHasSocket = storage_res.get_bool(row_idx, STORAGE_COL_SOCKET);
            item->m_bIsAppraisal = storage_res.get_bool(row_idx, STORAGE_COL_APPRAISAL);
            item->m_cDurability = storage_res.get_int32(row_idx, STORAGE_COL_DURABILITY);
            item->m_cGrade = storage_res.get_int32(row_idx, STORAGE_COL_GRADE);
            item->m_nLife = storage_res.get_int32(row_idx, STORAGE_COL_LIFESPAN);
            item->m_nGEM_OP = storage_res.get_int32(row_idx, STORAGE_COL_STAT_ID);
        }
    }

    return user->Send_gsv_BANK_ITEM_LIST();
}