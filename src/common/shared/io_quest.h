#ifndef __IO_QUEST_H
#define __IO_QUEST_H
#include "util/classstr.h"
#include "util/classhash.h"
#include "rose/io/stb.h"
#include "CUserDATA.h"

#pragma warning(disable : 4200)
//-------------------------------------------------------------------------------------------------

#define QST_VARTYPE_VAR 0x0000
#define QST_VARTYPE_SWITCH 0x0100
#define QST_VARTYPE_TIMER 0x0200
#define QST_VARTYPE_EPISODE 0x0300
#define QST_VARTYPE_JOB 0x0400
#define QST_VARTYPE_PLANET 0x0500
#define QST_VARTYPE_UNION 0x0600


struct STR_QUEST_DATA {
    union {
        int iType;
        struct {
            WORD m_wVarNO;
            WORD m_wVarTYPE;
        };
    };

    short nValue;
    BYTE btOp;
};

struct STR_ABIL_DATA {
    int iType;

    int iValue;
    BYTE btOp;
};

struct STR_ITEM_DATA {
    unsigned int uiItemSN;
    int iWhere;
    int iRequestCnt;
    BYTE btOp;
};

//-------------------------------------------------------------------------------------------------

struct StrHeader {
    unsigned int uiSize;
    int iType;
};

struct STR_COND_000 {
    unsigned int uiSize;
    int iType;

    int iQuestSN;
};

struct STR_COND_001 {
    unsigned int uiSize;
    int iType;

    int iDataCnt;
    STR_QUEST_DATA CheckData[1];
};

struct STR_COND_002 {
    unsigned int uiSize;
    int iType;

    int iDataCnt;
    STR_QUEST_DATA CheckData[1];
};

struct STR_COND_003 {
    unsigned int uiSize;
    int iType;

    int iDataCnt;
    STR_ABIL_DATA CheckData[1];
};

struct STR_COND_004 {
    unsigned int uiSize;
    int iType;

    int iDataCnt;
    STR_ITEM_DATA CheckData[1];
};

struct STR_COND_005 {
    unsigned int uiSize;
    int iType;

    BYTE btIsLeader;
    int iLevel;
    BYTE btReversed;
};

struct STR_COND_006 {
    unsigned int uiSize;
    int iType;

    int iZoneSN;
    int iX;
    int iY;
    int iZ;
    int iRadius;
};

struct STR_COND_007 {
    unsigned int uiSize;
    int iType;

    unsigned long ulTime; ///
    unsigned long ulEndTime;
};

struct STR_COND_008 {
    unsigned int uiSize;
    int iType;

    unsigned long ulTime;
    BYTE btOp;
};

struct STR_COND_009 {
    unsigned int uiSize;
    int iType;

    int iSkillSN1;
    int iSkillSN2;
    BYTE btOp;
};

struct STR_COND_010 {
    unsigned int uiSize;
    int iType;

    BYTE btLowPcnt;
    BYTE btHighPcnt;
};

typedef struct tagValueQST {
    unsigned int uiSize;
    int Type;

    BYTE btWho; // 0:NPC, 1:EventOBJ

    short nVarNo;
    int iValue;
    BYTE btOp;
} STR_COND_011, STR_REWD_011;

struct STR_COND_012 {
    unsigned int uiSize;
    int iType;

    short iZone;

    int iX;
    int iY;
    union {
        int iEventID;
        t_HASHKEY m_HashEventOBJ;
    };
};

struct STR_COND_013 {
    unsigned int uiSize;
    int iType;

    int iNpcNo;
};

struct STR_COND_014 {
    unsigned int uiSize;
    int iType;

    short nSN; // 0 ~ 255
    BYTE btOp;
};

struct STR_COND_015 {
    unsigned int uiSize;
    int iType;

    short nNumber1;
    short nNumber2;
};

struct STR_COND_016 {
    unsigned int uiSize;
    int iType;

    BYTE btWho;

    unsigned long ulTime; //
    unsigned long ulEndTime;
};

struct STR_NPCVAR {
    int iNpcNo;
    short nVarNo;
};

struct STR_COND_017 {
    unsigned int uiSize;
    int iType;

    STR_NPCVAR NpcVar1;
    STR_NPCVAR NpcVar2;
    BYTE btOp;
};

struct STR_COND_018 {
    unsigned int uiSize;
    int iType;

    BYTE btDate;

    BYTE btHour1;
    BYTE btMin1;
    BYTE btHour2;
    BYTE btMin2;
};

struct STR_COND_019 {
    unsigned int uiSize;
    int iType;

    BYTE btWeekDay;

    BYTE btHour1;
    BYTE btMin1;
    BYTE btHour2;
    BYTE btMin2;
};

struct STR_COND_020 {
    unsigned int uiSize;
    int iType;
    int iNo1;
    int iNo2;
};

struct STR_COND_021 {
    unsigned int uiSize;
    int iType;
    BYTE btSelObjType;
    int iRadius;
};

struct STR_COND_022 {
    unsigned int uiSize;
    int iType;
    unsigned short nX;
    unsigned short nY;
};

struct STR_COND_023 {
    unsigned int uiSize;
    int iType;
    BYTE btReg;
};

struct STR_COND_024 {
    unsigned int uiSize;
    int iType;

    short nPOS;
    BYTE btOP;
};

struct STR_COND_025 {
    unsigned int uiSize;
    int iType;

    short nCONT;
    BYTE btOP;
};

struct STR_COND_026 {
    unsigned int uiSize;
    int iType;

    short nGRD;
    BYTE btOP;
};

struct STR_COND_027 {
    unsigned int uiSize;
    int iType;

    short nPOINT;
    BYTE btOP;
};

struct STR_COND_028 {
    unsigned int uiSize;
    int iType;

    int iMONEY;
    BYTE btOP;
};

struct STR_COND_029 {
    unsigned int uiSize;
    int iType;

    short nMemberCNT;
    BYTE btOP;
};

struct STR_COND_030 {
    unsigned int uiSize;
    int iType;

    short nSkill1;
    short nSkill2;
    BYTE btOP;
};

struct STR_REWD_000 {
    unsigned int uiSize;
    int iType;

    int iQuestSN;
    BYTE btOp;
};

struct STR_REWD_001 {
    unsigned int uiSize;
    int iType;

    unsigned int uiItemSN;
    BYTE btOp;
    short nDupCNT;
    BYTE btPartyOpt;
};

struct STR_REWD_002 {
    unsigned int uiSize;
    int iType;

    int iDataCnt;
    STR_QUEST_DATA CheckData[1];
};

struct STR_REWD_003 {
    unsigned int uiSize;
    int iType;

    int iDataCnt;
    STR_ABIL_DATA CheckData[1];
    BYTE btPartyOpt;
};

struct STR_REWD_004 {
    unsigned int uiSize;
    int iType;

    int iDataCnt;
    STR_QUEST_DATA CheckData[1];
};

struct STR_REWD_005 {
    unsigned int uiSize;
    int iType;

    BYTE btTarget;
    BYTE btEquation;
    int iValue;
    int iItemSN;
    BYTE btPartyOpt;
    short nItemOpt;
};

struct STR_REWD_006 {
    unsigned int uiSize;
    int iType;

    int iPercentOfHP;
    int iPercentOfMP;
    BYTE btPartyOpt;
};

struct STR_REWD_007 {
    unsigned int uiSize;
    int iType;

    int iZoneSN;
    int iX;
    int iY;
    BYTE btPartyOpt;
};

struct STR_REWD_008 {
    unsigned int uiSize;
    int iType;

    int iMonsterSN;
    int iHowMany;

    BYTE btWho;

    int iZoneSN;
    int iX; /// X
    int iY; /// Y

    int iRange;
    int iTeamNo;
};

struct STR_REWD_009 {
    unsigned int uiSize;
    int iType;

    short shNameLen;
    char szNextTriggerSN[1];
};

struct STR_REWD_010 {
    unsigned int uiSize;
    int iType;
};

struct STR_REWD_012 {
    unsigned int uiSize;
    int iType;

    BYTE btMsgType;
    int iStrID;
};

struct STR_REWD_012_STR {
    unsigned int uiSize;
    int iType;

    BYTE btMsgType;
    char szMsg[1];
};

struct STR_REWD_013 {
    unsigned int uiSize;
    int iType;

    BYTE btWho; // 0:NPC, 1:EventOBJ

    int iSec;
    short nNameLength;
    char szTriggerName[1];

    t_HASHKEY m_HashNextTrigger;
};

struct STR_REWD_014 {
    unsigned int uiSize;
    int iType;

    BYTE btOp;
    int iSkillNo;
};

//#define		TYPE_REWD_015
struct STR_REWD_015 {
    unsigned int uiSize;
    int iType;

    short nSN;
    BYTE btOp;
};

//#define		TYPE_REWD_016
struct STR_REWD_016 {
    unsigned int uiSize;
    int iType;

    short nGroupSN;
};

//#define		TYPE_REWD_017
struct STR_REWD_017 {
    unsigned int uiSize;
    int iType;
};

struct STR_REWD_018 {
    unsigned int uiSize;
    int iType;

    int iStrID;
    short nCnt;
    BYTE Data[1];
};

struct STR_REWD_019 {
    unsigned int uiSize;
    int iType;

    short nZoneNo;

    short nTeamNo;
    short nTriggerLength;
    char TriggerName[1];

    t_HASHKEY m_HashTrigger;
};

struct STR_REWD_020 {
    unsigned int uiSize;
    int iType;
    unsigned char btNoType;
};

struct STR_REWD_021 {
    unsigned int uiSize;
    int iType;
#ifdef __SERVER
    int iX;
    int iY;
#else
    short nX;
    short nY;
#endif
};

struct STR_REWD_022 {
    unsigned int uiSize;
    int iType;

    short nZonNo;
    BYTE btOp;
};

struct STR_REWD_023 {
    unsigned int uiSize;
    int iType;
};

struct STR_REWD_024 {
    unsigned int uiSize;
    int iType;

    int iMONEY;
    BYTE btOP;
};

struct STR_REWD_025 {
    unsigned int uiSize;
    int iType;

    short nPOINT;
    BYTE btOP;
};

struct STR_REWD_026 {
    unsigned int uiSize;
    int iType;

    short nSkillNo;
    BYTE btOP;
};

struct STR_REWD_027 {
    unsigned int uiSize;
    int iType;

    short nCONT;
    BYTE btOP;
};

struct STR_REWD_028 {
    unsigned int uiSize;
    int iType;

    int iRange;
    short nZoneNo;
    int iX;
    int iY;
};

struct STR_REWD_029 {
    unsigned int uiSize;
    int iType;

    short nLength;
    char szSrciptName[1];
};

struct STR_REWD_030 {
    unsigned int uiSize;
    int iType;
};

struct STR_REWD_031 {
    unsigned int uiSize;
    int iType;

    int iMonsterSN;
    int iCompareValue;
    STR_QUEST_DATA Var;
};

struct STR_REWD_032 {
    unsigned int uiSize;
    int iType;

    unsigned int uiItemSN;
    int iCompareValue;
    BYTE btPartyOpt;
};

struct STR_REWD_033 {
    unsigned int uiSize;
    int iType;

    short nNextRewardSplitter;
};

struct STR_REWD_034 {
    unsigned int uiSize;
    int iType;

    BYTE btHIDE;
};

//-------------------------------------------------------------------------------------------------
union uniQstENTITY {
    struct {
        unsigned int uiSize;
        int iType;
    };
    BYTE m_pDATA[0];

    STR_COND_000 m_Cond000;
    STR_COND_001 m_Cond001;
    STR_COND_002 m_Cond002;
    STR_COND_003 m_Cond003;
    STR_COND_004 m_Cond004;
    STR_COND_005 m_Cond005;
    STR_COND_006 m_Cond006;
    STR_COND_007 m_Cond007;
    STR_COND_008 m_Cond008;
    STR_COND_009 m_Cond009;
    STR_COND_010 m_Cond010;
    STR_COND_011 m_Cond011;
    STR_COND_012 m_Cond012;
    STR_COND_013 m_Cond013;
    STR_COND_014 m_Cond014;
    STR_COND_015 m_Cond015;
    STR_COND_016 m_Cond016;
    STR_COND_017 m_Cond017;
    STR_COND_018 m_Cond018;
    STR_COND_019 m_Cond019;
    STR_COND_020 m_Cond020;
    STR_COND_021 m_Cond021;
    STR_COND_022 m_Cond022;
    STR_COND_023 m_Cond023;
    STR_COND_024 m_Cond024;
    STR_COND_025 m_Cond025;
    STR_COND_026 m_Cond026;
    STR_COND_027 m_Cond027;
    STR_COND_028 m_Cond028;
    STR_COND_029 m_Cond029;
    STR_COND_030 m_Cond030;

    STR_REWD_000 m_Rewd000;
    STR_REWD_001 m_Rewd001;
    STR_REWD_002 m_Rewd002;
    STR_REWD_003 m_Rewd003;
    STR_REWD_004 m_Rewd004;
    STR_REWD_005 m_Rewd005;
    STR_REWD_006 m_Rewd006;
    STR_REWD_007 m_Rewd007;
    STR_REWD_008 m_Rewd008;
    STR_REWD_009 m_Rewd009;
    STR_REWD_010 m_Rewd010;
    STR_REWD_011 m_Rewd011;
    STR_REWD_012 m_Rewd012;
    STR_REWD_012_STR m_Rewd012_STR;
    STR_REWD_013 m_Rewd013;
    STR_REWD_014 m_Rewd014;

    STR_REWD_015 m_Rewd015;
    STR_REWD_016 m_Rewd016;
    STR_REWD_017 m_Rewd017;

    STR_REWD_018 m_Rewd018;
    STR_REWD_019 m_Rewd019;
    STR_REWD_020 m_Rewd020;
    STR_REWD_021 m_Rewd021;
    STR_REWD_022 m_Rewd022;

    STR_REWD_023 m_Rewd023;
    STR_REWD_024 m_Rewd024;
    STR_REWD_025 m_Rewd025;
    STR_REWD_026 m_Rewd026;
    STR_REWD_027 m_Rewd027;
    STR_REWD_028 m_Rewd028;
    STR_REWD_029 m_Rewd029;
    STR_REWD_031 m_Rewd031;
    STR_REWD_032 m_Rewd032;
    STR_REWD_033 m_Rewd033;
    STR_REWD_034 m_Rewd034;
};

//-------------------------------------------------------------------------------------------------
/*
enum eQE_CODE {		// Quest error code
    QSTERR_QUEST_OK = 0x0000,
    QSTERR_QUEST_NOT_REGISTERED,
} ;
*/
class CUserDATA;
#ifndef __SERVER
class CObjUSER;
#endif
class CQUEST;
class CQuestTRIGGER;
class CObjNPC;
class CObjEVENT;

#ifdef __SERVER
class classUSER;
#endif

struct tQST_PARAM {
#ifndef __SERVER
    CQuestTRIGGER* m_pCurrentTRIGGER;
    CUserDATA* m_pOWNER;
#else
    CObjVAR* m_pEventVAR;
    CObjVAR* m_pNpcVAR;
    classUSER* m_pOWNER;
    CGameOBJ* m_pCallOBJ;
#endif

    short m_nCurZoneNO;

    BYTE m_btQuestSLOT;
    CQUEST* m_pQUEST;
    t_HASHKEY m_HashNextTRIGGER;

    bool m_bServerFUNC;
    short m_nErrSTEP;

    void Init(CUserDATA* pOWNER, short nZoneNO = 0) {
        m_btQuestSLOT = QUEST_PER_PLAYER;
        m_pQUEST = NULL;
        m_HashNextTRIGGER = 0;
        m_nErrSTEP = -1;
        m_bServerFUNC = false;
        m_nCurZoneNO = nZoneNO;
#ifdef __SERVER
        m_pNpcVAR = NULL;
        m_pEventVAR = NULL;
        m_pOWNER = (classUSER*)pOWNER;
#else
        m_pOWNER = pOWNER;
#endif
    }
};

//-------------------------------------------------------------------------------------------------
class CQuestTRIGGER {
private:
    BYTE m_btCheckNEXT;
    unsigned int m_uiCondCNT;
    unsigned int m_uiRewdCNT;

    uniQstENTITY** m_ppCondition;
    uniQstENTITY** m_ppReward;

    void Init_COND(uniQstENTITY* pCOND);
    void Init_REWD(uniQstENTITY* pREWD);

public:
    CStrVAR m_Name;
    CQuestTRIGGER* m_pNextTrigger;

    CQuestTRIGGER() {
		m_pNextTrigger = NULL;
#ifdef __SERVER
        m_iOwerNpcIDX = 0;
#endif
    }
    ~CQuestTRIGGER() { this->Free(); }

    BYTE GetCheckNext() { return m_btCheckNEXT; }

#ifdef __SERVER
    int m_iOwerNpcIDX;
    bool Load(FILE* fpIN, STBDATA* pSTB, int iLangCol);
#else
    bool Client_Load(CFileSystem* pFileSystem);
#endif
    void Free();

#ifdef __SERVER
    bool Proc(tQST_PARAM* pPARAM, bool bDoReward, short nSelectReward);
#else
    bool Proc(tQST_PARAM* pPARAM, bool bDoReward);
#endif
};

//-------------------------------------------------------------------------------------------------
// QUEST
#ifdef __SERVER
    #define QUEST_NAME(I) g_QuestList.m_STB.get_cstr(I, 0)
    #define QUEST_DESC(I) g_QuestList.m_STB.get_cstr(I, 4)
#else
    #include "../GameCommon/StringManager.h"
    #define QUEST_NAME(I) CStringManager::GetSingleton().GetQuestName(I)
    #define QUEST_DESC(I) CStringManager::GetSingleton().GetQuestDesc(I)
    #define QUEST_START_MSG(I) CStringManager::GetSingleton().GetQuestStartMsg(I)
    #define QUEST_END_MSG(I) CStringManager::GetSingleton().GetQuestEndMsg(I)
#endif

#define QUEST_TIME_LIMIT(I) g_QuestList.m_STB.get_int32(I, 1)
#define QUEST_OWNER_TYPE(I) g_QuestList.m_STB.get_int32(I, 2)
#define QUEST_ICON(I) g_QuestList.m_STB.get_int32(I, 3)

enum eQST_RESULT {
    QST_RESULT_INVALID = 0,
    QST_RESULT_SUCCESS,
    QST_RESULT_FAILED,
    QST_RESULT_STOPPED
};

class CQuestDATA {
private:
    classHASH<CQuestTRIGGER*> m_HashQUEST;
    CStrVAR m_QuestListSTB;
    CStrVAR m_QuestFILE;
    CStrVAR m_BaseDIR;

#ifdef __SERVER
    CStrVAR m_QuestLangSTB;
    int m_iLangCol;
    STBDATA* m_pSTB;
#endif

    bool m_bEnable;
    long m_lRefCnt;

#ifdef __SERVER
    void LoadQuestTrigger(FILE* fpIN, unsigned int uiTriggerCNT, char* szFileName);
    bool LoadDATA(char* szFileName);
#else
    void LoadQuestTrigger(CFileSystem* pFileSystem, unsigned int uiTriggerCNT, char* szFileName);
    bool Client_LoadDATA(char* szFileName);
#endif

public:
    STBDATA m_STB;

    bool LoadQuestTable();
#ifdef __SERVER
    bool LoadQuestTable(char* szQuestFile,
        char* szQuestListFile,
        char* szBaseDIR = NULL,
        char* szQustLangFile = NULL,
        int iLangCol = 0);
#else
	bool LoadQuestTable(char* szQuestFile, char* szQuestListFile, char* szBaseDIR = NULL);
#endif

    void Free();
    CQuestDATA() { m_bEnable = false, m_lRefCnt = 0; }
    ~CQuestDATA() { this->Free(); }

#ifdef __SERVER
    eQST_RESULT CheckQUEST(CUserDATA* pUSER,
        t_HASHKEY HashQuest,
        bool bDoReward = false,
        int iEventNpcIDX = 0,
        CGameOBJ* pCallOBJ = NULL,
        short nSelectReward = -1);
#else
	eQST_RESULT CheckQUEST(CUserDATA* pUSER, t_HASHKEY HashQuest, bool bDoReward = false);
#endif

    CQuestTRIGGER* GetQuest(t_HASHKEY HashQuest) {
        tagHASH<CQuestTRIGGER*>* pHashNode;

        pHashNode = m_HashQUEST.Search(HashQuest);
        return (pHashNode) ? pHashNode->m_DATA : NULL;
    }

    void CheckAllQuest(CUserDATA* pUSER);

    bool IsEnable() { return m_bEnable; }
#ifdef __SERVER
    void ToggleEnable() { m_bEnable = !m_bEnable; }
    long GetRefCNT() { return m_lRefCnt; }
#endif
};
extern CQuestDATA g_QuestList;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#pragma warning(default : 4200)
#endif