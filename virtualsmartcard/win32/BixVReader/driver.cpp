//
// driver.cpp - WDFDRIVER bootstrap for the UMDF 2 port of BixVReader.
//
// DriverEntry is the new entry point.  It replaces the COM-based IDriverEntry
// (CMyDriver) that the UMDF 1.x version used.  All per-device setup happens
// in BixVReaderEvtDriverDeviceAdd, which is called by the framework once for
// each device instance that PnP enumerates for our service.
//

#include <initguid.h>
#include "internal.h"
#include "driver.h"
#include "device.h"

// Emit storage for the SmartCardReader class GUID exactly once.
DEFINE_GUID(SmartCardReaderGuid,
            0x50DD5230, 0xBA8A, 0x11D1, 0xBF, 0x5D, 0x00, 0x00, 0xF8, 0x05, 0xF5, 0x30);

EXTERN_C
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG     config;
    WDF_OBJECT_ATTRIBUTES attributes;

    OutputDebugString(L"[BixVReader]DriverEntry");

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = BixVReaderEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, BixVReaderEvtDriverDeviceAdd);

    NTSTATUS status = WdfDriverCreate(DriverObject,
                                      RegistryPath,
                                      &attributes,
                                      &config,
                                      WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        wchar_t log[200];
        swprintf_s(log, ARRAY_SIZE(log),
                   L"[BixVReader]WdfDriverCreate failed: 0x%08X", status);
        OutputDebugString(log);
    }
    return status;
}

VOID
BixVReaderEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    OutputDebugString(L"[BixVReader]EvtDriverContextCleanup");
}
