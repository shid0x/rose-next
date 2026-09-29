#ifndef _OBJECT_COMMAND_
#define _OBJECT_COMMAND_

#include "DataType.h"
#include <list>

class CObjCHAR;
class CObjCommand;

enum OBJECT_COMMAND_TYPE {
    OBJECT_COMMAND_STOP = 0,
    OBJECT_COMMAND_ATTACK = 1,
    OBJECT_COMMAND_SIT,
    OBJECT_COMMAND_STAND,
    OBJECT_COMMAND_MOVE,
    OBJECT_COMMAND_DIE,
    OBJECT_COMMAND_TOGGLE,
    OBJECT_COMMAND_Skill2SELF,
    OBJECT_COMMAND_Skill2OBJ,
    OBJECT_COMMAND_Skill2POS,
    OBJECT_COMMAND_MAX,
};

//----------------------------------------------------------------------------------------------------
/// Class CObjCommandManager
/// @brief Manager class for object command
//----------------------------------------------------------------------------------------------------

class CObjCommandManager {
private:
    std::list<CObjCommand*> m_ObjCommandList;
    /// Client index of the owning object while it is worth tracing (a monster in
    /// the local fight, set each frame by CObjCHAR::Proc under Debug); 0 = silent.
    /// Only used to name the owner when a queued command is dropped.
    int m_iTraceOwner;

public:
    void SetTraceOwner(int iOwner) { m_iTraceOwner = iOwner; }
    CObjCommandManager();
    ~CObjCommandManager();

    CObjCommand* GetObjCommand(int iCommandType);

    bool IsEmpty();

    int GetCommandCount();
    void PushCommand(CObjCommand* pObjCommand);

    CObjCommand*
    GetLastInvalidSkillCommand(); /// 가장최신에 받은 스킬 명령을 얻어온다.( result 를 받지 못한 )
    void SetValidFlag();
    void SetValidResultOfSkill();
    CObjCommand* PopCommand(bool& bSkillCommand);
    CObjCommand* PopLastCommand(bool& bSkillCommand);
    /// Is a skill command for this skill index waiting in the queue? Used by the
    /// legacy skill-damage receive path: a payload for a cast that is still queued
    /// on a remote caster must wait for that cast's action frame, not present now.
    bool HasSkillCommand(int iSkillIDX);
    /// Is any skill command waiting in the queue?
    bool HasAnySkillCommand();
    static bool IsSkillCommandType(int iType);
    /// Readable command type, for the CombatTrace lines.
    static const char* TypeName(int iType);

    void ClearCommand();
};

//----------------------------------------------------------------------------------------------------
/// Class CObjCommand
/// @brief abstract class for object command
//----------------------------------------------------------------------------------------------------

class CObjCommand {
protected:
    CObjCHAR* m_pOwner;
    int m_iType;

    /// skill 의 경우.. start_of_skill 을 받았는가?
    friend CObjCommandManager;
    bool m_bValid;
    bool m_bGetResultOfSkill; /// skill 의 경우 result of skill 을 받았는가?

public:
    CObjCommand(void);
    virtual ~CObjCommand(void);

    int GetType() { return m_iType; }
    bool bGetResultOfSkil() { return m_bGetResultOfSkill; }
    /// A remote caster's queued skill command is server-confirmed by construction,
    /// so it is valid at push: PopCommand deletes an invalid skill command, and
    /// GSV_SKILL_START (the avatar-flow validator) can arrive after the queue has
    /// already been popped behind an owed swing. Deliberately NOT a "result
    /// received" setter: PushCommand erasing older result-less skill commands is
    /// what keeps a remote caster's queue to the newest server command.
    void SetValid(bool bValid) { m_bValid = bValid; }

    virtual bool Execute(CObjCHAR* pObjCHAR) = 0 { *(int*)0 = 10; };
};

//----------------------------------------------------------------------------------------------------
/// Class CObjStopCommand
/// @brief command class for object stop command
//----------------------------------------------------------------------------------------------------

class CObjStopCommand: public CObjCommand {
public:
    CObjStopCommand();
    ~CObjStopCommand();

    void SetCMD_STOP();

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjAttackCommand
/// @brief command class for object attack command
///			SetCMD_Attack 가 두개네? 하나는 CObjAI, CObjCHAR   이상하면 확인해봐라..
//----------------------------------------------------------------------------------------------------

class CObjAttackCommand: public CObjCommand {
private:
    int m_iServerIndex;
    WORD m_wSrcDIST;
    D3DVECTOR m_PosGOTO;

public:
    CObjAttackCommand();
    ~CObjAttackCommand();

    void SetCMD_ATTACK(int iServerIndex, WORD wSrvDIST, const D3DVECTOR& PosGOTO);

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjSitCommand
/// @brief command class for object sit command
//----------------------------------------------------------------------------------------------------

class CObjSitCommand: public CObjCommand {
public:
    CObjSitCommand();
    ~CObjSitCommand();

    void SetCMD_SIT();

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjStandCommand
/// @brief command class for object stand command
//----------------------------------------------------------------------------------------------------

class CObjStandCommand: public CObjCommand {
public:
    CObjStandCommand();
    ~CObjStandCommand();

    void SetCMD_STAND();

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjMoveCommand
/// @brief command class for object move command
//----------------------------------------------------------------------------------------------------

class CObjMoveCommand: public CObjCommand {
private:
    bool m_bMoveToObjectMode;

    BYTE m_btRunMODE;

    WORD m_wSrvDIST;
    D3DVECTOR m_PosTO;

    int m_iServerTarget;

public:
    CObjMoveCommand();
    ~CObjMoveCommand();

    void SetCMD_MOVE(const D3DVECTOR& PosTO, BYTE btRunMODE);
    void SetCMD_MOVE(WORD wSrvDIST, const D3DVECTOR& PosTO, int iServerTarget);

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjDieCommand
/// @brief command class for object die command
//----------------------------------------------------------------------------------------------------

class CObjDieCommand: public CObjCommand {
public:
    CObjDieCommand();
    ~CObjDieCommand();

    void SetCMD_DIE();

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjToggleCommand
/// @brief command class for object toggle command
//----------------------------------------------------------------------------------------------------

class CObjToggleCommand: public CObjCommand {
private:
    BYTE m_btTYPE;

public:
    CObjToggleCommand();
    ~CObjToggleCommand();

    void SetCMD_TOGGLE(BYTE btTYPE);

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjSkill2SelfCommand
/// @brief command class for object Skill2Self command
//----------------------------------------------------------------------------------------------------

class CObjSkill2SelfCommand: public CObjCommand {
private:
    int m_iSkillIDX;

public:
    CObjSkill2SelfCommand();
    ~CObjSkill2SelfCommand();

    int GetSkillIndex() { return m_iSkillIDX; }
    void SetCMD_Skill2SELF(short nSkillIDX);

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjSkill2ObjCommand
/// @brief command class for object Skill2Obj command
//----------------------------------------------------------------------------------------------------

class CObjSkill2ObjCommand: public CObjCommand {
private:
    WORD m_wSrvDIST;
    D3DVECTOR m_PosTO;

    int m_iSkillIDX;
    int m_iServerTarget;

public:
    CObjSkill2ObjCommand();
    ~CObjSkill2ObjCommand();

    int GetSkillIndex() { return m_iSkillIDX; }
    int GetServerTarget() { return m_iServerTarget; }

    void
    SetCMD_Skill2OBJ(WORD wSrvDIST, const D3DVECTOR& PosTO, int iServerTarget, short nSkillIDX);
    void SetCMD_Skill2OBJ(int iServerTarget, short nSkillIDX);

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

//----------------------------------------------------------------------------------------------------
/// Class CObjSkill2PosCommand
/// @brief command class for object Skill2Pos command
//----------------------------------------------------------------------------------------------------

class CObjSkill2PosCommand: public CObjCommand {
private:
    D3DVECTOR m_PosGOTO;
    int m_iSkillIDX;

public:
    CObjSkill2PosCommand();
    ~CObjSkill2PosCommand();

    int GetSkillIndex() { return m_iSkillIDX; }
    D3DVECTOR& GetTargetPos() { return m_PosGOTO; }

    void SetCMD_Skill2POS(const D3DVECTOR& PosGOTO, short nSkillIDX);

    /*override*/ bool Execute(CObjCHAR* pObjCHAR);
};

#endif //_OBJECT_COMMAND_