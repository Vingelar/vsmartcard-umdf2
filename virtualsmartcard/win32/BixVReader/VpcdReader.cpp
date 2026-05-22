//
// VpcdReader.cpp - VPCD backend (talks to vsmartcard's vpicc).
// UMDF 2 port: WDFREQUEST handles instead of CComPtr<IWDFIoRequest>.
//

#include "internal.h"
#include "reader.h"
#include "device.h"
#include "memory.h"
#include "sectionLocker.h"

#include <winscard.h>
#include "../../src/vpcd/vpcd.h"

int VpcdReader::portBase;

VpcdReader::VpcdReader() {
    rpcType     = 2;
    state       = SCARD_ABSENT;
    cardPresent = false;
    breakSocket = false;
    ctx         = nullptr;
    port        = 0;
    InitializeCriticalSection(&ioSection);
}

VpcdReader::~VpcdReader() {
    DeleteCriticalSection(&ioSection);
}

void VpcdReader::init(wchar_t *section) {
    portBase = GetPrivateProfileInt(L"Driver", L"RPC_PORT_BASE", VPCDPORT, L"BixVReader.ini");
    port = (short)GetPrivateProfileInt(section, L"TCP_PORT", portBase + instance, L"BixVReader.ini");
}

bool VpcdReader::CheckATR() {
    bool r = (vicc_present((struct vicc_ctx*)ctx) == 1);
    if (r) signalInsertion();
    else   signalRemoval();
    return r;
}

bool VpcdReader::QueryTransmit(BYTE *APDU, int APDUlen, BYTE **Resp, int *Resplen) {
    bool r = false;
    if (APDU && APDUlen && Resplen) {
        ssize_t rapdu_len = vicc_transmit((struct vicc_ctx*)ctx, APDUlen, APDU, Resp);
        if (rapdu_len > 0) { *Resplen = (int)rapdu_len; r = true; }
        else                signalRemoval();
    }
    return r;
}

bool VpcdReader::QueryATR(BYTE *ATR, DWORD *ATRsize, bool reset) {
    unsigned char *atr = nullptr;
    bool r = false;

    if (ATR && ATRsize) {
        int atr_len = vicc_getatr((struct vicc_ctx*)ctx, &atr);
        if (atr_len > 0) {
            atr_len = (int)min((DWORD)atr_len, *ATRsize);
            memcpy(ATR, atr, atr_len);
            *ATRsize = atr_len;
            free(atr);
            r = true;
            if (reset) vicc_reset((struct vicc_ctx*)ctx);
        } else {
            signalRemoval();
        }
    }
    return r;
}

DWORD VpcdReader::startServer() {
    breakSocket = false;
    ctx = vicc_init(NULL, port);
    while (!breakSocket) {
        CheckATR();
        Sleep(1000);
    }
    return 0;
}

void VpcdReader::shutdown() {
    breakSocket = true;
    if (serverThread) {
        WaitForSingleObject(serverThread, 10000);
        CloseHandle(serverThread);
        serverThread = NULL;
    }
    if (ctx) {
        vicc_exit((struct vicc_ctx*)ctx);
        ctx = nullptr;
    }
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

void VpcdReader::signalRemoval(void) {
    if (cardPresent) {
        cardPresent = false;
        state       = SCARD_ABSENT;
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
    }
}

void VpcdReader::signalInsertion(void) {
    if (!cardPresent) {
        cardPresent = true;
        while (!waitInsertIpr.empty()) {
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
                break;
            }
            break;
        }
    }
}
