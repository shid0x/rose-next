#include <crtdbg.h>
#include <tchar.h>
#include <windows.h>

#include "ioDataPOOL.h"

IMPLEMENT_INSTANCE2(CPoolSENDIO)

CPoolSENDIO::CPoolSENDIO(UINT uiInitDataCNT, UINT uiIncDataCNT):
    CDataPOOL<IODATANODE>("SendIO", uiInitDataCNT, uiIncDataCNT) {}