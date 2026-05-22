//
// TcpIpReader.cpp - TCP/IP backend.  UMDF 2 port: WDFREQUEST handles instead
// of CComPtr<IWDFIoRequest>.
//

#include "internal.h"
#include "reader.h"
#include "device.h"
#include "memory.h"
#include "sectionLocker.h"

#include <winscard.h>
#include <Sddl.h>
#include <stdio.h>

int TcpIpReader::portBase;

TcpIpReader::TcpIpReader() {
    rpcType      = 1;
    state        = SCARD_ABSENT;
    socket       = INVALID_SOCKET;
    AcceptSocket = INVALID_SOCKET;
    eventsocket  = INVALID_SOCKET;
    breakSocket  = false;
}

void TcpIpReader::init(wchar_t *section) {
    portBase = GetPrivateProfileInt(L"Driver", L"RPC_PORT_BASE", 29500, L"BixVReader.ini");
    port      = GetPrivateProfileInt(section, L"TCP_PORT",       portBase + (instance << 1),       L"BixVReader.ini");
    eventPort = GetPrivateProfileInt(section, L"TCP_EVENT_PORT", portBase + 1 + (instance << 1),   L"BixVReader.ini");

    InitializeCriticalSection(&eventSection);
    InitializeCriticalSection(&dataSection);
}

bool TcpIpReader::CheckATR() {
    if (AcceptSocket == INVALID_SOCKET) return false;
    int read = 0;
    DWORD command = 1;
    if ((read = send(AcceptSocket, (char*)&command, sizeof(DWORD), 0)) <= 0) return false;
    DWORD size = 0;
    if ((read = recv(AcceptSocket, (char*)&size, sizeof(DWORD), MSG_WAITALL)) <= 0) return false;
    if (size == 0) return false;
    BYTE ATR[100];
    if ((read = recv(AcceptSocket, (char*)ATR, size, MSG_WAITALL)) <= 0) return false;
    return true;
}

bool TcpIpReader::QueryTransmit(BYTE *APDU, int APDUlen, BYTE **Resp, int *Resplen) {
    if (AcceptSocket == INVALID_SOCKET) return false;
    DWORD command = 2;
    int read = 0;
    if ((read = send(AcceptSocket, (char*)&command, sizeof(DWORD), 0)) <= 0) {
        ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
    }
    DWORD dwAPDUlen = (DWORD)APDUlen;
    if ((read = send(AcceptSocket, (char*)&dwAPDUlen, sizeof(DWORD), 0)) <= 0) {
        ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
    }
    if ((read = send(AcceptSocket, (char*)APDU, APDUlen, 0)) <= 0) {
        ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
    }
    DWORD dwRespLen = 0;
    if ((read = recv(AcceptSocket, (char*)&dwRespLen, sizeof(DWORD), MSG_WAITALL)) <= 0) {
        ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
    }
    if (dwRespLen != 0) {
        BYTE *p = (BYTE*)realloc(*Resp, dwRespLen);
        if (p == NULL) {
            ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
        }
        *Resp = p;
    }
    if ((read = recv(AcceptSocket, (char*)*Resp, dwRespLen, MSG_WAITALL)) <= 0) {
        ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
    }
    *Resplen = (int)dwRespLen;
    return true;
}

bool TcpIpReader::QueryATR(BYTE *ATR, DWORD *ATRsize, bool reset) {
    if (AcceptSocket == INVALID_SOCKET) return false;
    int read = 0;
    DWORD command = reset ? 0 : 1;
    if ((read = send(AcceptSocket, (char*)&command, sizeof(DWORD), 0)) <= 0) {
        ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
    }
    DWORD size = 0;
    if ((read = recv(AcceptSocket, (char*)&size, sizeof(DWORD), MSG_WAITALL)) <= 0) {
        ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
    }
    if (size == 0) return false;
    size = min(size, *ATRsize);
    if ((read = recv(AcceptSocket, (char*)ATR, size, MSG_WAITALL)) <= 0) {
        ::shutdown(AcceptSocket, SD_BOTH); AcceptSocket = INVALID_SOCKET; return false;
    }
    *ATRsize = size;
    return true;
}

DWORD TcpIpReader::startServer() {
    breakSocket = false;
    wchar_t log[300];

    // bring up the listening sockets the first time
    socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
    sockaddr_in svc{};
    svc.sin_family      = AF_INET;
    svc.sin_addr.s_addr = inet_addr("127.0.0.1");
    svc.sin_port        = htons((u_short)port);
    bind(socket, (SOCKADDR*)&svc, sizeof(svc));
    listen(socket, 1);

    eventsocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
    sockaddr_in esvc{};
    esvc.sin_family      = AF_INET;
    esvc.sin_addr.s_addr = inet_addr("127.0.0.1");
    esvc.sin_port        = htons((u_short)eventPort);
    bind(eventsocket, (SOCKADDR*)&esvc, sizeof(esvc));
    listen(eventsocket, 1);

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds); FD_SET(socket, &readfds);
        timeval tv = { 5, 0 };

        while (true) {
            if (breakSocket) return 0;
            FD_SET(socket, &readfds);
            int ret = select(0, &readfds, NULL, NULL, &tv);
            if (ret > 0) break;
            if (ret < 0) {
                DWORD err = WSAGetLastError();
                swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader]wsa err:%x", err);
                OutputDebugString(log);
                if (err == 0x2736) {
                    socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
                    bind(socket, (SOCKADDR*)&svc, sizeof(svc));
                    listen(socket, 1);
                    FD_ZERO(&readfds); FD_SET(socket, &readfds);
                }
            }
        }

        AcceptSocket = accept(socket, NULL, NULL);
        closesocket(socket);
        socket = INVALID_SOCKET;
        if (AcceptSocket == INVALID_SOCKET) return 0;

        swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader]Socket connected:%zu", AcceptSocket);
        OutputDebugString(log);

        FD_ZERO(&readfds); FD_SET(eventsocket, &readfds);
        while (true) {
            if (breakSocket) return 0;
            FD_SET(eventsocket, &readfds);
            int ret = select(0, &readfds, NULL, NULL, &tv);
            if (ret > 0) break;
            if (ret < 0) {
                DWORD err = WSAGetLastError();
                swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader]wsa err:%x", err);
                OutputDebugString(log);
                if (err == 0x2736) {
                    eventsocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
                    bind(eventsocket, (SOCKADDR*)&esvc, sizeof(esvc));
                    listen(eventsocket, 1);
                    FD_ZERO(&readfds); FD_SET(eventsocket, &readfds);
                }
            }
        }

        SOCKET AcceptEventSocket = accept(eventsocket, NULL, NULL);
        closesocket(eventsocket);
        eventsocket = INVALID_SOCKET;
        if (AcceptEventSocket == INVALID_SOCKET) return 0;

        swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader]Event Socket connected:%zu", AcceptEventSocket);
        OutputDebugString(log);

        if (!waitInsertIpr.empty()) {
            if (initProtocols()) {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                while (!waitInsertIpr.empty()) {
                    WDFREQUEST req = waitInsertIpr.back();
                    waitInsertIpr.pop_back();
                    if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
                        WdfRequestCompleteWithInformation(req, STATUS_SUCCESS, 0);
                    }
                }
                state = SCARD_SWALLOWED;
            }
        }

        while (true) {
            DWORD command = 0;
            int   read    = 0;
            if ((read = recv(AcceptEventSocket, (char*)&command, sizeof(DWORD), MSG_WAITALL)) <= 0) {
                state = SCARD_ABSENT;
                OutputDebugString(L"[BixVReader]Socket error");
                powered = 0;
                ::shutdown(AcceptSocket,      SD_BOTH);
                ::shutdown(AcceptEventSocket, SD_BOTH);

                if (!waitRemoveIpr.empty()) {
                    SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                    while (!waitRemoveIpr.empty()) {
                        WDFREQUEST req = waitRemoveIpr.back();
                        waitRemoveIpr.pop_back();
                        if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
                            WdfRequestCompleteWithInformation(req, STATUS_SUCCESS, 0);
                        }
                    }
                }
                if (!waitInsertIpr.empty()) {
                    SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                    while (!waitInsertIpr.empty()) {
                        WDFREQUEST req = waitInsertIpr.back();
                        waitInsertIpr.pop_back();
                        if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
                            WdfRequestCompleteWithInformation(req, STATUS_CANCELLED, 0);
                        }
                    }
                }
                break;
            }
            OutputDebugString(L"[BixVReader]Socket data");
            if (command == 0) powered = 0;

            if (command == 0 && !waitRemoveIpr.empty()) {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                state = SCARD_ABSENT;
                while (!waitRemoveIpr.empty()) {
                    WDFREQUEST req = waitRemoveIpr.back();
                    waitRemoveIpr.pop_back();
                    if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
                        WdfRequestCompleteWithInformation(req, STATUS_SUCCESS, 0);
                    }
                }
            }
            else if (command == 1 && !waitInsertIpr.empty()) {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                state = SCARD_SWALLOWED;
                initProtocols();
                while (!waitInsertIpr.empty()) {
                    WDFREQUEST req = waitInsertIpr.back();
                    waitInsertIpr.pop_back();
                    if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
                        WdfRequestCompleteWithInformation(req, STATUS_SUCCESS, 0);
                    }
                }
            }
        }
    }
}

void TcpIpReader::shutdown() {
    state       = SCARD_ABSENT;
    breakSocket = true;
    if (eventsocket != INVALID_SOCKET) closesocket(eventsocket);
    if (socket      != INVALID_SOCKET) closesocket(socket);
    if (serverThread) {
        WaitForSingleObject(serverThread, 10000);
        CloseHandle(serverThread);
        serverThread = NULL;
    }
    eventsocket = INVALID_SOCKET;
    socket      = INVALID_SOCKET;

    while (!waitRemoveIpr.empty()) {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WDFREQUEST req = waitRemoveIpr.back();
        waitRemoveIpr.pop_back();
        if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
            WdfRequestComplete(req, STATUS_CANCELLED);
        }
    }
    while (!waitInsertIpr.empty()) {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WDFREQUEST req = waitInsertIpr.back();
        waitInsertIpr.pop_back();
        if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
            WdfRequestComplete(req, STATUS_CANCELLED);
        }
    }
}
