
#include <crtdbg.h>
#include <tchar.h>
#include <windows.h>

#include "CIocpTHREAD.h"
#include "rose/common/log.h"

//-------------------------------------------------------------------------------------------------
CIocpTHREAD::CIocpTHREAD(bool bCreateSuspended): classTHREAD(bCreateSuspended) {
    ;
}

//-------------------------------------------------------------------------------------------------
void
CIocpTHREAD::Execute() {
    DWORD dwBytesIO;
    DWORD dwCompletionKey;
    LPOVERLAPPED lpOverlapped;

    while (!this->Terminated) {
        if (0
            == ::GetQueuedCompletionStatus(m_hIOCP,
                &dwBytesIO,
                &dwCompletionKey,
                (LPOVERLAPPED*)&lpOverlapped,
                INFINITE)) {
            /*
            If *lpOverlapped is NULL and the function does not dequeue a completion packet from the
            completion port, the RETURN VALUE IS ZERO. The function does not store information in
            the variables pointed to by the lpNumberOfBytesTransferred and lpCompletionKey
            parameters.

            If *lpOverlapped is not NULL and the function dequeues a completion packet
            for a failed I/O operation from the completion port, the RETURN VALUSE IS ZERO.
            return value is zero.
            The function stores information in the variables pointed to by
            lpNumberOfBytesTransferred, lpCompletionKey, and lpOverlapped.
            */
            if (NULL != lpOverlapped) {
                // 소켓 종료
                DWORD dwErrCODE = ::GetLastError();
                STATUS_ReturnFALSE(lpOverlapped, dwCompletionKey);
                continue;
            }

            break;
        }

        if (0 == dwBytesIO) {
            STATUS_ReturnFALSE(lpOverlapped, dwCompletionKey);
            continue;
        }
        if (dwBytesIO == -1) {
            // 0 == dwCompletionKey
            // IOCP is freed ?
            // g_LOG.CS_ODS(0xffff, "* Break:: WorkerThread ID: %d(0x%x),  WorkerNO: %d : dwBytesIO
            // == -1 \n", this->ThreadID, this->ThreadID, m_iThreadNO);
            break;
        }

        STATUS_ReturnTRUE(lpOverlapped, dwCompletionKey, dwBytesIO);
    }
}

//-------------------------------------------------------------------------------------------------
bool
CIocpTHREAD::Init(HANDLE hIOCP, int iThreadNO) {
    m_hIOCP = hIOCP;
    m_iThreadNO = iThreadNO;

    return true;
}
void
CIocpTHREAD::Free(void) {
    this->Terminate();

    do {
        if (0 == ::PostQueuedCompletionStatus(m_hIOCP, -1, 0, NULL)) {
            LOG_DEBUG("ThreadWORKER::Free() PostQueuedCompletionStatus() return 0, LastERROR: "
                      "{0}({0:#x})",
                GetLastError());
            break;
        }
    } while (!this->IsFinished());
}
