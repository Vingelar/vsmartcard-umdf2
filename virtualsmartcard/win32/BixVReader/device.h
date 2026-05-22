//
// device.h - per-device context and WDF callbacks for the UMDF 2 port.
//

#pragma once

#include "internal.h"
#include <vector>

class Reader;

// Per-device context attached to every WDFDEVICE we create.
typedef struct _DEVICE_CONTEXT {
    WDFDEVICE        WdfDevice;
    WDFQUEUE         DefaultQueue;
    CRITICAL_SECTION RequestLock;
    std::vector<Reader*>* Readers;
    int              NumInstances;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, BixVReaderGetDeviceContext)

// Per-request context.  We use it from the cancel callback to remove the
// request from whichever reader's wait-list it had been parked on.
typedef struct _REQUEST_CONTEXT {
    Reader* OwningReader;
} REQUEST_CONTEXT, *PREQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, BixVReaderGetRequestContext)

EXTERN_C_START

EVT_WDF_DEVICE_PREPARE_HARDWARE  BixVReaderEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE  BixVReaderEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY          BixVReaderEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT           BixVReaderEvtDeviceD0Exit;
EVT_WDF_DEVICE_QUERY_REMOVE      BixVReaderEvtDeviceQueryRemove;
EVT_WDF_DEVICE_QUERY_STOP        BixVReaderEvtDeviceQueryStop;
EVT_WDF_DEVICE_SURPRISE_REMOVAL  BixVReaderEvtDeviceSurpriseRemoval;
EVT_WDF_DEVICE_CONTEXT_CLEANUP   BixVReaderEvtDeviceContextCleanup;
EVT_WDF_REQUEST_CANCEL           BixVReaderEvtRequestCancel;

VOID BixVReaderProcessIoControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode);

VOID BixVReaderShutdownReaders(_In_ PDEVICE_CONTEXT ctx);

EXTERN_C_END

//
// Convenience accessor: derive the instance index ("DEV%i") from the file name
// associated with a request.  Returns 0 if the file name cannot be parsed.
//
int BixVReaderInstanceFromRequest(_In_ WDFREQUEST Request);
