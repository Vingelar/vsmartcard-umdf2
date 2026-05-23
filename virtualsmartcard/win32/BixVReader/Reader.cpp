//
// reader.cpp - SmartCard reader business logic for the UMDF 2 port.
//
// Functionally identical to the UMDF 1.x version; the only change is that
// pending requests are WDFREQUEST handles instead of CComPtr<IWDFIoRequest>
// pointers, and completions go through WdfRequestComplete*.
//

#include "internal.h"
#include "reader.h"
#include "device.h"
#include "memory.h"
#include "sectionLocker.h"

#include <winscard.h>

#ifndef SCARD_CHANNEL_TYPE_PCSC
#define SCARD_CHANNEL_TYPE_PCSC 0x00000002
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Reader::init(wchar_t *section) {
    UNREFERENCED_PARAMETER(section);
    state = SCARD_ABSENT;
}

void Reader::IoSmartCardIsPresent(WDFREQUEST request, size_t inBufSize, size_t outBufSize) {
    UNREFERENCED_PARAMETER(inBufSize);
    UNREFERENCED_PARAMETER(outBufSize);

    OutputDebugString(L"[BixVReader][IPRE]IOCTL_SMARTCARD_IS_PRESENT");
    if (CheckATR()) {
        // there's a smart card present, so complete the request
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
    }
    else {
        // leave the request pending; it will be completed later
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        waitInsertIpr.push_back(request);
        WdfRequestMarkCancelable(request, BixVReaderEvtRequestCancel);
    }
}

void Reader::IoSmartCardGetState(WDFREQUEST request, size_t inBufSize, size_t outBufSize) {
    UNREFERENCED_PARAMETER(inBufSize);
    UNREFERENCED_PARAMETER(outBufSize);
    OutputDebugString(L"[BixVReader][GSTA]IOCTL_SMARTCARD_GET_STATE");
    wchar_t log[200];
    swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader]STATE:%08X", state);
    OutputDebugString(log);
    setInt(deviceCtx, request, state);
}

void Reader::IoSmartCardIsAbsent(WDFREQUEST request, size_t inBufSize, size_t outBufSize) {
    UNREFERENCED_PARAMETER(inBufSize);
    UNREFERENCED_PARAMETER(outBufSize);
    OutputDebugString(L"[BixVReader][IABS]IOCTL_SMARTCARD_IS_ABSENT");
    if (!CheckATR()) {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
    }
    else {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        waitRemoveIpr.push_back(request);
        WdfRequestMarkCancelable(request, BixVReaderEvtRequestCancel);
    }
}

void Reader::IoSmartCardPower(WDFREQUEST request, size_t inBufSize, size_t outBufSize) {
    UNREFERENCED_PARAMETER(inBufSize);
    UNREFERENCED_PARAMETER(outBufSize);
    OutputDebugString(L"[BixVReader][POWR]IOCTL_SMARTCARD_POWER");
    DWORD code = getInt(request);
    if (code == SCARD_COLD_RESET) {
        OutputDebugString(L"[BixVReader][POWR]SCARD_COLD_RESET");
        protocol = 0; powered = 1; state = SCARD_NEGOTIABLE;
    }
    else if (code == SCARD_WARM_RESET) {
        OutputDebugString(L"[BixVReader][POWR]SCARD_WARM_RESET");
        protocol = 0; powered = 1; state = SCARD_NEGOTIABLE;
    }
    else if (code == SCARD_POWER_DOWN) {
        OutputDebugString(L"[BixVReader][POWR]SCARD_POWER_DOWN");
        protocol = 0; powered = 0; state = SCARD_SWALLOWED;
    }

    if (code == SCARD_COLD_RESET || code == SCARD_WARM_RESET) {
        BYTE  ATR[100];
        DWORD ATRsize = sizeof(ATR);
        if (!QueryATR(ATR, &ATRsize, true)) {
            WdfRequestCompleteWithInformation(request, STATUS_NO_MEDIA, 0);
            return;
        }
        setBuffer(deviceCtx, request, ATR, ATRsize);
    }
    else {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
    }
}

void Reader::IoSmartCardSetProtocol(WDFREQUEST request, size_t inBufSize, size_t outBufSize) {
    UNREFERENCED_PARAMETER(inBufSize);
    UNREFERENCED_PARAMETER(outBufSize);

    DWORD requestedProtocol = getInt(request);
    wchar_t log[200];
    swprintf_s(log, ARRAY_SIZE(log),
               L"[BixVReader][SPRT]IOCTL_SMARTCARD_SET_PROTOCOL:%08X", requestedProtocol);
    OutputDebugString(log);

    BYTE  ATR[100];
    DWORD ATRsize = sizeof(ATR);
    state = SCARD_SPECIFIC;
    if (!QueryATR(ATR, &ATRsize, true)) {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_NO_MEDIA, 0);
        return;
    }

    if (((requestedProtocol & SCARD_PROTOCOL_T1) != 0) &&
        ((availableProtocol  & SCARD_PROTOCOL_T1) != 0)) {
        protocol = SCARD_PROTOCOL_T1;
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
        OutputDebugString(L"[BixVReader]PROTOCOL SET: T1");
        return;
    }
    if (((requestedProtocol & SCARD_PROTOCOL_T0) != 0) &&
        ((availableProtocol  & SCARD_PROTOCOL_T0) != 0)) {
        protocol = SCARD_PROTOCOL_T0;
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
        OutputDebugString(L"[BixVReader]PROTOCOL SET: T0");
        return;
    }
    if (((requestedProtocol & SCARD_PROTOCOL_DEFAULT) != 0) &&
        ((availableProtocol  & SCARD_PROTOCOL_T1)      != 0)) {
        protocol = SCARD_PROTOCOL_T1;
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
        OutputDebugString(L"[BixVReader]PROTOCOL SET: T1");
        return;
    }
    if (((requestedProtocol & SCARD_PROTOCOL_DEFAULT) != 0) &&
        ((availableProtocol  & SCARD_PROTOCOL_T0)      != 0)) {
        protocol = SCARD_PROTOCOL_T0;
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
        OutputDebugString(L"[BixVReader]PROTOCOL SET: T0");
        return;
    }
    state = SCARD_NEGOTIABLE;
    SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
    WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
}

void Reader::IoSmartCardSetAttribute(WDFREQUEST request, size_t inBufSize, size_t outBufSize) {
    UNREFERENCED_PARAMETER(inBufSize);
    UNREFERENCED_PARAMETER(outBufSize);
    OutputDebugString(L"[BixVReader][SATT]IOCTL_SMARTCARD_SET_ATTRIBUTE");

    PVOID  data = nullptr;
    size_t size = 0;
    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(request, sizeof(DWORD), &data, &size))
        || data == nullptr || size < sizeof(DWORD)) {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
        return;
    }

    DWORD minCode = *(DWORD*)data;
    if (minCode == SCARD_ATTR_DEVICE_IN_USE) {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        OutputDebugString(L"[BixVReader][SATT]SCARD_ATTR_DEVICE_IN_USE");
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, 0);
        return;
    }

    wchar_t log[200];
    swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader][SATT]ERROR_NOT_SUPPORTED:%08X", minCode);
    OutputDebugString(log);
    SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
    WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
}

void Reader::IoSmartCardTransmit(WDFREQUEST request, size_t inBufSize, size_t outBufSize) {
    UNREFERENCED_PARAMETER(inBufSize);
    UNREFERENCED_PARAMETER(outBufSize);
    OutputDebugString(L"[BixVReader][TRSM]IOCTL_SMARTCARD_TRANSMIT");

    SCARD_IO_REQUEST* scardRequest = nullptr;
    size_t            scardRequestSize = 0;
    BYTE*             RAPDU    = nullptr;
    int               RAPDUlen = 0;

    if (!getBuffer(request, (void**)&scardRequest, &scardRequestSize)
        || scardRequestSize < sizeof(SCARD_IO_REQUEST)
        || scardRequest->dwProtocol != protocol) {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_INVALID_DEVICE_STATE, 0);
        goto end;
    }

    if (!QueryTransmit((BYTE*)(scardRequest + 1),
                       (int)(scardRequestSize - sizeof(SCARD_IO_REQUEST)),
                       &RAPDU, &RAPDUlen)) {
        SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
        WdfRequestCompleteWithInformation(request, STATUS_NO_MEDIA, 0);
        goto end;
    }
    {
        SCARD_IO_REQUEST* p = (SCARD_IO_REQUEST*)realloc(scardRequest,
                                                        RAPDUlen + sizeof(SCARD_IO_REQUEST));
        if (p == nullptr) {
            SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
            WdfRequestCompleteWithInformation(request, STATUS_INVALID_DEVICE_STATE, 0);
            goto end;
        }
        scardRequest             = p;
        scardRequest->cbPciLength = sizeof(SCARD_IO_REQUEST);
        scardRequest->dwProtocol  = protocol;
        memcpy(scardRequest + 1, RAPDU, RAPDUlen);
        setBuffer(deviceCtx, request, scardRequest, RAPDUlen + sizeof(SCARD_IO_REQUEST));
    }
end:
    free(scardRequest);
    free(RAPDU);
}

void Reader::IoSmartCardGetAttribute(WDFREQUEST request, size_t inBufSize, size_t outBufSize) {
    UNREFERENCED_PARAMETER(inBufSize);

    wchar_t log[200];
    char    temp[300];

    DWORD code = getInt(request);
    swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader][GATT]  - code %0X", code);
    OutputDebugString(log);

    switch (code) {
        case SCARD_ATTR_VALUE(SCARD_CLASS_VENDOR_DEFINED, 0xA009):
            OutputDebugString(L"[BixVReader][GATT]RPC_TYPE");
            setInt(deviceCtx, request, rpcType);
            return;

        case SCARD_ATTR_VALUE(SCARD_CLASS_VENDOR_DEFINED, 0xA00a):
            if (rpcType == 0) {
                PipeReader* pipe = (PipeReader*)this;
                OutputDebugString(L"[BixVReader][GATT]PIPE_NAME");
                sprintf_s(temp, ARRAY_SIZE(temp), "%.*S", (int)ARRAY_SIZE(temp) - 1, pipe->pipeName);
                temp[sizeof(temp) - 1] = '\0';
                setString(deviceCtx, request, temp, outBufSize);
            } else {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
            }
            return;

        case SCARD_ATTR_VALUE(SCARD_CLASS_VENDOR_DEFINED, 0xA00b):
            if (rpcType == 0) {
                PipeReader* pipe = (PipeReader*)this;
                OutputDebugString(L"[BixVReader][GATT]EVENT_PIPE_NAME");
                sprintf_s(temp, ARRAY_SIZE(temp), "%.*S", (int)ARRAY_SIZE(temp) - 1, pipe->pipeEventName);
                temp[sizeof(temp) - 1] = '\0';
                setString(deviceCtx, request, temp, outBufSize);
            } else {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
            }
            return;

        case SCARD_ATTR_VALUE(SCARD_CLASS_VENDOR_DEFINED, 0xA00c):
            if (rpcType == 1) {
                TcpIpReader* tcpIp = (TcpIpReader*)this;
                OutputDebugString(L"[BixVReader][GATT]PORT");
                setInt(deviceCtx, request, tcpIp->port);
            } else {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
            }
            return;

        case SCARD_ATTR_VALUE(SCARD_CLASS_VENDOR_DEFINED, 0xA00d):
            if (rpcType == 1) {
                TcpIpReader* tcpIp = (TcpIpReader*)this;
                OutputDebugString(L"[BixVReader][GATT]EVENT_PORT");
                setInt(deviceCtx, request, tcpIp->eventPort);
            } else {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
            }
            return;

        case SCARD_ATTR_VALUE(SCARD_CLASS_VENDOR_DEFINED, 0xA00e):
            if (rpcType == 1) {
                OutputDebugString(L"[BixVReader][GATT]BASE_PORT");
                setInt(deviceCtx, request, TcpIpReader::portBase);
            } else {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
            }
            return;


        case SCARD_ATTR_CHANNEL_ID:
            // DWORD 0xDDDDCCCC: channel type in high word, channel number in low word
            OutputDebugString(L"[BixVReader][GATT]SCARD_ATTR_CHANNEL_ID");
            setInt(deviceCtx, request, (SCARD_CHANNEL_TYPE_PCSC << 16) | ((DWORD)deviceUnit & 0xFFFF));
            return;

        case SCARD_ATTR_CHARACTERISTICS:
            OutputDebugString(L"[BixVReader][GATT]SCARD_ATTR_CHARACTERISTICS");
            setInt(deviceCtx, request, 0);
            return;

        case SCARD_ATTR_VENDOR_NAME:
            OutputDebugString(L"[BixVReader][GATT]SCARD_ATTR_VENDOR_NAME");
            setString(deviceCtx, request, vendorName, outBufSize);
            return;

        case SCARD_ATTR_VENDOR_IFD_TYPE:
            OutputDebugString(L"[BixVReader][GATT]SCARD_ATTR_VENDOR_IFD_TYPE");
            setString(deviceCtx, request, vendorIfdType, outBufSize);
            return;

        case SCARD_ATTR_DEVICE_UNIT:
            OutputDebugString(L"[BixVReader][GATT]SCARD_ATTR_DEVICE_UNIT");
            setInt(deviceCtx, request, deviceUnit);
            return;

        case SCARD_ATTR_ATR_STRING: {
            OutputDebugString(L"[BixVReader][GATT]SCARD_ATTR_ATR_STRING");
            BYTE  ATR[100];
            DWORD ATRsize = sizeof(ATR);
            if (!QueryATR(ATR, &ATRsize)) {
                SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
                WdfRequestCompleteWithInformation(request, STATUS_NO_MEDIA, 0);
                return;
            }
            setBuffer(deviceCtx, request, ATR, ATRsize);
            return;
        }

        case SCARD_ATTR_CURRENT_PROTOCOL_TYPE:
            OutputDebugString(L"[BixVReader][GATT]SCARD_ATTR_CURRENT_PROTOCOL_TYPE");
            setInt(deviceCtx, request, protocol);
            return;

        default: {
            swprintf_s(log, ARRAY_SIZE(log), L"[BixVReader][GATT]ERROR_NOT_SUPPORTED:%08X", code);
            OutputDebugString(log);
            SectionLocker lock(deviceCtx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, this);
            WdfRequestCompleteWithInformation(request, STATUS_NOT_SUPPORTED, 0);
        }
    }
}

bool Reader::CheckATR() { return false; }

bool Reader::QueryTransmit(BYTE *APDU, int APDUlen, BYTE **Resp, int *Resplen) {
    UNREFERENCED_PARAMETER(APDU);
    UNREFERENCED_PARAMETER(APDUlen);
    UNREFERENCED_PARAMETER(Resp);
    UNREFERENCED_PARAMETER(Resplen);
    return false;
}

bool Reader::QueryATR(BYTE *ATR, DWORD *ATRsize, bool reset) {
    UNREFERENCED_PARAMETER(ATR);
    UNREFERENCED_PARAMETER(ATRsize);
    UNREFERENCED_PARAMETER(reset);
    return false;
}

bool Reader::initProtocols() {
    // ask ATR to determine available protocols
    BYTE  ATR[100];
    DWORD ATRsize = sizeof(ATR);
    availableProtocol = 0;
    if (QueryATR(ATR, &ATRsize, true)) {
        availableProtocol = 0;
        int iNumHistBytes = 0;
        int iTotHistBytes = 0;
        int y = 0;
        int block = 1;
        bool isHist = false;
        char let = 'A';
        for (unsigned int i = 0; i < ATRsize && !isHist; ++i) {
            if (i == 0) {
                // TS
            }
            else if (i == 1) {
                let = 'A';
                y = ATR[i] >> 4;
                iTotHistBytes = ATR[i] & 0xF;
            }
            else {
                while ((y & 1) == 0) {
                    ++let;
                    y >>= 1;
                    if (y == 0) {
                        isHist = true;
                        iNumHistBytes = 1;
                        --i;
                        goto end;
                    }
                }
                y = y & 0xE;
                if (let == 'D') {
                    protocol = (ATR[i] & 0x0f);
                    availableProtocol |= 1 << protocol;
                    let = 'A';
                    y = ATR[i] >> 4;
                    ++block;
                }
            }
end:
            ;
        }
        if (availableProtocol == 0) availableProtocol = 1;
        return true;
    }
    return false;
}

DWORD Reader::startServer() { return 0; }
void  Reader::shutdown()    {}
