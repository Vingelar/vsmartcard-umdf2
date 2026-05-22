//
// memory.h - tiny helpers around WDFREQUEST input/output buffers.
//

#pragma once

#include "internal.h"
#include "device.h"

bool  getBuffer(WDFREQUEST request, void **buffer, size_t *bufferLen);
void  setString(PDEVICE_CONTEXT ctx, WDFREQUEST request, const char *result, size_t outSize);
void  setBuffer(PDEVICE_CONTEXT ctx, WDFREQUEST request, const void *result, size_t inSize);
void  setInt   (PDEVICE_CONTEXT ctx, WDFREQUEST request, DWORD result);
DWORD getInt   (WDFREQUEST request);
