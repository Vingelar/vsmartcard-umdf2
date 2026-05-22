//
// queue.h - default I/O queue setup for the UMDF 2 port.
//

#pragma once

#include "internal.h"

EXTERN_C_START

NTSTATUS BixVReaderCreateDefaultQueue(_In_ WDFDEVICE Device, _Out_ WDFQUEUE* Queue);

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL BixVReaderEvtIoDeviceControl;

EXTERN_C_END
