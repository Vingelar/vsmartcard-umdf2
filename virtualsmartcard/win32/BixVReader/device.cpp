//
// device.cpp - UMDF 2 device callbacks for BixVReader.
//
// Maps the COM-based UMDF 1.x logic onto the WDFDEVICE / WDFREQUEST handle
// model used by UMDF 2.  The smart-card protocol logic stays in reader.cpp,
// PipeReader.cpp, TcpIpReader.cpp and VpcdReader.cpp; this file is purely the
// glue between the framework callbacks and that logic.
//

#include "internal.h"
#include "driver.h"
#include "device.h"
#include "queue.h"
#include "reader.h"
#include "memory.h"
#include "sectionLocker.h"

#include <winscard.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>

NTSTATUS
BixVReaderEvtDriverDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    OutputDebugString(L"[BixVReader]EvtDriverDeviceAdd");

    // Set up PnP/Power callbacks BEFORE creating the device.
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware  = BixVReaderEvtDevicePrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware  = BixVReaderEvtDeviceReleaseHardware;
    pnpCallbacks.EvtDeviceD0Entry          = BixVReaderEvtDeviceD0Entry;
    pnpCallbacks.EvtDeviceD0Exit           = BixVReaderEvtDeviceD0Exit;
    pnpCallbacks.EvtDeviceQueryRemove      = BixVReaderEvtDeviceQueryRemove;
    pnpCallbacks.EvtDeviceQueryStop        = BixVReaderEvtDeviceQueryStop;
    pnpCallbacks.EvtDeviceSurpriseRemoval  = BixVReaderEvtDeviceSurpriseRemoval;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    // Per-device-context attributes.
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    deviceAttributes.EvtCleanupCallback = BixVReaderEvtDeviceContextCleanup;

    WDFDEVICE device = NULL;
    NTSTATUS  status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        wchar_t log[200];
        swprintf_s(log, ARRAY_SIZE(log),
                   L"[BixVReader]WdfDeviceCreate failed: 0x%08X", status);
        OutputDebugString(log);
        return status;
    }

    PDEVICE_CONTEXT ctx = BixVReaderGetDeviceContext(device);
    ctx->WdfDevice    = device;
    ctx->DefaultQueue = NULL;
    ctx->Readers      = new std::vector<Reader*>();
    ctx->NumInstances = 0;
    InitializeCriticalSection(&ctx->RequestLock);

    // How many virtual readers does the .ini file ask for?
    ctx->NumInstances =
        GetPrivateProfileInt(L"Driver", L"NumReaders", 1, L"BixVReader.ini");
    if (ctx->NumInstances < 1) {
        ctx->NumInstances = 1;
    }

    // Initialise Winsock once per device (the TCP and VPCD readers need it).
    WSADATA wsa;
    int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (wsaErr != 0) {
        wchar_t log[200];
        swprintf_s(log, ARRAY_SIZE(log),
                   L"[BixVReader]WSAStartup failed: %d", wsaErr);
        OutputDebugString(log);
    }

    // Create one device interface per virtual reader instance, using "DEV<i>"
    // as reference string so user mode can pick a specific one via the file
    // name when opening the device.
    for (int i = 0; i < ctx->NumInstances; ++i) {
        wchar_t refString[16];
        swprintf_s(refString, ARRAY_SIZE(refString), L"DEV%d", i);

        // RtlInitUnicodeString lives in ntdll, but pulling that in just for
        // this one call is wasteful - the struct is trivial to fill by hand.
        UNICODE_STRING refStringUs;
        size_t len = wcslen(refString);
        refStringUs.Length        = (USHORT)(len * sizeof(WCHAR));
        refStringUs.MaximumLength = (USHORT)((len + 1) * sizeof(WCHAR));
        refStringUs.Buffer        = refString;

        status = WdfDeviceCreateDeviceInterface(
            device, &SmartCardReaderGuid, &refStringUs);
        if (!NT_SUCCESS(status)) {
            wchar_t log[200];
            swprintf_s(log, ARRAY_SIZE(log),
                       L"[BixVReader]WdfDeviceCreateDeviceInterface(%s) failed: 0x%08X",
                       refString, status);
            OutputDebugString(log);
            return status;
        }
    }

    // Default queue (parallel dispatch).  Our IOCTL handler dispatches to the
    // appropriate Reader instance based on the file name on the request.
    status = BixVReaderCreateDefaultQueue(device, &ctx->DefaultQueue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    OutputDebugString(L"[BixVReader]EvtDriverDeviceAdd ok");
    return STATUS_SUCCESS;
}

VOID
BixVReaderEvtDeviceContextCleanup(
    _In_ WDFOBJECT DeviceObject)
{
    PDEVICE_CONTEXT ctx = BixVReaderGetDeviceContext((WDFDEVICE)DeviceObject);

    OutputDebugString(L"[BixVReader]EvtDeviceContextCleanup");

    // Belt-and-braces: make sure no readers are leaked if D0Exit didn't run.
    if (ctx->Readers != nullptr) {
        BixVReaderShutdownReaders(ctx);
        delete ctx->Readers;
        ctx->Readers = nullptr;
    }
    DeleteCriticalSection(&ctx->RequestLock);
    WSACleanup();
}

NTSTATUS
BixVReaderEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourceList,
    _In_ WDFCMRESLIST ResourceListTranslated)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);
    OutputDebugString(L"[BixVReader]EvtDevicePrepareHardware");
    return STATUS_SUCCESS;
}

NTSTATUS
BixVReaderEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourceListTranslated)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourceListTranslated);
    OutputDebugString(L"[BixVReader]EvtDeviceReleaseHardware");
    return STATUS_SUCCESS;
}

static DWORD WINAPI ReaderServerThread(LPVOID p)
{
    Reader* r = reinterpret_cast<Reader*>(p);
    return r->startServer();
}

NTSTATUS
BixVReaderEvtDeviceD0Entry(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);

    PDEVICE_CONTEXT ctx = BixVReaderGetDeviceContext(Device);

    OutputDebugString(L"[BixVReader]EvtDeviceD0Entry");

    // Re-read NumReaders here in case the .ini changed.
    int n = GetPrivateProfileInt(L"Driver", L"NumReaders", 1, L"BixVReader.ini");
    if (n < 1) n = 1;
    ctx->NumInstances = n;
    ctx->Readers->resize(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        wchar_t section[64];
        char    sectionA[64];
        swprintf_s(section,  ARRAY_SIZE(section),  L"Reader%d", i);
        sprintf_s(sectionA,  ARRAY_SIZE(sectionA),  "Reader%d",  i);

        int rpcType = GetPrivateProfileInt(section, L"RPC_TYPE", 0, L"BixVReader.ini");
        Reader* r = nullptr;
        if      (rpcType == 0) r = new PipeReader();
        else if (rpcType == 1) r = new TcpIpReader();
        else if (rpcType == 2) r = new VpcdReader();
        else                   r = new PipeReader();

        r->instance   = i;
        r->deviceCtx  = ctx;
        GetPrivateProfileStringA(sectionA, "VENDOR_NAME",
                                 "Bix", r->vendorName, sizeof r->vendorName,
                                 "BixVReader.ini");
        GetPrivateProfileStringA(sectionA, "VENDOR_IFD_TYPE",
                                 "VIRTUAL_CARD_READER",
                                 r->vendorIfdType, sizeof r->vendorIfdType,
                                 "BixVReader.ini");
        r->deviceUnit = GetPrivateProfileInt(section, L"DEVICE_UNIT", i, L"BixVReader.ini");
        r->protocol   = 0;
        r->init(section);

        (*ctx->Readers)[i] = r;

        DWORD threadId = 0;
        r->serverThread = CreateThread(NULL, 0, ReaderServerThread, r, 0, &threadId);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
BixVReaderEvtDeviceD0Exit(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(TargetState);

    PDEVICE_CONTEXT ctx = BixVReaderGetDeviceContext(Device);

    OutputDebugString(L"[BixVReader]EvtDeviceD0Exit");

    BixVReaderShutdownReaders(ctx);
    return STATUS_SUCCESS;
}

NTSTATUS
BixVReaderEvtDeviceQueryRemove(_In_ WDFDEVICE Device)
{
    PDEVICE_CONTEXT ctx = BixVReaderGetDeviceContext(Device);
    OutputDebugString(L"[BixVReader]EvtDeviceQueryRemove");
    BixVReaderShutdownReaders(ctx);
    return STATUS_SUCCESS;
}

NTSTATUS
BixVReaderEvtDeviceQueryStop(_In_ WDFDEVICE Device)
{
    PDEVICE_CONTEXT ctx = BixVReaderGetDeviceContext(Device);
    OutputDebugString(L"[BixVReader]EvtDeviceQueryStop");
    BixVReaderShutdownReaders(ctx);
    return STATUS_SUCCESS;
}

VOID
BixVReaderEvtDeviceSurpriseRemoval(_In_ WDFDEVICE Device)
{
    PDEVICE_CONTEXT ctx = BixVReaderGetDeviceContext(Device);
    OutputDebugString(L"[BixVReader]EvtDeviceSurpriseRemoval");
    BixVReaderShutdownReaders(ctx);
}

VOID
BixVReaderShutdownReaders(_In_ PDEVICE_CONTEXT ctx)
{
    if (!ctx || !ctx->Readers) return;

    for (int i = 0; i < ctx->NumInstances; ++i) {
        Reader* r = (*ctx->Readers)[i];
        if (r) {
            r->shutdown();
            delete r;
            (*ctx->Readers)[i] = nullptr;
        }
    }
    ctx->NumInstances = 0;
}

//
// Derive the instance index ("DEV%d") from the file name attached to the
// request.  The trailing digits of the reference string are decoded.  Returns
// 0 if anything is unparseable.
//
int BixVReaderInstanceFromRequest(_In_ WDFREQUEST Request)
{
    WDFFILEOBJECT fileObject = WdfRequestGetFileObject(Request);
    if (fileObject == NULL) {
        return 0;
    }

    PCUNICODE_STRING name = WdfFileObjectGetFileName(fileObject);
    if (name == NULL || name->Length == 0 || name->Buffer == NULL) {
        return 0;
    }

    // Walk back from the end and read trailing digits.
    USHORT count = name->Length / sizeof(WCHAR);
    USHORT end   = count;
    while (end > 0 && name->Buffer[end - 1] >= L'0' && name->Buffer[end - 1] <= L'9') {
        --end;
    }
    if (end == count) {
        return 0;
    }
    return _wtoi(&name->Buffer[end]);
}

VOID
BixVReaderProcessIoControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode)
{
    UNREFERENCED_PARAMETER(Queue);

    WDFDEVICE       device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT ctx    = BixVReaderGetDeviceContext(device);

    wchar_t log[200];
    swprintf_s(log, ARRAY_SIZE(log),
               L"[BixVReader][IOCT]IOCTL %08X - In %zu Out %zu",
               IoControlCode, InputBufferLength, OutputBufferLength);
    OutputDebugString(log);

    int instance = BixVReaderInstanceFromRequest(Request);
    if (instance < 0 || instance >= ctx->NumInstances ||
        ctx->Readers == nullptr || (*ctx->Readers)[instance] == nullptr) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_STATE, 0);
        return;
    }

    Reader& reader = *(*ctx->Readers)[instance];

    switch (IoControlCode) {
        case IOCTL_SMARTCARD_GET_ATTRIBUTE:
            reader.IoSmartCardGetAttribute(Request, InputBufferLength, OutputBufferLength);
            return;
        case IOCTL_SMARTCARD_IS_PRESENT:
            reader.IoSmartCardIsPresent(Request, InputBufferLength, OutputBufferLength);
            return;
        case IOCTL_SMARTCARD_GET_STATE:
            reader.IoSmartCardGetState(Request, InputBufferLength, OutputBufferLength);
            return;
        case IOCTL_SMARTCARD_IS_ABSENT:
            reader.IoSmartCardIsAbsent(Request, InputBufferLength, OutputBufferLength);
            return;
        case IOCTL_SMARTCARD_POWER:
            reader.IoSmartCardPower(Request, InputBufferLength, OutputBufferLength);
            return;
        case IOCTL_SMARTCARD_SET_ATTRIBUTE:
            reader.IoSmartCardSetAttribute(Request, InputBufferLength, OutputBufferLength);
            return;
        case IOCTL_SMARTCARD_SET_PROTOCOL:
            reader.IoSmartCardSetProtocol(Request, InputBufferLength, OutputBufferLength);
            return;
        case IOCTL_SMARTCARD_TRANSMIT:
            reader.IoSmartCardTransmit(Request, InputBufferLength, OutputBufferLength);
            return;
        default:
            swprintf_s(log, ARRAY_SIZE(log),
                       L"[BixVReader][IOCT]ERROR_NOT_SUPPORTED:%08X", IoControlCode);
            OutputDebugString(log);
            WdfRequestCompleteWithInformation(Request, STATUS_NOT_SUPPORTED, 0);
            return;
    }
}

//
// EvtRequestCancel runs when an upper layer cancels a pending IS_PRESENT or
// IS_ABSENT request.  The OwningReader is stored in the per-request context.
//
VOID
BixVReaderEvtRequestCancel(_In_ WDFREQUEST Request)
{
    OutputDebugString(L"[BixVReader]EvtRequestCancel");

    PREQUEST_CONTEXT reqCtx = BixVReaderGetRequestContext(Request);
    Reader*          reader = (reqCtx != nullptr) ? reqCtx->OwningReader : nullptr;

    if (reader != nullptr) {
        SectionLocker lock(reader->deviceCtx->RequestLock,
                           const_cast<char*>(__FUNCTION__), __LINE__, reader);

        for (auto it = reader->waitRemoveIpr.begin();
             it != reader->waitRemoveIpr.end(); ++it) {
            if (*it == Request) {
                OutputDebugString(L"[BixVReader]Cancel Remove");
                reader->waitRemoveIpr.erase(it);
                break;
            }
        }
        for (auto it = reader->waitInsertIpr.begin();
             it != reader->waitInsertIpr.end(); ++it) {
            if (*it == Request) {
                OutputDebugString(L"[BixVReader]Cancel Insert");
                reader->waitInsertIpr.erase(it);
                break;
            }
        }
    }

    WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0);
}
