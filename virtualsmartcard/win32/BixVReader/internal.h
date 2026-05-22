//
// internal.h - common pre-compiled header for the UMDF 2 port of BixVReader.
//
// The UMDF 1.x version of this driver was COM/ATL based (IDriverEntry,
// IPnpCallback, IQueueCallbackDeviceIoControl, ...).  UMDF 2 uses the same
// handle/callback model as KMDF (WDFDRIVER, WDFDEVICE, WDFREQUEST and
// EVT_WDF_* function pointers), so this header pulls in <wdf.h> instead of
// <wudfddi.h> and drops every COM/ATL reference.
//

#pragma once

#define WIN32_LEAN_AND_MEAN

//
// <wdf.h> drags in wudfwdm.h, which expects to use kernel-mode NTSTATUS
// definitions from <ntstatus.h>.  The Win32 headers (windows.h via winnt.h)
// have a small subset of the same constants, which would collide.  The
// standard "WIN32_NO_STATUS" trick keeps both happy.
//
#define WIN32_NO_STATUS
#include <windows.h>
#undef  WIN32_NO_STATUS
#include <ntstatus.h>

#include <devioctl.h>

#include <wdf.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

//
// SmartCard-reader class device interface GUID.  The GUID *data* is emitted
// in exactly one translation unit (driver.cpp) which includes <initguid.h>
// before this header.  Everywhere else we just want the extern declaration.
//
EXTERN_C const GUID SmartCardReaderGuid;

#include "specstrings.h"

#define inFunc
