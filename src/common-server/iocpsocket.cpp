#include <crtdbg.h>
#include <tchar.h>
#include <winsock2.H>

#include <windows.h>

#include "LIB_Util.h"
#include "classIOCP.h"
#include "classPACKET.h"
#include "classUTIL.h"
#include "iocpSOCKET.h"

#include "rose/common/log.h"

//-------------------------------------------------------------------------------------------------
//
//  IOCP Client socket class
//
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
iocpSOCKET::iocpSOCKET(): m_csRecvQ(4000) {
    this->Init_SCOKET();
}
iocpSOCKET::~iocpSOCKET() {
    _ASSERT(0 == this->m_iSocketIDX);
    // classIOCP::DelClientSocket( this->m_iSocketIDX );		// @~iocpSOCKET()
    this->Clear_LIST();
}

void
iocpSOCKET::CloseSocket(void) {
    if (m_Socket != INVALID_SOCKET) {
        struct linger li = {0, 0}; // Default: SO_DONTLINGER

        ::shutdown(m_Socket, SD_BOTH);
        ::setsockopt(m_Socket, SOL_SOCKET, SO_LINGER, (char*)&li, sizeof(li));
        ::closesocket(m_Socket);

        m_Socket = INVALID_SOCKET;
        m_bWritable = false;
    }
}

//-------------------------------------------------------------------------------------------------
void
iocpSOCKET::Clear_LIST(void) {
    classDLLNODE<tagIO_DATA>* pNode;

    m_csRecvQ.Lock();
    {
        for (size_t i = 0; i < recv_list.size(); ++i) {
            this->Free_RecvIODATA(recv_list.front());
            recv_list.pop();
        }
    }
    m_csRecvQ.Unlock();

    m_csSendQ.Lock();
    {
        pNode = m_SendList.GetHeadNode();
        while (pNode) {
            m_SendList.DeleteNode(pNode);
            this->Free_SendIODATA(&pNode->DATA);

            pNode = m_SendList.GetHeadNode();
        }
    }
    m_csSendQ.Unlock();
}

//-------------------------------------------------------------------------------------------------
// pRecvNode에 이어 받기.
ePacketRECV
iocpSOCKET::Recv_Continue(tagIO_DATA* recv_data) {
    if (0
        == ::ReadFile((HANDLE)m_Socket,
            &recv_data->packet.bytes[recv_data->bytes],
            MAX_PACKET_SIZE - recv_data->bytes,
            NULL,
            (LPOVERLAPPED)recv_data)) {

        if (ERROR_IO_PENDING != ::GetLastError()) {
            return eRESULT_PACKET_DISCONNECT;
        }
    }

    return eRESULT_PACKET_OK; // true;
}

//-------------------------------------------------------------------------------------------------
// 새로 받기.
ePacketRECV
iocpSOCKET::Recv_Start(void) {
    tagIO_DATA* recv_data = this->Alloc_RecvIODATA();

    if (NULL == recv_data) {
        return eRESULT_PACKET_DISCONNECT; // false;
    }

    _ASSERT(recv_data->mode == IOMode::Read);
    _ASSERT(recv_data->bytes == 0);

    return this->Recv_Continue(recv_data);
}

//-------------------------------------------------------------------------------------------------
// CThreadWORKER::STATUS_ReturnTRUE () 에서만 호출된다.
ePacketRECV
iocpSOCKET::Recv_Complete(tagIO_DATA* recv_data) {
    ePacketRECV eResult = eRESULT_PACKET_OK;

    this->LockSOCKET();

    // Read less data than packet header
    if (recv_data->bytes < sizeof(t_PACKETHEADER)) {
        eResult = this->Recv_Continue(recv_data);
        this->UnlockSOCKET();
        return eResult;
    }

    // Read at least packet header and possibly some packet data bytes
    uint32_t packet_size = static_cast<uint32_t>(recv_data->packet.size);
    if (packet_size == 0) {
        this->UnlockSOCKET();
        this->Free_RecvIODATA(recv_data);

        LOG_WARN("Received a packet with no data from {}", this->m_IP.Get());
        return eRESULT_PACKET_BLOCK;
    }

    if (recv_data->bytes < packet_size) {
        eResult = this->Recv_Continue(recv_data);
        this->UnlockSOCKET();
        return eResult;
    }

    if (recv_data->bytes == packet_size) {
        this->UnlockSOCKET();
        if (!this->Recv_Done(recv_data)) {
            return eRESULT_PACKET_DISCONNECT;
        }
        return this->Recv_Start();
    }

    // Read more data than one packet
    t_PACKETHEADER* pHEADER;
    short nRemainBytes, nPacketSIZE;

    nRemainBytes = (short)recv_data->bytes - recv_data->packet.size;
    pHEADER = (t_PACKETHEADER*)&recv_data->packet.bytes[recv_data->packet.size];
    nPacketSIZE = 0;
    while (nRemainBytes >= sizeof(t_PACKETHEADER)) {
        if (0 == nPacketSIZE) {
            nPacketSIZE = pHEADER->m_nSize;
            if (0 == nPacketSIZE) {
                this->UnlockSOCKET();
                this->Free_RecvIODATA(recv_data);
                g_LOG.CS_ODS(0xffff,
                    "*** ERROR: Decode recv packet header2, IP[ %s ]\n",
                    this->m_IP.Get());

                // 블랙 리스트에 ip 등록...
                return eRESULT_PACKET_BLOCK; // false;
            }
        }

        if (nRemainBytes == nPacketSIZE) {
            this->UnlockSOCKET();
            if (!this->Recv_Done(recv_data)) {
                return eRESULT_PACKET_DISCONNECT;
            }
            return this->Recv_Start();
        }

        if (nRemainBytes > nPacketSIZE) {
            // 추가 패킷이 있다.
            nRemainBytes -= nPacketSIZE;
            pHEADER = (t_PACKETHEADER*)(pHEADER->m_pDATA + nPacketSIZE);
            nPacketSIZE = 0;
        } else
            break;
    }

    // 모자란 부분을 다시 읽어들일 데이타 생성.
    tagIO_DATA* new_recv = this->Alloc_RecvIODATA();
    if (new_recv) {
        new_recv->bytes = nRemainBytes;
        ::CopyMemory(new_recv->packet.bytes, pHEADER, nRemainBytes);

        recv_data->bytes -= nRemainBytes;

        this->UnlockSOCKET();
        if (!this->Recv_Done(recv_data)) {
            this->Free_RecvIODATA(new_recv);
            return eRESULT_PACKET_DISCONNECT; // false;
        }
        return this->Recv_Continue(new_recv); // 이어 받기.
    }

    eResult = eRESULT_PACKET_DISCONNECT; // false;
    this->UnlockSOCKET();
    return eResult;
}

//-------------------------------------------------------------------------------------------------
// pSendNode에 이어 보내기.
bool
iocpSOCKET::Send_Continue(tagIO_DATA* pSendDATA) {
    if (pSendDATA->bytes >= pSendDATA->packet.size) {
        LOG_WARN(">>ERROR:: Sending packet: Len: {}, completed: {}, IP:{}",
            pSendDATA->packet.size,
            pSendDATA->bytes,
            this->Get_IP());
        return false;
    }

    this->m_bWritable = false;
    if (0
        == ::WriteFile((HANDLE)m_Socket,
            &pSendDATA->packet.bytes[pSendDATA->bytes],
            pSendDATA->packet.size - pSendDATA->bytes,
            NULL,
            (LPOVERLAPPED)pSendDATA)) {
        // Function failed ..
        if (ERROR_IO_PENDING != GetLastError()) {
            return false;
        }
    }
    this->m_dwCheckTIME = ::timeGetTime();

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
iocpSOCKET::Send_Start(const classPACKET& packet) {
    LOG_TRACE("Sending to client #{} packet {:#x}", this->m_iSocketIDX, packet.type);

    if (m_Socket == INVALID_SOCKET)
        return false;

    classDLLNODE<tagIO_DATA>* pSendNODE;
    pSendNODE = this->Alloc_SendIODATA(packet);
    if (NULL == pSendNODE)
        return false;

    m_csSendQ.Lock();
    {
        m_SendList.AppendNode(pSendNODE);

        if (this->m_bWritable) {
            pSendNODE = m_SendList.GetHeadNode();
            if (!this->Send_Continue(&pSendNODE->DATA)) {
                m_csSendQ.Unlock();
                this->CloseSocket();
                return false;
            }
        } else {
            int iQedCnt = m_SendList.GetNodeCount();
            if (iQedCnt > 100) {
                // 보내기 시도한 후 아직까지 다음 패킷을 보내지 못하고 있는넘...
                // 패킷을 쌓놓고 있다면 짤라버려야지...
                DWORD dwPassTime = ::timeGetTime() - this->m_dwCheckTIME;
                if (dwPassTime >= 60 * 1000 || iQedCnt > 2500) {
                    g_LOG.CS_ODS(0xffff,
                        ">>Sending timeout: packet: %d, time: %d, IP:%s\n",
                        iQedCnt,
                        dwPassTime,
                        this->Get_IP());
                    this->CloseSocket();
                }
            }
        }
    }
    m_csSendQ.Unlock();
    return true;
}

//-------------------------------------------------------------------------------------------------
// CThreadWORKER::STATUS_ReturnTRUE() 에서만 호출된다.
bool
iocpSOCKET::Send_Complete(tagIO_DATA* pSendDATA) {
    m_csSendQ.Lock();
    {
        if (0 == this->m_iSocketIDX) { // 종료됐다.
            m_csSendQ.Unlock();
            return false;
        }

        this->m_bWritable = true;
        if (pSendDATA->bytes == (WORD)pSendDATA->packet.size) {
            classDLLNODE<tagIO_DATA>* pHeadNODE = m_SendList.GetHeadNode();
            _ASSERT(pHeadNODE == pSendDATA->node);

            classDLLNODE<tagIO_DATA>* pNextNODE = m_SendList.GetNextNode(pHeadNODE);
            if (pNextNODE) {
                _ASSERT(pNextNODE->DATA.bytes == 0);
            }

            m_SendList.DeleteNode(pSendDATA->node);
            this->Free_SendIODATA(pSendDATA);

            classDLLNODE<tagIO_DATA>* pSendNODE = m_SendList.GetHeadNode();
            if (pSendNODE) {

                _ASSERT(pSendNODE->DATA.bytes == 0);

                if (!this->Send_Continue(&pSendNODE->DATA)) {
                    m_csSendQ.Unlock();
                    return false;
                }
            }
        } else if (pSendDATA->bytes < pSendDATA->packet.size) { // 부분 전송됨..
            if (!this->Send_Continue(pSendDATA)) {
                m_csSendQ.Unlock();
                return false;
            }
        } else {
            _ASSERT(0);
        }
    }
    m_csSendQ.Unlock();

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
iocpSOCKET::Recv_Done(tagIO_DATA* pRecvDATA) {
    t_PACKETHEADER* pPacket = reinterpret_cast<t_PACKETHEADER*>(&pRecvDATA->packet.bytes);
    LOG_TRACE("Received from client #{} packet {:#x}", this->m_iSocketIDX, pPacket->m_wType);

    do {
        short nTotalPacketLEN = pPacket->m_nSize;
        if (pPacket->m_nSize > pRecvDATA->bytes) {
            LOG_WARN("Invalid packet: size > bytes read. [{}].", this->m_IP.Get());
            this->Free_RecvIODATA(pRecvDATA);
            return false;
        }

        if (pPacket->m_nSize == 0) {
            LOG_WARN("Invalid packet: size == 0. [{}].", this->m_IP.Get());
            this->Free_RecvIODATA(pRecvDATA);
            return false;
        }

        if (!this->HandlePACKET(pPacket)) {
            this->Free_RecvIODATA(pRecvDATA);
            return false;
        }

        pRecvDATA->bytes -= nTotalPacketLEN;
        pPacket = (t_PACKETHEADER*)(pPacket->m_pDATA + nTotalPacketLEN);
    } while (pRecvDATA->bytes);

    this->Free_RecvIODATA(pRecvDATA);

    return true;
}

//-------------------------------------------------------------------------------------------------
