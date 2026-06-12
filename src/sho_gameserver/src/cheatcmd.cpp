#include "stdafx.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "rose/common/commands.h"
#include "rose/common/status_effect/status_effect.h"
#include "rose/common/util.h"

#include "LIB_gsMAIN.h"
#include "GS_USER.h"
#include "ZoneLIST.h"
#include "GS_ListUSER.h"
#include "IO_Quest.h"
#include "GS_SocketLSV.h"
#include "GS_ThreadSQL.h"

using namespace Rose;
using namespace Rose::Common;

// clang-format off
#define REGISTER_COMMAND(ID, FUNC) {commands[ID].name, {FUNC, commands[ID]}}


bool
help(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user) {
        return false;
    }

    for(const CommandInfo& cmd_info: commands) {
        if (user->m_dwRIGHT >= cmd_info.level) {
            std::string msg = cmd_info.name + std::string(" -- ") + cmd_info.description;
            user->send_server_whisper(msg);
        }
    }
    return true;
}

bool
hp(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user) {
        return false;
    }

    user->send_server_whisper(fmt::format("Current HP = {}", user->Get_HP()));
    return true;
}

bool
kill_all(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user || !user->GetZONE()) {
        return false;
    }

    user->GetZONE()->Kill_AllMOB(user, true);
    return true;
}

bool
levelup(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user) {
        return false;
    }

    if (args.size() < 2) {
        user->send_server_whisper(info.usage);
        return false;
    }

    const int level_amount = std::atoi(args[1].c_str());
    if (level_amount <= 0) {
        user->send_server_whisper("Invalid level amount, must be positive.");
        return false;
    }

    user->level_up(level_amount);

    return true;
}

bool
maps(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user) {
        return false;
    }

    user->send_server_whisper("| ID | Name |");
    user->send_server_whisper("-------------");

    for (int zone_idx = 0; zone_idx < g_TblZONE.row_count; ++zone_idx) {
        const char* zone_name = ZONE_NAME(zone_idx);
        if (!zone_name) {
            continue;
        }

        std::stringstream message("| ");
        message << std::setfill('0') << std::setw(2) << zone_idx << " | "
                << std::string(zone_name);

        user->send_server_whisper(message.str());
    }
    return true;
}

bool
online(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user) {
        return false;
    }

    // TODO: RAM: Not an ideal implementation. Non-performant if many players.
    // Ergonomics could be improved (e.g. sort by name).
    g_pUserLIST->for_each_user([user](classUSER* user2) {
        user->send_server_whisper(fmt::format("{} (level {})", user2->Get_NAME(), user2->Get_LEVEL() ));
    });

    user->send_server_whisper(fmt::format("Players Online: {}", g_pUserLIST->Get_AccountCNT()));

    return true;
}

bool
rates(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user) {
        return false;
    }

    user->send_server_whisper("Drop: " + std::to_string(Get_WorldDROP()));
    user->send_server_whisper("Money: " + std::to_string(Get_WorldDROP_M()));
    user->send_server_whisper("Exp: " + std::to_string(Get_WorldEXP()));
    user->send_server_whisper("Craft: " + std::to_string(Get_WorldPROD()));
    user->send_server_whisper("Prices: " + std::to_string(Get_WorldRATE()));
    user->send_server_whisper("Reward: " + std::to_string(Get_WorldREWARD()));
    user->send_server_whisper("Stamina: " + std::to_string(Get_WorldSTAMINA()));

    return true;
}

bool
reload_config(classUSER* user, CommandInfo, const std::vector<std::string>&) {
    const bool res = reload_server_game_config();
    if (res) {
        user->send_server_whisper("Server game config reloaded");
    } else {
        user->send_server_whisper("Server game config failed");
    }
    return res;
}

bool
stats(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user) {
        return false;
    }

    classUSER* target = user;
    if (args.size() >= 2) {
        classUSER* user2 = g_pUserLIST->Find_CHAR(const_cast<char*>(args[1].c_str()));
        if (user2) {
            target = user2;
        }
    }

    user->send_server_whisper(fmt::format("Stats for {}", user->Get_NAME()));
    user->send_server_whisper(fmt::format("HP: {}/{}",target->Get_HP(), target->Get_MaxHP()));
    user->send_server_whisper(fmt::format("MP: {}/{}",target->Get_MP(), target->Get_MaxMP()));
    user->send_server_whisper("MSPD: " + std::to_string(target->total_move_speed()));
    user->send_server_whisper("ASPD: " + std::to_string(target->total_attack_speed()));
    user->send_server_whisper("ATK: " + std::to_string(target->total_attack_power()));
    user->send_server_whisper("HIT: " + std::to_string(target->total_hit_rate()));

    return true;
}

bool
teleport(classUSER* user, CommandInfo info, const std::vector<std::string>& args) {
    if (!user) {
        return false;
    }

    if (args.size() < 2) {
        user->send_server_whisper(info.usage);
        return false;
    }

    const int map_id = std::atoi(args[1].c_str());
    if (!g_pZoneLIST->IsValidZONE(map_id)) {
        user->send_server_whisper("Invalid map id");
        return false;
    }

    tPOINTF spawn = g_pZoneLIST->GetZONE(map_id)->Get_StartPOS();
    if (args.size() >= 4) {
        spawn.x = atoi(args[2].c_str()) * 1000.0f;
        spawn.y = atoi(args[3].c_str()) * 1000.0f;
    }

    POINTS sector_pos;
    sector_pos.x = spawn.x / g_pZoneLIST->GetSectorSIZE(map_id);
    sector_pos.y = spawn.y / g_pZoneLIST->GetSectorSIZE(map_id);

    if (sector_pos.x < 0 && sector_pos.y < 0) {
        user->send_server_whisper("Invalid coordinates");
        return false;
    }

    user->Proc_TELEPORT(map_id, spawn);
    return true;
}

using CommandFunction = std::function<bool(classUSER*, CommandInfo, const std::vector<std::string>&)>;
static const std::unordered_map<std::string, std::tuple<CommandFunction, CommandInfo>>
    command_registry = {
        REGISTER_COMMAND(Command::HELP, help),
        REGISTER_COMMAND(Command::HP, hp),
        REGISTER_COMMAND(Command::KILL_ALL, kill_all),
        REGISTER_COMMAND(Command::LEVELUP, levelup),
        REGISTER_COMMAND(Command::MAPS, maps),
        REGISTER_COMMAND(Command::ONLINE, online),
        REGISTER_COMMAND(Command::RATES, rates),
        REGISTER_COMMAND(Command::STATS, stats),
        REGISTER_COMMAND(Command::TELEPORT, teleport),
        REGISTER_COMMAND(Command::RELOAD_CONFIG, reload_config),
    };

char* l_szAbility[] = {"STR",
    "DEX",
    "INT",
    "CON",
    "CHA",
    "SEN",

    NULL};

DWORD
classUSER::A_Cheater() {
    return this->m_dwRIGHT & RIGHT_MASTER;
}

DWORD
classUSER::B_Cheater() {
    return this->m_dwRIGHT & (RIGHT_MASTER | RIGHT_MG);
}

DWORD
classUSER::C_Cheater() {
    return this->m_dwRIGHT & (RIGHT_MASTER | RIGHT_DEV | RIGHT_MG | RIGHT_NG);
}

DWORD
classUSER::GM_Cheater() {
    return this->m_dwRIGHT & (RIGHT_MASTER | RIGHT_MG | RIGHT_NG);
}

DWORD
classUSER::TWGM_Cheater() {
    return this->m_dwRIGHT & (RIGHT_TWG | RIGHT_MG | RIGHT_DEV | RIGHT_MASTER);
}

short
classUSER::Cheat_where(CStrVAR* pStrVAR,
    char* pArg1 /*szCharName*/,
    char* pArg2 /*szAccount*/,
    char* szCode) {
    classUSER* pUSER = NULL;
    if (pArg2) {
        pUSER = g_pUserLIST->Find_ACCOUNT(pArg2);
    } else {
        pUSER = g_pUserLIST->Find_CHAR(pArg1);
    }

    if (pUSER) {
        if (pUSER->GetZONE()) {
            // CStrVAR *pCStr = this->GetZONE()->GetStrVAR ();

            pStrVAR->Printf("%s:%d ( %.0f, %.0f )",
                pUSER->GetZONE()->Get_NAME(),
                pUSER->GetZONE()->Get_ZoneNO(),
                pUSER->m_PosCUR.x,
                pUSER->m_PosCUR.y);
            this->Send_gsv_WHISPER(pUSER->Get_NAME(), pStrVAR->Get());
        }
    } else {
        // 월드 서버로 전송.
        g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
    }
    return CHEAT_NOLOG;
}

short
classUSER::Cheat_account(char* pArg1, char* szCode) {
    // 케릭 이름으로 계정 얻기
    classUSER* pUSER = NULL;
    pUSER = g_pUserLIST->Find_CHAR(pArg1);
    if (pUSER && pUSER->m_dwRIGHT <= RIGHT_DEV) {
        this->Send_gsv_WHISPER(pUSER->Get_NAME(), pUSER->Get_ACCOUNT());
    } else {
        // 월드 서버로 전송.
        g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
    }
    return CHEAT_NOLOG;
}

short
classUSER::Cheat_move(char* pArg1, char* pArg2, char* szCode) {
    classUSER* pUSER = NULL;
    if (pArg2) {
        pUSER = g_pUserLIST->Find_ACCOUNT(pArg2);
    } else if (pArg1) {
        pUSER = g_pUserLIST->Find_CHAR(pArg1);
    }

    if (pUSER) {
        if (pUSER->GetZONE()) {
            this->Proc_TELEPORT(pUSER->GetZONE()->Get_ZoneNO(), pUSER->m_PosCUR); // "/move"
        }
    } else {
        // 월드 서버로 전송.
        g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
    }
    return CHEAT_NOLOG;
}
// C등급..
short
classUSER::Cheat_mm(short nZoneNO, char* pArg2, char* pArg3) {
    if (!this->Is_CartGuest() && g_pZoneLIST->IsValidZONE(nZoneNO)) {
        tPOINTF PosWARP;

        PosWARP.x = atoi(pArg2) * 1000.f;
        PosWARP.y = atoi(pArg3) * 1000.f;

        POINTS PosSEC;
        PosSEC.x = (short)(PosWARP.x / g_pZoneLIST->GetSectorSIZE(nZoneNO));
        PosSEC.y = (short)(PosWARP.y / g_pZoneLIST->GetSectorSIZE(nZoneNO));

        // y축 체크는 안됨 - 로컬존이 아닐경우 g_pZoneLIST->GetZONE(nZoneNO)->Get_SectorYCNT()에서
        // 뻑~~
        if (PosSEC.x >= 0 && PosSEC.y >= 0) {
            this->Proc_TELEPORT(nZoneNO, PosWARP); // "/mm"
            return CHEAT_NOLOG;
        }
    }
    return CHEAT_INVALID;
}
// C등급
short
classUSER::Cheat_call(char* pArg1, char* pArg2, char* szCode) {
    classUSER* pUSER = NULL;
    if (pArg2) {
        pUSER = g_pUserLIST->Find_ACCOUNT(pArg2);
    } else if (pArg1) {
        pUSER = g_pUserLIST->Find_CHAR(pArg1);
    }
    if (pUSER) {
        if (this->m_dwRIGHT >= pUSER->m_dwRIGHT) {
            // pUSER에게 바로 Send_gsv_TELEPORT_REPLY을 날리는것은
            // pUSER의 존이 NULL이 되어 운이 나쁘면 pUSER루프시 뻑~~~
            pUSER->Send_gsv_RELAY_REQ(RELAY_TYPE_RECALL,
                this->GetZONE()->Get_ZoneNO(),
                this->m_PosCUR);
        }
    } else {
        // 월드 서버로 전송.
        // 나의 존/좌표를 전송하여 상대방으로 하여금 오게끔...
        g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
    }
    return CHEAT_INVALID;
}
// C등급..
short
classUSER::Cheat_out(char* pArg1, char* pArg2, char* szCode) {
    classUSER* pUSER = NULL;
    if (pArg2)
        pUSER = g_pUserLIST->Find_ACCOUNT(pArg2);
    else if (pArg1)
        pUSER = g_pUserLIST->Find_CHAR(pArg1);

    if (pUSER) {
        if (this->m_dwRIGHT >= pUSER->m_dwRIGHT) {
            // 권한이 같거나 높아야...
            pUSER->Send_gsv_GM_COMMAND(this->Get_NAME(), GM_CMD_LOGOUT);
            return CHEAT_PROCED;
        }
    } else {
        g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
    }
    return CHEAT_INVALID;
}
// C등급..
short
classUSER::Cheat_shut(char* pArg1, char* pArg2, char* pArg3, char* szCode) {
    WORD wBlockTIME = 1;
    if (pArg2) {
        wBlockTIME = (WORD)atoi(pArg2);
        if (wBlockTIME > 20)
            wBlockTIME = 20;
    }

    classUSER* pUSER = NULL;
    if (pArg3)
        pUSER = g_pUserLIST->Find_ACCOUNT(pArg3);
    else if (pArg1)
        pUSER = g_pUserLIST->Find_CHAR(pArg1);

    if (pUSER) {
        if (this->m_dwRIGHT >= pUSER->m_dwRIGHT) {
            pUSER->Send_gsv_GM_COMMAND(this->Get_NAME(), GM_CMD_SHUT, wBlockTIME);
            return CHEAT_PROCED;
        }
    } else {
        g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
    }
    return CHEAT_INVALID;
}

// 선택적...
short
classUSER::Cheat_npc(CStrVAR* pStrVAR,
    CObjNPC* pNPC,
    int iNpcIDX,
    char* pArg2,
    char* pArg3,
    char* pArg4) {
    if (pArg2) {
        if (!strcmpi(pArg2, "ENABLE")) {
            ;
        } else if (!strcmpi(pArg2, "DISABLE")) {
            ;
        } else if (!strcmpi(pArg2, "MOVE")) { // 모두 가능
            if (pNPC->GetZONE() && pNPC->GetZONE()->Get_ZoneNO()) {
                this->Proc_TELEPORT(pNPC->GetZONE()->Get_ZoneNO(), pNPC->m_PosCUR);
            }
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg2, "ADD")) {
            if (A_Cheater() && pArg3) {
                int iQuestIDX = atoi(pArg3);
                if (iQuestIDX >= 0) {
                    pNPC = new CObjNPC;
                    // float fModelDIR = quaternionToModelDirection( fQW, fQX, fQY, fQZ );
                    // ( (CObjNPC*)pNewCHAR )->Set_DIR( fModelDIR );
                    if (!pNPC->Init(this->GetZONE(),
                            iNpcIDX,
                            this->m_PosCUR.x,
                            this->m_PosCUR.y,
                            TEAMNO_NPC,
                            NULL,
                            iQuestIDX)) {
                        SAFE_DELETE(pNPC);
                    } else
                        g_pZoneLIST->Add_LocalNPC(pNPC);
                }
            }
        } else if (!strcmpi(pArg2, "CALL")) {
            if (A_Cheater() && this->GetZONE() == pNPC->GetZONE()) {
                this->GetZONE()->Sub_DIRECT(pNPC);
                pNPC->m_PosCUR = this->m_PosCUR;
                this->GetZONE()->Add_OBJECT(pNPC);
            }
        } else if (!strcmpi(pArg2, "DEL")) {
            if (A_Cheater() && this->GetZONE() == pNPC->GetZONE()) {
                this->GetZONE()->Sub_DIRECT(pNPC);
                SAFE_DELETE(pNPC);
            }
        } else if (!strcmpi(pArg2, "EVENT")) {
            if (B_Cheater() && pArg3) {
                int iQuestIDX = atoi(pArg3);
                if (iQuestIDX >= 0)
                    pNPC->m_nQuestIDX = iQuestIDX;
            }
        } else if (!strcmpi(pArg2, "DIR")) {
            if (B_Cheater() && pArg3) {
                float fDir = (float)atof(pArg3);
                pNPC->Set_DIR(fDir);
            }
        } else if (!strcmpi(pArg2, "VAR") && pArg3 && pArg4) {
            if (B_Cheater() && pArg4) {
                int iVarIDX = atoi(pArg3);
                int iVarVALUE = atoi(pArg4);
                pNPC->Set_ObjVAR(iVarIDX, iVarVALUE);
            }
        }
        /*
        else
        if ( !strcmpi(pArg2, "getVAR") && pArg3 ) {
            int iVarIDX   = atoi( pArg3 );
            xxx = pNPC->Get_ObjVAR( iVarIDX, iVarVALUE );
            pStrVAR->Printf ("%s Zone:%d, Pos(%.0f,%.0f)", pNPC->Get_NAME(),
        pNPC->GetZONE()->Get_ZoneNO(), pNPC->m_PosCUR.x, pNPC->m_PosCUR.y ); this->Send_gsv_WHISPER(
        "SERVER", pStrVAR->Get() );
        }
        */
        return CHEAT_PROCED;
    }

    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return CHEAT_INVALID;

    pCPacket->m_HEADER.m_wType = GSV_SERVER_DATA;
    pCPacket->m_HEADER.m_nSize = sizeof(gsv_SERVER_DATA);
    pCPacket->m_gsv_SERVER_DATA.m_btDataTYPE = SERVER_DATA_NPC;
    pCPacket->AppendData(pNPC->m_pVAR, sizeof(tagObjVAR));

    this->SendPacket(pCPacket);
    Packet_ReleaseNUnlock(pCPacket);

    pStrVAR->Printf("%s Zone:%d, Pos(%.0f,%.0f)",
        pNPC->Get_NAME(),
        pNPC->GetZONE()->Get_ZoneNO(),
        pNPC->m_PosCUR.x,
        pNPC->m_PosCUR.y);
    this->Send_gsv_WHISPER("SERVER", pStrVAR->Get());

    return CHEAT_PROCED;
}

short
classUSER::Cheat_add(char* pArg1, char* pArg2, char* pArg3, char* szCode) {
    if (B_Cheater()) {
        // /add goddess <target>
        if (!strcmpi(pArg1, "goddess")) {
            classUSER* target = this;
            if (pArg2) {
                target = g_pUserLIST->Find_CHAR(pArg2);
                if (!target || this->GetZONE() != target->GetZONE()) {
                    return CHEAT_INVALID;
                }
            }

            target->m_IngSTATUS.enable_status(StatusEffectType::Goddess);
            target->UpdateAbility();

            Send_gsv_CHARSTATE_CHANGE(target->m_IngSTATUS.m_dwSubStatusFLAG);
            return CHEAT_PROCED;
        }

        if (NULL == pArg2) {
            if (!strcmpi(pArg1, "arua")) {
                // this->Del_Goddess ();
                this->Add_Goddess();
                return CHEAT_PROCED;
            }
        } else {
            classUSER* pUSER = NULL;
            if (pArg3) {
                // 올려줄 대상이 있는가 ?
                pUSER = g_pUserLIST->Find_CHAR(pArg3);
                if (NULL == pUSER) {
                    // 월드 서버로 전송.
                    g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
                    return CHEAT_PROCED;
                }
            } else {
                pUSER = this;
            }

            if (NULL == pUSER->GetZONE()) {
                return 0;
            }
            if (!strcmpi(pArg1, "EXP")) {
                int iExp = atoi(pArg2);
                if (iExp < 0)
                    iExp = 1;

                // 경험치를 올려줄 대상이 있는가 ?
                pUSER->Add_EXP(iExp, true, 0);
                return CHEAT_PROCED;
            } else if (!strcmpi(pArg1, "BP")) {
                int iValue = atoi(pArg2);
                pUSER->AddCur_BonusPOINT(iValue);

                if (pUSER->GetCur_BonusPOINT() < 0)
                    pUSER->SetCur_BonusPOINT(0);
                else if (pUSER->GetCur_BonusPOINT() > 9999)
                    pUSER->SetCur_BonusPOINT(9999);
                return CHEAT_PROCED;
            } else if (!strcmpi(pArg1, "SP")) {
                int iValue = atoi(pArg2);
                pUSER->AddCur_SkillPOINT(iValue);

                if (pUSER->GetCur_SkillPOINT() < 0)
                    pUSER->SetCur_BonusPOINT(0);
                else if (pUSER->GetCur_SkillPOINT() > 9999)
                    pUSER->SetCur_BonusPOINT(9999);
                return CHEAT_PROCED;
            } else if (!strcmpi(pArg1, "UP")) {
                if (pUSER->GetCur_UNION()) {
                    int iValue = atoi(pArg2);
                    pUSER->AddCur_UnionPOINT(pUSER->GetCur_UNION(), iValue);

                    if (pUSER->GetCur_UnionPOINT(pUSER->GetCur_UNION()) < 0)
                        pUSER->SetCur_UnionPOINT(pUSER->GetCur_UNION(), 0);
                    else if (pUSER->GetCur_UnionPOINT(pUSER->GetCur_UNION()) > 99999)
                        pUSER->SetCur_UnionPOINT(pUSER->GetCur_UNION(), 99999);
                }
                return CHEAT_PROCED;
            } else if (!strcmpi(pArg1, "MONEY")) {
                pUSER->Add_CurMONEY(atoi(pArg2));
                return CHEAT_PROCED;
            } else if (!strcmpi(pArg1, "SKILL")) { // 스킬을 배운것으로...
                short nSkillIDX = atoi(pArg2);
                if (nSkillIDX >= 1 && nSkillIDX < g_SkillList.Get_SkillCNT()) {
                    if (SKILL_ICON_NO(nSkillIDX)) {
                        pUSER->Send_gsv_SKILL_LEARN_REPLY(nSkillIDX);
                    }
                }
                return CHEAT_PROCED;
            } else if (!strcmpi(pArg1, "QUEST")) {
                short nQuestIDX = atoi(pArg2);
                short nSlot = this->Quest_Append(nQuestIDX);
                if (nSlot >= 0)
                    this->Send_gsv_QUEST_REPLY(RESULT_QUEST_REPLY_ADD_SUCCESS,
                        (BYTE)nSlot,
                        nQuestIDX);
                else
                    this->Send_gsv_QUEST_REPLY(RESULT_QUEST_REPLY_ADD_FAILED, 0, nQuestIDX);

                return CHEAT_PROCED;
            }
        }
    }
    return CHEAT_INVALID;
}

short
classUSER::Cheat_del(CStrVAR* pStrVAR, char* pArg1, char* pArg2, char* pArg3) {
    // /del goddess <target>
    if (!strcmpi(pArg1, "goddess")) {
        classUSER* target = this;
        if (pArg2) {
            target = g_pUserLIST->Find_CHAR(pArg2);

            if (!target || this->GetZONE() != target->GetZONE()) {
                return CHEAT_INVALID;
            }
        }

        target->m_IngSTATUS.disable_status(StatusEffectType::Goddess);
        target->UpdateAbility();

        Send_gsv_CHARSTATE_CHANGE(target->m_IngSTATUS.m_dwSubStatusFLAG);
        return CHEAT_PROCED;
    }

    if (!strcmpi(pArg1, "arua")) {
        this->Del_Goddess();
        return CHEAT_PROCED;
    }

    if (A_Cheater() && pArg2) {
        classUSER* pUSER = NULL;
        if (pArg3) {
            pUSER = g_pUserLIST->Find_CHAR(pArg3);
            if (NULL == pUSER || this->GetZONE() != pUSER->GetZONE()) {
                return CHEAT_INVALID;
            }
        } else {
            pUSER = this;
        }

        if (!strcmpi(pArg1, "HP")) {
            pUSER->Set_HP(1);
            pUSER->Set_MP(0);
            return CHEAT_PROCED;
        } else if (!strcmpi(pArg1, "EXP")) {
            pUSER->m_GrowAbility.m_lEXP = 0;
            pUSER->Add_EXP(0, false, 0);
            return CHEAT_PROCED;
        } else if (!strcmpi(pArg1, "BP")) {
            pUSER->m_GrowAbility.m_nBonusPoint = 0;
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "SP")) {
            pUSER->m_GrowAbility.m_nSkillPoint = 0;
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "SKILL")) {
            if (!strcmpi(pArg2, "ALL")) {
                pUSER->Reward_InitSKILL();
            } else {
                short nSkillIDX = atoi(pArg2);
                for (short nSlot = 0; nSlot < MAX_LEARNED_SKILL_CNT; nSlot++) {
                    if (0 == nSkillIDX || pUSER->m_Skills.m_nSkillINDEX[nSlot] == nSkillIDX)
                        pUSER->m_Skills.m_nSkillINDEX[nSlot] = 0;
                }
            }
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "MONEY")) {
            pUSER->SetCur_MONEY(0);
            pUSER->Send_gsv_SET_MONEYONLY(GSV_SET_MONEY_ONLY);
            return CHEAT_PROCED;
        } else if (!strcmpi(pArg1, "QUEST") && pUSER->GetZONE()) {
            // CStrVAR *pCStr = this->GetZONE()->GetStrVAR ();
            short nQuestIDX = atoi(pArg2);
            if (pUSER->Quest_Delete(nQuestIDX)) {
                pStrVAR->Printf("%d quest delete success", nQuestIDX);
            } else {
                pStrVAR->Printf("%d quest delete failed", nQuestIDX);
            }

            pUSER->Send_gsv_WHISPER("SERVER", pStrVAR->Get());
            return CHEAT_PROCED;
        }
    }
    return CHEAT_INVALID;
}

short
classUSER::Cheat_get(CStrVAR* pStrVAR, char* pArg1, char* pArg2, char* szCode) {
    if (!strcmpi(pArg1, "HP") || !strcmpi(pArg1, "TG")) {
        CObjCHAR* pUSER = NULL;
        if (pArg2) {
            pUSER = g_pUserLIST->Find_CHAR(pArg2);
        } else {
            if (!strcmpi(pArg1, "TG")) {
                pUSER = (CObjCHAR*)this->Get_TARGET(true);
            } else
                pUSER = this;
        }

        if (pUSER) {
            if (this->Get_RIGHT() >= pUSER->Get_RIGHT() && this->GetZONE()) {
                short nMovSpeed = (short)pUSER->total_move_speed();
                short nAtkSpeed = pUSER->total_attack_speed();
                BYTE btGndATT = (pUSER->GetZONE())
                    ? pUSER->GetZONE()->IsMovablePOS((int)(pUSER->m_PosCUR.x),
                        (int)(pUSER->m_PosCUR.y))
                    : 0;

                if (pUSER->IsUSER()) {
                    std::ostringstream output;
                    output << "HP: " << pUSER->Get_HP() << "/" << pUSER->Get_MaxHP() << ", "
                           << "MP:" << pUSER->Get_MP() << "/" << pUSER->Get_MaxMP() << ","
                           << "LEV: " << pUSER->Get_LEVEL() << ", "
                           << "EXP: " << pUSER->Get_EXP() << "/"
                           << ((classUSER*)pUSER)->Get_NeedEXP(pUSER->Get_LEVEL()) << ", "
                           << "JOB: " << pUSER->Get_JOB()
                           << ", "
                              "ANI:(M: "
                           << std::fixed << std::setprecision(2) << m_fRunAniSPEED
                           << ",A:" << pUSER->total_attack_speed() << "), "
                           << "Atk: " << pUSER->total_attack_power() << ", "
                           << "Def: " << pUSER->Get_DEF() << ", "
                           << "Res: " << pUSER->Get_RES() << ", "
                           << "Hit: " << pUSER->total_hit_rate() << ", "
                           << "Crit: " << pUSER->Get_CRITICAL() << ", "
                           << "Dodge: " << pUSER->Get_AVOID() << ", "
                           << "Aspd: " << pUSER->total_attack_speed() << ", "
                           << "Mspd: " << (int)pUSER->total_move_speed();

                    this->Send_gsv_WHISPER(pUSER->Get_NAME(), (char*)output.str().c_str());
                    return CHEAT_NOLOG;
                } else {
                    pStrVAR->Printf(
                        "HP:%d/%d \nMP:%d/%d \nLEV:%d (EXP:%d), JOB:%d, SPD(M:%d,A:%d), Hit:%d, "
                        "Crt:%d, AP:%d, DP:%d, AVD:%d, ATTR:%d, bCST:%d, SKL:%d, FLG:%x",
                        pUSER->Get_HP(),
                        pUSER->Get_MaxHP(),
                        pUSER->Get_MP(),
                        pUSER->Get_MaxMP(),
                        pUSER->Get_LEVEL(),
                        pUSER->Get_EXP(),
                        pUSER->Get_JOB(),
                        nMovSpeed,
                        nAtkSpeed,
                        pUSER->total_hit_rate(),
                        pUSER->Get_CRITICAL(),
                        pUSER->total_attack_power(),
                        pUSER->Get_DEF(),
                        pUSER->Get_AVOID(),
                        btGndATT,
                        pUSER->m_bCastingSTART,
                        pUSER->Get_ActiveSKILL(),
                        pUSER->m_IngSTATUS.GetFLAGs());
                }
                this->Send_gsv_WHISPER(pUSER->Get_NAME(), pStrVAR->Get());
            }
        } else {
            // 월드 서버로 전송.
            g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
        }
        return CHEAT_NOLOG;
    }

    if (!strcmpi(pArg1, "USER") && this->GetZONE()) {
        pStrVAR->Printf("Server users:%d, Zone user/char: %d/%d",
            g_pUserLIST->GetUsedSocketCNT(),
            this->GetZONE()->Get_UserCNT(),
            this->GetZONE()->GetGameObjCNT());
        this->Send_gsv_WHISPER("<SERVER>::", pStrVAR->Get());
        return CHEAT_NOLOG;
    } else if (!strcmpi(pArg1, "PARTY")) {
        if (this->GetPARTY()) {
            pStrVAR->Printf("Party users:%d, Lev:%d, Exp:%d/%d, AvgLEV:%d, Option: %x",
                this->GetPARTY()->GetMemberCNT(),
                this->GetPARTY()->GetPartyLEV(),
                this->GetPARTY()->GetPartyEXP(),
                CCal::Get_PartyLevelUpNeedEXP(this->GetPARTY()->GetPartyLEV()),
                this->GetPARTY()->GetAverageLEV(),
                this->GetPARTY()->m_btPartyRULE);
            this->Send_gsv_WHISPER("<SERVER>::", pStrVAR->Get());
        }
        return CHEAT_NOLOG;
    } else if (B_Cheater() && !strcmpi(pArg1, "EC")) {
        classPACKET* pCPacket = Packet_AllocNLock();
        if (!pCPacket)
            return 0;

        pCPacket->m_HEADER.m_wType = GSV_SERVER_DATA;
        pCPacket->m_HEADER.m_nSize = sizeof(gsv_SERVER_DATA);
        pCPacket->m_gsv_SERVER_DATA.m_btDataTYPE = SERVER_DATA_ECONOMY;
        pCPacket->AppendData(this->GetZONE()->m_Economy.m_pEconomy, sizeof(tagECONOMY));

        this->SendPacket(pCPacket);
        Packet_ReleaseNUnlock(pCPacket);

        return CHEAT_NOLOG;
    } else if (B_Cheater() && !strcmpi(pArg1, "WORLD") && this->GetZONE()) {
        pStrVAR->Printf("wDrop:%d, wDropM:%d, wEXP:%d, wPROD:%d, wRATE:%d, wREWARD:%d, "
                        "wSTAMINA:%d, QUEST(Enable:%d, Ref:%d)",
            Get_WorldDROP(),
            Get_WorldDROP_M(),
            Get_WorldEXP(),
            Get_WorldPROD(),
            Get_WorldRATE(),
            Get_WorldREWARD(),
            Get_WorldSTAMINA(),
            g_QuestList.IsEnable(),
            g_QuestList.GetRefCNT());
        this->Send_gsv_WHISPER("<WORLD>::", pStrVAR->Get());
        return CHEAT_NOLOG;
    }
    return CHEAT_INVALID;
}

// A 등급
short
classUSER::Cheat_item(char* pArg1, char* pArg2, char* pArg3, char* pArg4) {
    if (B_Cheater()) {
        int item_type = atoi(pArg1);
        int item_id = atoi(pArg2);

        if (item_type < ITEM_TYPE_FACE_ITEM || item_type > ITEM_TYPE_RIDE_PART) {
            this->Send_gsv_WHISPER("Server", "Invalid item type");
            return CHEAT_INVALID;
        }

        if (item_id <= 0 || item_id >= g_pTblSTBs[item_type]->row_count) {
            this->Send_gsv_WHISPER("Server", "Invalid item id");
            return CHEAT_INVALID;
        }

        int iDupCNT = atoi(pArg3);

        tagITEM item;
        item.Init(item_type, item_id, iDupCNT);

        if (item.m_cType > 0 && item.m_cType < ITEM_TYPE_MONEY) {
            if (item.m_cType >= ITEM_TYPE_USE && item.m_cType != ITEM_TYPE_RIDE_PART) {
                if (ITEM_ICON_NO(item_type, item_id) <= 0) {
                    this->Send_gsv_WHISPER("Server", "Invalid item: Invalid icon id");
                    return CHEAT_INVALID;
                }

                if (iDupCNT > 100) {
                    iDupCNT = 100;
                } else if (iDupCNT < 1) {
                    iDupCNT = 1;
                }

                item.m_uiQuantity = iDupCNT;
            } else {
                if (ITEM_ICON_NO(item_type, item_id) <= 0) {
                    this->Send_gsv_WHISPER("Server", "Invalid item: Invalid icon id");
                    return CHEAT_INVALID;
                }

                item.m_nLife = MAX_ITEM_LIFE;
                item.m_cDurability = ITEM_DURABITY(item.m_cType, item.m_nItemNo);
                item.m_nGEM_OP = iDupCNT % 301;
                item.m_bIsAppraisal = 1;
                if (0 == item.m_nGEM_OP) {
                    if (pArg4 && ITEM_RARE_TYPE(item.GetTYPE(), item.GetItemNO())) {
                        if (atoi(pArg4)) {
                            item.m_bHasSocket = 1;
                        }
                    }
                }
            }

            short nInvIDX = this->Add_ITEM(item);
            if (nInvIDX >= 0) {
                std::string msg = fmt::format("Spawning item {}:{} ({})", item_type, item_id, iDupCNT);
                this->Send_gsv_SET_INV_ONLY((BYTE)nInvIDX, &item);
                this->Send_gsv_WHISPER("Server", const_cast<char*>(msg.c_str()));
                return CHEAT_PROCED;
            }
        }
    }
    return CHEAT_INVALID;
}

// B등급..
short
classUSER::Cheat_mon(char* pArg1, char* pArg2) {
    if (B_Cheater()) {
        int iMobIDX = atoi(pArg1);
        int iMobCNT = atoi(pArg2);
        if (iMobCNT > 100)
            iMobCNT = 100;
        this->GetZONE()->RegenCharacter(this->m_PosCUR.x,
            this->m_PosCUR.y,
            1500,
            iMobIDX,
            iMobCNT,
            TEAMNO_MOB,
            true);
    }
    return CHEAT_PROCED;
}

//
short
classUSER::Cheat_mon2(char* pArg1, char* pArg2, char* pArg3, char* pArg4) {
    if (!pArg1 || !pArg2 || !pArg3 || !pArg4)
        return CHEAT_INVALID;

    if (TWGM_Cheater()) {
        int iMobIDX = atoi(pArg1);
        int iX = atoi(pArg2);
        int iY = atoi(pArg3);
        int iMobCNT = atoi(pArg4);
        if (iMobCNT > 100)
            iMobCNT = 100;
        this->GetZONE()
            ->RegenCharacter(iX * 1000, iY * 1000, 1500, iMobIDX, iMobCNT, TEAMNO_MOB, true);
        return CHEAT_PROCED;
    }
    return CHEAT_INVALID;
}

// B등급..
short
classUSER::Cheat_damage(char* pArg1, char* pArg2, char* pArg3) {
    if (B_Cheater()) {
        int iDistance = atoi(pArg1);
        int iDamage = atoi(pArg2);

        if (iDistance < 1000)
            iDistance = 1000;

        uniDAMAGE sDamage;
        if (iDamage < 0 || iDamage >= GameStaticConfig::MAX_DAMAGE)
            sDamage.m_wDamage = GameStaticConfig::MAX_DAMAGE;
        else
            sDamage.m_wDamage = iDamage;

        if (pArg3) {
            int iDamageType = atoi(pArg3);
            switch (iDamageType) {
                case 1:
                    sDamage.m_wDamage |= DMG_BIT_HITTED;
                    break; // 맞는 동작을 한다.
                case 2:
                    sDamage.m_wDamage |= DMG_BIT_CRITICAL;
                    break; // 크리티컬 데미지다
            }
        }

        CObjCHAR* pTarget;
        pTarget = (CObjCHAR*)this->AI_FindFirstOBJ(iDistance);
        while (pTarget) {
            if (pTarget->IsA(OBJ_MOB)) {
                if (iDamage >= pTarget->Get_HP())
                    pTarget->Set_HP(1);
                this->Give_DAMAGE(pTarget, sDamage);
            }

            pTarget = (CObjCHAR*)this->AI_FindNextOBJ();
        }
    }
    return CHEAT_PROCED;
}

short
classUSER::Cheat_quest(CStrVAR* pStrVAR, char* pArg1) {
    if (B_Cheater()) {
        t_HASHKEY HashQst = StrToHashKey(pArg1);

        // CStrVAR *pCStr;
        if (QST_RESULT_SUCCESS == g_QuestList.CheckQUEST(this, HashQst, true)) {
            pStrVAR->Printf("quest check success: %s", pArg1);
            this->Send_gsv_QUEST_REPLY(RESULT_QUEST_REPLY_TRIGGER_SUCCESS, 0, (int)HashQst);
        } else {
            pStrVAR->Printf("quest check failed: %s", pArg1);
            this->Send_gsv_QUEST_REPLY(RESULT_QUEST_REPLY_TRIGGER_FAILED, 0, (int)HashQst);
        }
        this->Send_gsv_WHISPER("SERVER", pStrVAR->Get());
    }
    return CHEAT_PROCED;
}

short
classUSER::Cheat_set(classUSER* pUSER, char* pArg1, char* pArg2, char* pArg3) {
    if (NULL == pUSER)
        pUSER = this;

    if (B_Cheater()) {
        for (short nI = 0; l_szAbility[nI]; nI++) {
            if (!strcmpi(pArg1, l_szAbility[nI])) {
                int iValue = atoi(pArg2);

                if (iValue < 10)
                    iValue = 10;
                if (iValue > GameStaticConfig::MAX_STAT)
                    iValue = GameStaticConfig::MAX_STAT;

                pUSER->m_BasicAbility.m_nBasicA[nI] = iValue;
                pUSER->update_speed();
                return CHEAT_SEND;
            }
        }

        if (!strcmpi(pArg1, "JOB")) {
            short nWantJob = atoi(pArg2);
            short nJob, nJob10, nJob100;

            nJob = (nWantJob % 10); // 1단위 0~
            if (nJob && nJob < 3) {
                nJob10 = ((nWantJob % 100) / 10) % 4; // 10 단위
                nJob100 = (nWantJob / 100) % 5;
                if (nJob10 && nJob100) {
                    nJob += (nJob10 * 10);
                    nJob += (nJob100 * 100);
                }
            } else
                nJob = 0;
            pUSER->SetCur_JOB(nJob);
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "LEV")) {
            pUSER->Set_LEVEL(atoi(pArg2));
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "FAME")) {
            int iValue = atoi(pArg2);
            if (iValue < 0)
                iValue = 0;
            if (iValue > 99)
                iValue = 99;

            pUSER->SetCur_FAME(iValue);
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "MONEY")) {
            __int64 i64Money;
            i64Money = _atoi64(pArg2);
            pUSER->SetCur_MONEY(i64Money);
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "grade") && pArg3) {
            int iPos = atoi(pArg2);
            if (iPos > 0 && iPos < MAX_EQUIP_IDX) {
                if (this->m_Inventory.m_ItemEQUIP[iPos].GetHEADER()) {
                    int iGrade = atoi(pArg3);
                    if (iGrade < 0)
                        iGrade = 0;
                    if (iGrade > 9)
                        iGrade = 9;

                    this->m_Inventory.m_ItemEQUIP[iPos].m_cGrade = iGrade;
                }
            }
            return CHEAT_PROCED;
        }
    }

    if (B_Cheater()) {
        if (!strcmpi(pArg1, "worldDROP_M")) {
            int iValue = atoi(pArg2);
            if (RIGHT_MG == this->m_dwRIGHT) {
                if (iValue < 50)
                    iValue = 50;
                if (iValue > 300)
                    iValue = 300;
            }
            ::Set_WorldDROP_M(iValue);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg1, "worldDROP")) {
            int iValue = atoi(pArg2);
            if (RIGHT_MG == this->m_dwRIGHT) {
                if (iValue < 50)
                    iValue = 50;
                if (iValue > 200)
                    iValue = 200;
            }
            ::Set_WorldDROP(iValue);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg1, "worldEXP")) {
            int iValue = atoi(pArg2);
            if (RIGHT_MG == this->m_dwRIGHT) {
                if (iValue < 50)
                    iValue = 50;
                if (iValue > 300)
                    iValue = 300;
            }
            ::Set_WorldEXP(iValue);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg1, "worldPROD")) {
            int iValue = atoi(pArg2);
            if (RIGHT_MG == this->m_dwRIGHT) {
                if (iValue < 70)
                    iValue = 70;
                if (iValue > 130)
                    iValue = 130;
            }
            ::Set_WorldPROD(iValue);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg1, "worldRATE")) {
            int iValue = atoi(pArg2);
            if (RIGHT_MG == this->m_dwRIGHT) {
                if (iValue < 70)
                    iValue = 70;
                if (iValue > 120)
                    iValue = 120;
            }
            ::Set_WorldRATE(iValue);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg1, "worldREWARD")) {
            int iValue = atoi(pArg2);
            if (RIGHT_MG == this->m_dwRIGHT) {
                if (iValue < 50)
                    iValue = 50;
                if (iValue > 300)
                    iValue = 300;
            }
            ::Set_WorldREWARD(iValue);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg1, "worldSTAMINA")) {
            int iValue = atoi(pArg2);
            if (RIGHT_MG == this->m_dwRIGHT) {
                if (iValue < 0)
                    iValue = 0;
                if (iValue > 200)
                    iValue = 200;
            }
            ::Set_WorldSTAMINA(iValue);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg1, "union")) {
            int iValue = atoi(pArg2);
            if (iValue >= 0 && iValue < MAX_UNION_COUNT) {
                this->SetCur_UNION(iValue);
                return CHEAT_SEND;
            }
        }
#ifdef __KCHS_BATTLECART__
        else if (!strcmpi(pArg1, "maxpathp")) {
            int iValue = atoi(pArg2);
            this->Set_MaxPatHP((short)iValue);
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "pathp")) {
            int iValue = atoi(pArg2);
            this->SetCur_PatHP((short)iValue);
            return CHEAT_SEND;
        } else if (!strcmpi(pArg1, "pattime")) {
            int iValue = atoi(pArg2);
            this->SetCur_PatCoolTIME(iValue);
            Send_gsv_PATSTATE_CHAGE(0, this->GetCur_PatCoolTIME());
            return CHEAT_SEND;
        }
#endif
    }
    return CHEAT_INVALID;
}

//-------------------------------------------------------------------------------------------------
short
classUSER::Cheat_toggle(CStrVAR* pStrVAR, char* pArg1) {
    if (B_Cheater()) {
        if (!strcmpi(pArg1, "PK") && this->GetZONE()) {
            DWORD dwFLAG = this->GetZONE()->Toggle_PK_FLAG();

            // CStrVAR *pCStr = this->GetZONE()->GetStrVAR ();
            pStrVAR->Printf("PK Mode: %s ", (dwFLAG & ZONE_FLAG_PK_ALLOWED) ? "ON" : "OFF");
            this->Send_gsv_WHISPER("SERVER", pStrVAR->Get());
            return CHEAT_NOLOG;
        } else if (!strcmpi(pArg1, "QUEST")) {
            g_QuestList.ToggleEnable();
        } else if (!strcmpi(pArg1, "REGEN")) {
            pStrVAR->Printf("Zone %d Regen mode: %s ",
                this->GetZONE()->Get_ZoneNO(),
                this->GetZONE()->Toggle_REGEN() ? "ON" : "OFF");
            this->Send_gsv_WHISPER("SERVER", pStrVAR->Get());
        }
    }

    return CHEAT_INVALID;
}

short
classUSER::Cheat_regen(CStrVAR* pStrVAR, char* pArg1, char* pArg2) {
    if (A_Cheater()) {
        if (!strcmpi(pArg1, "COUNT")) {
            pStrVAR->Printf("RegenPointCNT: %d", this->GetZONE()->GetRegenPointCNT());
            this->Send_gsv_WHISPER("<SERVER>::", pStrVAR->Get());
        } else if (!strcmpi(pArg1, "INFO")) {
            if (pArg2) {
                int iPointIDX = atoi(pArg2);
                CRegenPOINT* pRegen = this->GetZONE()->GetRegenPOINT(iPointIDX);
                if (pRegen) {
                    pStrVAR->Printf("%dth RegenINFO(%.0f,%.0f):: Live:%d/%d, Tactics:%d/%d",
                        iPointIDX,
                        pRegen->m_fXPos,
                        pRegen->m_fYPos,
                        pRegen->GetLiveCNT(),
                        pRegen->m_iLimitCNT,
                        pRegen->m_iCurTactics,
                        pRegen->m_iTacticsPOINT);
                    this->Send_gsv_WHISPER("<SERVER>::", pStrVAR->Get());
                }
            }
        } else if (!strcmpi(pArg1, "MOB") && pArg2) {
            int iPointIDX = atoi(pArg2);
            this->GetZONE()->GetRegenPointMOB(iPointIDX, this);
        } else if (!strcmpi(pArg1, "CALL") && pArg2) {
            int iPointIDX = atoi(pArg2);
            int iCallCNT = this->GetZONE()->CallRegenPointMOB(iPointIDX);
            pStrVAR->Printf("%dth RegenCALL: %d mobs", iPointIDX, iCallCNT);
            this->Send_gsv_WHISPER("<SERVER>::", pStrVAR->Get());
        } else if (!strcmpi(pArg1, "NEAR")) {
            pStrVAR->Printf("Nearst RegenPNT:: %dth",
                this->GetZONE()->GetNearstRegenPOINT(this->m_PosCUR));
            this->Send_gsv_WHISPER("<SERVER>::", pStrVAR->Get());
        }
    }

    return CHEAT_NOLOG;
}

//-------------------------------------------------------------------------------------------------
// 2005-08-17 : kchs , 현재속도를 fRate(비율)만큼 증가시킴
short
classUSER::Cheat_speed(char* pArg1) {
    float fRate = (float)atof(pArg1);
    // m_nRunSPEED *= fRate;
    float calcspeed = static_cast<float>(this->stats.move_speed);
    calcspeed *= fRate;
    if (calcspeed > MAX_SHORT) {
        this->stats.move_speed = (MAX_SHORT);
    } else if (calcspeed < 100) {
        this->stats.move_speed = 100;
    } else {
        this->stats.move_speed = static_cast<uint16_t>(calcspeed);
    }
    Send_gsv_SPEED_CHANGED(false);

    return CHEAT_PROCED;
}

short
classUSER::Parse_CheatCODE(char* szCode) {
    // New cheat code handling
    std::vector<std::string> tokens = Util::split_string_whitespace(szCode);
    if (tokens.empty()) {
        return CHEAT_INVALID;
    }

    std::string& command_name = tokens.at(0);
    if (command_name.empty()) {
        return CHEAT_INVALID;
    }

    if (command_name.front() == '/') {
        command_name.erase(0, 1);
    }

    std::transform(command_name.begin(), command_name.end(), command_name.begin(), ::tolower);

    auto registered_command = command_registry.find(command_name);
    if (registered_command != command_registry.end()) {
        CommandFunction command_func = std::get<0>(registered_command->second);
        CommandInfo info = std::get<1>(registered_command->second);

        if(this->m_dwRIGHT < info.level) {
            this->send_server_whisper("You don't have permission to use this command.");
            return CHEAT_INVALID;
        }

        if (command_func(this, info, tokens)) {
            return CHEAT_PROCED;
        } else {
            return CHEAT_INVALID;
        }
    }

    // End new cheat code handling

    short nProcMODE = 0;

    char *pToken, *pArg1, *pArg2, *pArg3;
    char* pDelimiters = " ";

    CStrVAR* pStrVAR = this->GetZONE()->GetStrVAR();

    pToken = pStrVAR->GetTokenFirst(szCode, pDelimiters);
    pArg1 = pStrVAR->GetTokenNext(pDelimiters);
    if (NULL == pToken)
        return CHEAT_INVALID;

    if (pArg1) {
#ifdef __NEW_LOG
        if (!strcmpi(pToken, "/logtest")) {
            return 1;
        }
#endif

        if (!strcmpi(pToken, "/pay")) {
            int iPayType = atoi(pArg1);
            if (iPayType > 0 && iPayType <= BILLING_MSG_PAY_IQ) {
                this->Send_gsv_BILLING_MESSAGE(iPayType, "200512312400");
            }
        }
        if (!strcmpi(pToken, "/team")) {
            int iTeamNo = atoi(pArg1);
            if (iTeamNo >= 0) {
                this->m_iTeamNO = iTeamNo;
            }
        }
        if (!strcmpi(pToken, "/where")) {
            pArg2 = pStrVAR->GetTokenNext(pDelimiters); // account
            return Cheat_where(pStrVAR, pArg1, pArg2, szCode);
        }
        if (!strcmpi(pToken, "/account")) {
            return Cheat_account(pArg1, szCode);
        }
        if (!strcmpi(pToken, "/move")) {
            pArg2 = pStrVAR->GetTokenNext(pDelimiters); // account
            return Cheat_move(pArg1, pArg2, szCode);
        }
        // 맵이동..
        if (!strcmpi(pToken, "/mm")) {
            // pArg1 // zone no
            short nZoneNO = atoi(pArg1);
            pArg2 = pStrVAR->GetTokenNext(pDelimiters); // x pos
            pArg3 = pStrVAR->GetTokenNext(pDelimiters); // y pos
            if (pArg2 && pArg3) {
                return Cheat_mm(nZoneNO, pArg2, pArg3);
            }
        }
        if (!strcmpi(pToken, "/NPC")) {
            // pArg1 == npc no
            int iNpcIDX = atoi(pArg1);
            if (iNpcIDX) {
                CObjNPC* pNPC = g_pZoneLIST->Get_LocalNPC(iNpcIDX);
                if (pNPC) {
                    pArg2 = pStrVAR->GetTokenNext(pDelimiters); // move, call, var
                    pArg3 = pStrVAR->GetTokenNext(pDelimiters); // event id, var idx
                    char* pArg4 = pStrVAR->GetTokenNext(pDelimiters); // var value
                    return Cheat_npc(pStrVAR, pNPC, iNpcIDX, pArg2, pArg3, pArg4);
                }
            }
            return CHEAT_INVALID;
        }

        if (!strcmpi(pToken, "/ADD")) {
            // 포인트 상승 치트코드...
            pArg2 = pStrVAR->GetTokenNext(pDelimiters);
            pArg3 = pStrVAR->GetTokenNext(pDelimiters);
            return Cheat_add(pArg1, pArg2, pArg3, szCode);
        }
        if (!strcmpi(pToken, "/DEL")) {
            // 포인트 상승 치트코드...
            pArg2 = pStrVAR->GetTokenNext(pDelimiters);
            pArg3 = pStrVAR->GetTokenNext(pDelimiters);
            nProcMODE = Cheat_del(pStrVAR, pArg1, pArg2, pArg3);
        }
        // 아이템 관련 치트코드...
        if (!strcmpi(pToken, "/ITEM")) {
            pArg2 = pStrVAR->GetTokenNext(pDelimiters);
            pArg3 = pStrVAR->GetTokenNext(pDelimiters);
            if (!pArg2 || !pArg3) {
                this->Send_gsv_WHISPER("Server", "Usage: /item <type_id> <item_id> <quantity>");
                this->Send_gsv_WHISPER("Server",
                    "Usage: /item <type_id> <item_id> <stat_id> <socket>");
                return CHEAT_INVALID;
            }
            char* pArg4 = pStrVAR->GetTokenNext(pDelimiters);
            return Cheat_item(pArg1, pArg2, pArg3, pArg4);
        }

        if (!strcmpi(pToken, "/GET")) {
            pArg2 = pStrVAR->GetTokenNext(pDelimiters); // account
            return Cheat_get(pStrVAR, pArg1, pArg2, szCode);
        }
    } else {
        if (!strcmpi(pToken, "/respawn")) {
            // 저장된 부활장소에서 살아나기..
            this->Recv_cli_REVIVE_REQ(REVIVE_TYPE_SAVE_POS);
            return CHEAT_NOLOG;
        }
    }

    if (C_Cheater()) {
        if (pArg1) {
            if (!strcmpi(pToken, "/nc")) {
                // 서버 전체 공지
                g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT(&szCode[4], this->Get_NAME());
                return CHEAT_PROCED;
            } else if (!strcmpi(pToken, "/nz")) {
                // 현재 맵 공지
                g_pZoneLIST->Send_gsv_ANNOUNCE_CHAT(this->GetZONE()->Get_ZoneNO(),
                    &szCode[4],
                    this->Get_NAME());
                return CHEAT_PROCED;
            }

            // 몹 소환
            if (!strcmpi(pToken, "/mon")) {
                // pArg1 Mob IDX
                pArg2 = pStrVAR->GetTokenNext(pDelimiters); // mob cnt
                if (pArg2) {
                    return Cheat_mon(pArg1, pArg2);
                }
            }

            // 몹 삭제
            if (!strcmpi(pToken, "/damage")) {
                // pArg1 Distance
                // pArg2 Damage
                // pArg3 DamageType
                pArg2 = pStrVAR->GetTokenNext(pDelimiters);
                if (pArg2) {
                    pArg3 = pStrVAR->GetTokenNext(pDelimiters);
                    return Cheat_damage(pArg1, pArg2, pArg3);
                }
            }
        } else { // !pArg1
            // 무적 치트코드
            if (!strcmpi(pToken, "/invincible") && (GM_Cheater() || TWGM_Cheater())) {
                m_IngSTATUS.ToggleSubFLAG(FLAG_CHEAT_INVINCIBLE);
                this->Send_gsv_WHISPER("SERVER",
                    this->m_IngSTATUS.IsSubSET(FLAG_CHEAT_INVINCIBLE) ? "Invincible Mode"
                                                                      : "Normal Mode");
                return CHEAT_NOLOG;
            } else if (!strcmpi(pToken, "/dead")) {
                this->Set_HP(10);
                this->SetCMD_ATTACK(this->Get_INDEX());
                return CHEAT_NOLOG;
            }
        }
    }

    if (C_Cheater() || TWGM_Cheater()) {
        if (pArg1) {
            if (!strcmpi(pToken, "/na")) {
                g_pSockLSV->Send_gsv_CHEAT_REQ(this, this->m_dwWSID, 0, szCode);
            } else if (!strcmpi(pToken, "/call")) {
                if (this->GetZONE()) {
                    pArg2 = pStrVAR->GetTokenNext(pDelimiters); // account
                    return Cheat_call(pArg1, pArg2, szCode);
                }

                return CHEAT_NOLOG;
            } else if (!strcmpi(pToken, "/out")) {
                // 강제 접속 종료...
                pArg2 = pStrVAR->GetTokenNext(pDelimiters); // account
                return Cheat_out(pArg1, pArg2, szCode);
            } else if (!strcmpi(pToken, "/shut")) {
                // 말하기 금지...
                pArg2 = pStrVAR->GetTokenNext(pDelimiters); // block time
                pArg3 = pStrVAR->GetTokenNext(pDelimiters); // account
                return Cheat_shut(pArg1, pArg2, pArg3, szCode);
            }
        }
    }

    if (TWGM_Cheater()) {
        if (pArg1) {
            if (!strcmpi(pToken, "/speed")) {
                // 말하기 금지...
                return Cheat_speed(pArg1);
            } else
                // 몹 소환
                if (!strcmpi(pToken, "/mon2")) {
                // pArg1 Mob IDX
                char* pArg2 = pStrVAR->GetTokenNext(pDelimiters); // X
                char* pArg3 = pStrVAR->GetTokenNext(pDelimiters); // Y
                char* pArg4 = pStrVAR->GetTokenNext(pDelimiters); // 마리수 , pArg1 : 몬스터 인덱스
                if (pArg2 && pArg3 && pArg4) {
                    return Cheat_mon2(pArg1, pArg2, pArg3, pArg4);
                }
            }
        }

        if (!strcmpi(pToken, "/HIDE")) {
            this->m_IngSTATUS.ToggleSubFLAG(FLAG_SUB_HIDE);
            DWORD dwFlag = this->m_IngSTATUS.m_dwSubStatusFLAG;
            Send_gsv_CHARSTATE_CHANGE(dwFlag);
            return CHEAT_NOLOG;
        }
    }

    if (pArg1) {
        // 퀘스트 체크...
        if (!strcmpi(pToken, "/QUEST")) {
            if (!strcmpi(pArg1, "all")) {
                g_QuestList.CheckAllQuest(this);
                return CHEAT_NOLOG;
            }
            return Cheat_quest(pStrVAR, pArg1);
        }

        // 능력치 상승 치트코드...
        if (!strcmpi(pToken, "/FULL") && B_Cheater()) {
            if (!strcmpi(pArg1, "HP")) {
                this->Set_HP(this->Get_MaxHP());
                nProcMODE = CHEAT_SEND;
            } else if (!strcmpi(pArg1, "MP")) {
                this->Set_MP(this->Get_MaxMP());
                nProcMODE = CHEAT_SEND;
            }
        } else if (!strcmpi(pToken, "/SET")) {
            pArg2 = pStrVAR->GetTokenNext(pDelimiters);
            if (pArg2) {
                classUSER* pUSER = NULL;
                pArg3 = pStrVAR->GetTokenNext(pDelimiters); // 대상.
                if (pArg3) {
                    pUSER = g_pUserLIST->Find_CHAR(pArg3);
                }
                nProcMODE = Cheat_set(pUSER, pArg1, pArg2, pArg3);
            } else
                return CHEAT_INVALID;
        }
        if (!strcmpi(pToken, "/TOGGLE")) {
            return Cheat_toggle(pStrVAR, pArg1);
        }
        if (!strcmpi(pToken, "/RESET") && A_Cheater()) {
            if (!strcmpi(pArg1, "QUEST")) {
                pStrVAR->Printf("Reset quest result: %d", g_QuestList.LoadQuestTable());
                this->Send_gsv_WHISPER("SERVER", pStrVAR->Get());
            } else if (!strcmpi(pArg1, "REGEN")) {
                this->GetZONE()->Reset_REGEN();
                pStrVAR->Printf("Zone %d Reset regen point", this->GetZONE()->Get_ZoneNO());
                this->Send_gsv_WHISPER("SERVER", pStrVAR->Get());
            }
            return CHEAT_NOLOG;
        } else if (!strcmpi(pToken, "/REGEN") && A_Cheater()) {
            pArg2 = pStrVAR->GetTokenNext(pDelimiters);
            return Cheat_regen(pStrVAR, pArg1, pArg2);
        } else if (!strcmpi(pToken, "/HIDE") && C_Cheater()) {
            this->m_IngSTATUS.ToggleSubFLAG(FLAG_SUB_HIDE);
            return CHEAT_NOLOG;
        }
    } else {
        if (!strcmpi(pToken, "/revive")) {
            // 현재 존의 부활장소에서..
            this->Recv_cli_REVIVE_REQ(REVIVE_TYPE_REVIVE_POS);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pToken, "/alive")) {
            // 현재 존의 부활장소에서..
            this->Set_HP(this->GetCur_MaxHP());
            this->Recv_cli_REVIVE_REQ(REVIVE_TYPE_CURRENT_POS);
            return CHEAT_NOLOG;
        } else if (!strcmpi(pToken, "/SAVE")) {
            g_pThreadSQL->Add_BackUpUSER(this);
            return CHEAT_NOLOG;
        }
    }

    if (CHEAT_SEND == nProcMODE) {
        classPACKET* pCPacket = Packet_AllocNLock();

        pCPacket->m_HEADER.m_wType = GSV_CHEAT_CODE;
        pCPacket->m_HEADER.m_nSize = sizeof(gsv_CHEAT_CODE);
        pCPacket->m_gsv_CHEAT_CODE.m_wObjectIDX = this->Get_INDEX();
        pCPacket->AppendString(szCode);

        this->SendPacket(pCPacket);

        Packet_ReleaseNUnlock(pCPacket);
        return CHEAT_SEND;
    }

    return CHEAT_INVALID;
}
