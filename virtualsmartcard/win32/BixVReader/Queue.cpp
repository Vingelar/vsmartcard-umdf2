//
// queue.cpp - default I/O queue for the UMDF 2 port.
//
// All IOCTLs land here.  We forward to BixVReaderProcessIoControl which then
// dispatches to the right Reader instance based on the file name attached to
// the request.
//

#include "internal.h"
#include "device.h"
#include "queue.h"

NTSTATUS
BixVReaderCreateDefaultQueue(_In_ WDFDEVICE Device, _Out_ WDFQUEUE* Queue)
{
    WDF_IO_QUEUE_CONFIG cfg;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchParallel);

    cfg.PowerManaged           = WdfFalse;
    cfg.AllowZeroLengthRequests = TRUE;
    cfg.EvtIoDeviceControl     = BixVReaderEvtIoDeviceControl;

    NTSTATUS status = WdfIoQueueCreate(Device, &cfg, WDF_NO_OBJECT_ATTRIBUTES, Queue);
    if (!NT_SUCCESS(status)) {
        wchar_t log[200];
        swprintf_s(log, ARRAY_SIZE(log),
                   L"[BixVReader]WdfIoQueueCreate failed: 0x%08X", status);
        OutputDebugString(log);
    } else {
        OutputDebugString(L"[BixVReader]IoQueue Created");
    }
    return status;
}

VOID
BixVReaderEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode)
{
    BixVReaderProcessIoControl(Queue, Request,
                               OutputBufferLength, InputBufferLength,
                               IoControlCode);
}
