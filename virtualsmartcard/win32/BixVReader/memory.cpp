//
// memory.cpp - UMDF 2 buffer helpers.
//
// UMDF 1.x exposed buffers via IWDFMemory; UMDF 2 returns plain pointers and
// sizes through WdfRequestRetrieveInputBuffer / WdfRequestRetrieveOutputBuffer.
//

#include "memory.h"
#include "sectionLocker.h"

#include <stdlib.h>
#include <string.h>

bool getBuffer(WDFREQUEST request, void **buffer, size_t *bufferLen)
{
    PVOID    data = nullptr;
    size_t   size = 0;

    NTSTATUS status = WdfRequestRetrieveInputBuffer(request, 0, &data, &size);
    if (!NT_SUCCESS(status) || data == nullptr) {
        OutputDebugString(L"[BixVReader]WdfRequestRetrieveInputBuffer failed");
        return false;
    }

    if (size != 0) {
        void* out = realloc(*buffer, size);
        if (out == nullptr) {
            OutputDebugString(L"[BixVReader]realloc failed");
            return false;
        }
        memcpy(out, data, size);
        *buffer = out;
    }
    *bufferLen = size;
    return true;
}

void setBuffer(PDEVICE_CONTEXT ctx, WDFREQUEST request, const void *result, size_t inSize)
{
    PVOID    outBuf = nullptr;
    size_t   outLen = 0;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(request, inSize, &outBuf, &outLen);

    SectionLocker lock(ctx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, request);
    if (!NT_SUCCESS(status) || outBuf == nullptr) {
        OutputDebugString(L"[BixVReader]WdfRequestRetrieveOutputBuffer failed");
        WdfRequestComplete(request, status);
        return;
    }
    memcpy(outBuf, result, inSize);
    WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, inSize);
}

void setString(PDEVICE_CONTEXT ctx, WDFREQUEST request, const char *result, size_t outSize)
{
    size_t want = strlen(result) + 1;
    size_t size = (outSize > 0 && outSize < want) ? outSize : want;

    PVOID    outBuf = nullptr;
    size_t   outLen = 0;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(request, size, &outBuf, &outLen);

    SectionLocker lock(ctx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, request);
    if (!NT_SUCCESS(status) || outBuf == nullptr) {
        OutputDebugString(L"[BixVReader]WdfRequestRetrieveOutputBuffer failed");
        WdfRequestComplete(request, status);
        return;
    }
    if (size > outLen) size = outLen;
    memcpy(outBuf, result, size);
    WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, size);
}

void setInt(PDEVICE_CONTEXT ctx, WDFREQUEST request, DWORD result)
{
    PVOID    outBuf = nullptr;
    size_t   outLen = 0;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(request, sizeof(result), &outBuf, &outLen);

    SectionLocker lock(ctx->RequestLock, const_cast<char*>(__FUNCTION__), __LINE__, request);
    if (!NT_SUCCESS(status) || outBuf == nullptr) {
        OutputDebugString(L"[BixVReader]WdfRequestRetrieveOutputBuffer failed");
        WdfRequestComplete(request, status);
        return;
    }
    memcpy(outBuf, &result, sizeof(result));
    WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, sizeof(result));
}

DWORD getInt(WDFREQUEST request)
{
    PVOID    data = nullptr;
    size_t   size = 0;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(request, sizeof(DWORD), &data, &size);
    if (!NT_SUCCESS(status) || data == nullptr || size < sizeof(DWORD)) {
        OutputDebugString(L"[BixVReader]getInt: bad input");
        return 0xFFFFFFFFu;
    }
    DWORD d = 0;
    memcpy(&d, data, sizeof(d));
    return d;
}
