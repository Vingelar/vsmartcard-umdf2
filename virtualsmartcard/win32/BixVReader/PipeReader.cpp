//
// PipeReader.cpp - named-pipe backend.  UMDF 2 port: WDFREQUEST handles
// instead of CComPtr<IWDFIoRequest>.
//

#include "internal.h"
#include "reader.h"
#include "device.h"
#include "memory.h"
#include "sectionLocker.h"

#include <winscard.h>
#include <Sddl.h>
#include <stdio.h>

BOOL CreateMyDACL(SECURITY_ATTRIBUTES * pSA)
{
    TCHAR * szSD = TEXT("D:")           // Discretionary ACL
        TEXT("(D;OICI;GA;;;BG)")        // Deny built-in guests
        TEXT("(D;OICI;GA;;;AN)")        // Deny anonymous logon
        TEXT("(A;OICI;GRGWGX;;;AU)")    // Allow r/w/x to authenticated users
        TEXT("(A;OICI;GA;;;BA)");       // Allow full control to admins

    if (NULL == pSA) return FALSE;

    return ConvertStringSecurityDescriptorToSecurityDescriptor(
        szSD, SDDL_REVISION_1, &(pSA->lpSecurityDescriptor), NULL);
}

PipeReader::PipeReader() {
    rpcType   = 0;
    state     = SCARD_ABSENT;
    pipe      = NULL;
    eventpipe = NULL;
    InitializeCriticalSection(&eventSection);
    InitializeCriticalSection(&dataSection);
}

void PipeReader::init(wchar_t *section) {
    wchar_t temp[300];
    swprintf_s(temp, ARRAY_SIZE(temp), L"SCardSimulatorDriver%d", instance);
    GetPrivateProfileStringW(section, L"PIPE_NAME", temp, pipeName, 300, L"BixVReader.ini");
    swprintf_s(temp, ARRAY_SIZE(temp), L"SCardSimulatorDriverEvents%d", instance);
    GetPrivateProfileStringW(section, L"PIPE_EVENT_NAME", temp, pipeEventName, 300, L"BixVReader.ini");
}

bool PipeReader::CheckATR() {
    if (pipe == NULL) return false;
    DWORD read = 0;
    DWORD command = 1;
    if (!WriteFile(pipe, &command, sizeof(DWORD), &read, NULL)) return false;
    FlushFileBuffers(pipe);
    DWORD size = 0;
    if (!ReadFile(pipe, &size, sizeof(DWORD), &read, NULL)) return false;
    if (size == 0) return false;
    BYTE ATR[100];
    if (!ReadFile(pipe, ATR, size, &read, NULL)) return false;
    return true;
}

bool PipeReader::QueryTransmit(BYTE *APDU, int APDUlen, BYTE **Resp, int *Resplen) {
    if (pipe == NULL) return false;
    DWORD command = 2;
    DWORD read = 0;
    if (!WriteFile(pipe, &command, sizeof(DWORD), &read, NULL)) { pipe = NULL; return false; }
    DWORD dwAPDUlen = (DWORD)APDUlen;
    if (!WriteFile(pipe, &dwAPDUlen, sizeof(DWORD), &read, NULL)) { pipe = NULL; return false; }
    if (!WriteFile(pipe, APDU, APDUlen, &read, NULL))             { pipe = NULL; return false; }
    FlushFileBuffers(pipe);
    DWORD dwRespLen = 0;
    if (!ReadFile(pipe, &dwRespLen, sizeof(DWORD), &read, NULL))  { pipe = NULL; return false; }
    if (dwRespLen != 0) {
        BYTE *p = (BYTE*)realloc(*Resp, dwRespLen);
        if (p == NULL) { pipe = NULL; return false; }
        *Resp = p;
    }
    if (!ReadFile(pipe, *Resp, dwRespLen, &read, NULL))           { pipe = NULL; return false; }
    *Resplen = (int)dwRespLen;
    return true;
}

bool PipeReader::QueryATR(BYTE *ATR, DWORD *ATRsize, bool reset) {
    if (pipe == NULL) return false;
    DWORD command = reset ? 0 : 1;
    DWORD read = 0;
    if (!WriteFile(pipe, &command, sizeof(DWORD), &read, NULL))   { pipe = NULL; return false; }
    FlushFileBuffers(pipe);
    DWORD size = 0;
    if (!ReadFile(pipe, &size, sizeof(DWORD), &read, NULL))       { pipe = NULL; return false; }
    if (size == 0) return false;
    size = min(size, *ATRsize);
    if (!ReadFile(pipe, ATR, size, &read, NULL))                  { pipe = NULL; return false; }
    *ATRsize = size;
    return true;
}

DWORD PipeReader::startServer() {
    SECURITY_ATTRIBUTES sa;
    sa.nLength        = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;
    CreateMyDACL(&sa);

    wchar_t temp[300];
    swprintf_s(temp, ARRAY_SIZE(temp), L"\\\\.\\pipe\\%s", pipeName);
    HANDLE _pipe      = CreateNamedPipe(temp, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                        PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 0, 0, 0, &sa);
    swprintf_s(temp, ARRAY_SIZE(temp), L"\\\\.\\pipe\\%s", pipeEventName);
    HANDLE _eventpipe = CreateNamedPipe(temp, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                        PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 0, 0, 0, &sa);
    wchar_t log[300];
    swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader]Pipe created:%s:%p", pipeName, _pipe);
    OutputDebugString(log);

    while (true) {
        BOOL ris = ConnectNamedPipe(_pipe, NULL);
        if (ris == 0) {
            swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader]Pipe NOT connected:%x", GetLastError());
            OutputDebugString(log);
        } else {
            OutputDebugString(L"[BixVReader]Pipe connected");
        }
        ris = ConnectNamedPipe(_eventpipe, NULL);
        if (ris == 0) {
            swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader]Event Pipe NOT connected:%x", GetLastError());
            OutputDebugString(log);
        } else {
            OutputDebugString(L"[BixVReader]Event Pipe connected");
        }
        pipe      = _pipe;
        eventpipe = _eventpipe;

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
            DWORD read    = 0;
            if (!ReadFile(eventpipe, &command, sizeof(DWORD), &read, NULL)) {
                state = SCARD_ABSENT;
                OutputDebugString(L"[BixVReader]Pipe error");
                powered   = 0;
                pipe      = NULL;
                eventpipe = NULL;

                if (!waitRemoveIpr.empty()) {
                    SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                    while (!waitRemoveIpr.empty()) {
                        WDFREQUEST req = waitRemoveIpr.back();
                        waitRemoveIpr.pop_back();
                        OutputDebugString(L"[BixVReader]complete Wait Remove");
                        if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
                            WdfRequestCompleteWithInformation(req, STATUS_SUCCESS, 0);
                            OutputDebugString(L"[BixVReader]Wait Remove Completed");
                        }
                    }
                }
                if (!waitInsertIpr.empty()) {
                    SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                    while (!waitInsertIpr.empty()) {
                        WDFREQUEST req = waitInsertIpr.back();
                        waitInsertIpr.pop_back();
                        OutputDebugString(L"[BixVReader]cancel Wait Insert");
                        if (WdfRequestUnmarkCancelable(req) != STATUS_CANCELLED) {
                            WdfRequestCompleteWithInformation(req, STATUS_CANCELLED, 0);
                        }
                    }
                }
                DisconnectNamedPipe(_pipe);
                DisconnectNamedPipe(_eventpipe);
                break;
            }
            OutputDebugString(L"[BixVReader]Pipe data");
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
    return 0;
}

void PipeReader::shutdown() {
    state = SCARD_ABSENT;
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
