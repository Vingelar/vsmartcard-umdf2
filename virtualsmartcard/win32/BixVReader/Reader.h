//
// reader.h - Smart-card reader business logic interfaces.
//
// UMDF 2 port:  every pending request is a WDFREQUEST handle; the previous
// CComPtr<IWDFIoRequest> wrappers are gone (handles are not reference
// counted).  The Reader owns a PDEVICE_CONTEXT pointer so it can reach the
// per-device CRITICAL_SECTION used to serialise request completions.
//

#pragma once

#include "internal.h"

#include <winsock2.h>
#include <vector>

struct _DEVICE_CONTEXT;
typedef struct _DEVICE_CONTEXT DEVICE_CONTEXT, *PDEVICE_CONTEXT;

class Reader {
public:
    PDEVICE_CONTEXT             deviceCtx;
    std::vector<WDFREQUEST>     waitRemoveIpr;
    std::vector<WDFREQUEST>     waitInsertIpr;

    HANDLE  serverThread;

    char    vendorName[300];
    char    vendorIfdType[300];
    int     deviceUnit;
    int     powered;
    int     instance;
    int     rpcType;
    int     state;
    DWORD   protocol;          // T0 or T1 - protocol in use
    DWORD   availableProtocol; // T0, T1 or Both - protocols available

    void IoSmartCardGetAttribute(WDFREQUEST request, size_t inBufSize, size_t outBufSize);
    void IoSmartCardIsPresent   (WDFREQUEST request, size_t inBufSize, size_t outBufSize);
    void IoSmartCardGetState    (WDFREQUEST request, size_t inBufSize, size_t outBufSize);
    void IoSmartCardIsAbsent    (WDFREQUEST request, size_t inBufSize, size_t outBufSize);
    void IoSmartCardPower       (WDFREQUEST request, size_t inBufSize, size_t outBufSize);
    void IoSmartCardSetAttribute(WDFREQUEST request, size_t inBufSize, size_t outBufSize);
    void IoSmartCardSetProtocol (WDFREQUEST request, size_t inBufSize, size_t outBufSize);
    void IoSmartCardTransmit    (WDFREQUEST request, size_t inBufSize, size_t outBufSize);

    bool initProtocols();
    virtual bool QueryTransmit(BYTE *APDU, int APDUlen, BYTE **Resp, int *Resplen);
    virtual bool QueryATR     (BYTE *ATR,  DWORD *ATRsize, bool reset = false);
    virtual bool CheckATR();
    virtual DWORD startServer();
    virtual void  shutdown();
    virtual void  init(wchar_t *section);

    virtual ~Reader() {}
};

class PipeReader : public Reader {
public:
    wchar_t pipeName[300];
    wchar_t pipeEventName[300];
    HANDLE  pipe;
    HANDLE  eventpipe;

    PipeReader();
    bool QueryTransmit(BYTE *APDU, int APDUlen, BYTE **Resp, int *Resplen) override;
    bool QueryATR     (BYTE *ATR,  DWORD *ATRsize, bool reset = false) override;
    bool CheckATR() override;
    DWORD startServer() override;
    void  shutdown() override;
    void  init(wchar_t *section) override;

    CRITICAL_SECTION eventSection;
    CRITICAL_SECTION dataSection;
};

class TcpIpReader : public Reader {
public:
    static int portBase;
    int     port;
    int     eventPort;
    SOCKET  socket;
    SOCKET  AcceptSocket;
    SOCKET  eventsocket;
    bool    breakSocket;

    TcpIpReader();
    bool QueryTransmit(BYTE *APDU, int APDUlen, BYTE **Resp, int *Resplen) override;
    bool QueryATR     (BYTE *ATR,  DWORD *ATRsize, bool reset = false) override;
    bool CheckATR() override;
    DWORD startServer() override;
    void  shutdown() override;
    void  init(wchar_t *section) override;

    CRITICAL_SECTION eventSection;
    CRITICAL_SECTION dataSection;
};

class VpcdReader : public Reader {
public:
    static int portBase;
    short   port;
    void*   ctx;
    bool    breakSocket;
    bool    cardPresent;

    VpcdReader();
    ~VpcdReader();
    bool QueryTransmit(BYTE *APDU, int APDUlen, BYTE **Resp, int *Resplen) override;
    bool QueryATR     (BYTE *ATR,  DWORD *ATRsize, bool reset = false) override;
    bool CheckATR() override;
    DWORD startServer() override;
    void  shutdown() override;
    void  init(wchar_t *section) override;
    void  signalRemoval(void);
    void  signalInsertion(void);

    CRITICAL_SECTION ioSection;
};
