//
// driver.h - WDFDRIVER glue for the UMDF 2 port.
//

#pragma once

#include "internal.h"

EXTERN_C_START

DRIVER_INITIALIZE              DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD      BixVReaderEvtDriverDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP BixVReaderEvtDriverContextCleanup;

EXTERN_C_END
