#ifndef __STDAFX_H
#define __STDAFX_H

//-------------------------------------------------------------------------------------------------

#define _WIN32_WINNT 0x0500
//#define	__SERVER

#if _MSC_VER > 1000
    #pragma once
#endif // _MSC_VER > 1000

#define __SHO_LS_LIB

#define USE_ORACLE_DB 1 // 사내섭을 제외한...
#define USE_MSSQL // 한국을 제외한 다른 나라...

#include <assert.h>
#include <crtdbg.h>
#include <cstdint>
#include <iostream>
#include <stdio.h>
#include <tchar.h>
#include <windows.h>

#include "rose/common/log.h"

#include "LIB_Util.h"
#include "util/classhash.h"
#include "util/classstr.h"
#include "classTIME.h"

#include "CDLList.h"
#include "CDataPOOL.h"
#include "CVector.h"
#include "DLLIST.h"
#include "Net_Prototype.h"
#include "SRV_Prototype.h"
#include "classSYNCOBJ.h"

#include "CRandom.h"
#include "IOCPSocketSERVER.h"
#include "ioDataPOOL.h"

//-------------------------------------------------------------------------------------------------
#endif