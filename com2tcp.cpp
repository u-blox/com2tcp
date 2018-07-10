/*
 * $Id: com2tcp.cpp,v 1.10 2006/11/16 12:51:43 vfrolov Exp $
 *
 * Copyright (c) 2005-2006 Vyacheslav Frolov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * $Log: com2tcp.cpp,v $
 * Revision 1.10  2006/11/16 12:51:43  vfrolov
 * Added ability to set COM port parameters
 *
 * Revision 1.9  2005/11/25 13:49:23  vfrolov
 * Implemented --interface option for client mode
 *
 * Revision 1.8  2005/10/18 09:53:36  vfrolov
 * Added EVENT_ACCEPT
 *
 * Revision 1.7  2005/10/03 13:48:08  vfrolov
 * Added --ignore-dsr and listen options
 *
 * Revision 1.6  2005/06/10 15:55:10  vfrolov
 * Implemented --terminal option
 *
 * Revision 1.5  2005/06/08 15:48:17  vfrolov
 * Implemented --awak-seq option
 *
 * Revision 1.4  2005/06/07 10:06:37  vfrolov
 * Added ability to use port names
 *
 * Revision 1.3  2005/06/06 15:20:46  vfrolov
 * Implemented --telnet option
 *
 * Revision 1.2  2005/05/30 12:17:32  vfrolov
 * Fixed resolving problem
 *
 * Revision 1.1  2005/05/30 10:02:33  vfrolov
 * Initial revision
 *
 */

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <vector>
using namespace std;
#include "utils.h"
#include "telnet.h"

///////////////////////////////////////////////////////////////
static SOCKET Accept(SOCKET hSockListen);
static void Disconnect(SOCKET hSock);
///////////////////////////////////////////////////////////////
static void TraceLastError(const char *pFmt, ...)
{
  DWORD err = GetLastError();
  va_list va;
  va_start(va, pFmt);
  vfprintf(stderr, pFmt, va);
  va_end(va);

  fprintf(stderr, " ERROR %s (%lu)\n", strerror(err), (unsigned long)err);
}
///////////////////////////////////////////////////////////////
static BOOL myGetCommState(HANDLE hC0C, DCB *dcb)
{
  dcb->DCBlength = sizeof(*dcb);

  if (!GetCommState(hC0C, dcb)) {
    TraceLastError("GetCommState()");
    return FALSE;
  }
  return TRUE;
}

static BOOL mySetCommState(HANDLE hC0C, DCB *dcb)
{
  if (!SetCommState(hC0C, dcb)) {
    TraceLastError("SetCommState()");
    return FALSE;
  }
  return TRUE;
}
///////////////////////////////////////////////////////////////
static void CloseEvents(int num, HANDLE *hEvents)
{
  for (int i = 0 ; i < num ; i++) {
    if (hEvents[i]) {
      if (!::CloseHandle(hEvents[i])) {
        TraceLastError("CloseEvents(): CloseHandle()");
      }
      hEvents[i] = NULL;
    }
  }
}

static BOOL PrepareEvents(int num, HANDLE *hEvents, OVERLAPPED *overlaps)
{
  memset(hEvents, 0, num * sizeof(HANDLE));
  memset(overlaps, 0, num * sizeof(OVERLAPPED));

  for (int i = 0 ; i < num ; i++) {
    overlaps[i].hEvent = hEvents[i] = ::CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!hEvents[i]) {
      TraceLastError("PrepareEvents(): CreateEvent()");
      CloseEvents(i, hEvents);
      return FALSE;
    }
  }
  return TRUE;
}
///////////////////////////////////////////////////////////////
static void InOut(
   HANDLE hC0C,
   SOCKET hSock,
   Protocol &protocol,
   BOOL ignoreDSR,
   SOCKET hSockListen = INVALID_SOCKET)
{
  printf("InOut() START\n");

  protocol.Clean();

  BOOL stop = FALSE;

  enum {
    EVENT_READ,
    EVENT_SENT,
    EVENT_RECEIVED,
    EVENT_WRITTEN,
    EVENT_STAT,
    EVENT_CLOSE,
    EVENT_ACCEPT,
    EVENT_NUM
  };

  HANDLE hEvents[EVENT_NUM];
  OVERLAPPED overlaps[EVENT_NUM];

  if (!PrepareEvents(EVENT_NUM, hEvents, overlaps))
    stop = TRUE;

  if (!SetCommMask(hC0C, EV_DSR)) {
    TraceLastError("InOut(): SetCommMask()");
    stop = TRUE;
  }

  WSAEventSelect(hSock, hEvents[EVENT_CLOSE], FD_CLOSE);

  if (hSockListen != INVALID_SOCKET)
    WSAEventSelect(hSockListen, hEvents[EVENT_ACCEPT], FD_ACCEPT);

  DWORD not_used;

  BYTE cbufRead[64];
  BOOL waitingRead = FALSE;

  BYTE cbufSend[64];
  int cbufSendSize = 0;
  int cbufSendDone = 0;
  BOOL waitingSend = FALSE;

  BYTE cbufRecv[64];
  BOOL waitingRecv = FALSE;

  BYTE cbufWrite[64];
  int cbufWriteSize = 0;
  int cbufWriteDone = 0;
  BOOL waitingWrite = FALSE;

  BOOL waitingStat = FALSE;
  int DSR = -1;

  while (!stop) {
    if (!waitingSend) {
      if (!cbufSendSize) {
        cbufSendSize = protocol.Read(cbufSend, sizeof(cbufSend));
        if (cbufSendSize < 0)
          break;
      }

      DWORD num = cbufSendSize - cbufSendDone;

      if (num) {
        if (!WriteFile((HANDLE)hSock, cbufSend + cbufSendDone, num, &not_used, &overlaps[EVENT_SENT])) {
          if (::GetLastError() != ERROR_IO_PENDING) {
            TraceLastError("InOut(): WriteFile(hSock)");
            break;
          }
        }
        waitingSend = TRUE;
      }
    }

    if (!waitingRead && !protocol.isSendFull()) {
      if (!ReadFile(hC0C, cbufRead, sizeof(cbufRead), &not_used, &overlaps[EVENT_READ])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("InOut(): ReadFile(hC0C)");
          break;
        }
      }
      waitingRead = TRUE;
    }

    if (!waitingWrite) {
      if (!cbufWriteSize) {
        cbufWriteSize = protocol.Recv(cbufWrite, sizeof(cbufWrite));
        if (cbufWriteSize < 0)
          break;
      }

      DWORD num = cbufWriteSize - cbufWriteDone;

      if (num) {
        if (!WriteFile(hC0C, cbufWrite + cbufWriteDone, num, &not_used, &overlaps[EVENT_WRITTEN])) {
          if (::GetLastError() != ERROR_IO_PENDING) {
            TraceLastError("InOut(): WriteFile(hC0C)");
            break;
          }
        }
        waitingWrite = TRUE;
      }
    }

    if (!waitingRecv && !protocol.isWriteFull()) {
      if (!ReadFile((HANDLE)hSock, cbufRecv, sizeof(cbufRecv), &not_used, &overlaps[EVENT_RECEIVED])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("InOut(): ReadFile(hSock)");
          break;
        }
      }
      waitingRecv = TRUE;
    }

    if (!waitingStat) {
      if (!WaitCommEvent(hC0C, &not_used, &overlaps[EVENT_STAT])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("InOut(): WaitCommEvent()");
          break;
        }
      }
      waitingStat = TRUE;

      DWORD stat;

      if (!GetCommModemStatus(hC0C, &stat)) {
        TraceLastError("InOut(): GetCommModemStatus()");
        break;
      }

      if (!(stat & MS_DSR_ON)) {
        if (DSR != 0) {
          printf("DSR is OFF\n");
          DSR = 0;
        }
        if (!ignoreDSR) {
          if (waitingSend)
            Sleep(1000);
          break;
        }
      } else {
        if (DSR != 1) {
          printf("DSR is ON\n");
          DSR = 1;
        }
      }
    }

    if ((waitingRead || waitingSend) && (waitingRecv || waitingWrite) && waitingStat) {
      DWORD done;

      switch (WaitForMultipleObjects(EVENT_NUM, hEvents, FALSE, 5000)) {
      case WAIT_OBJECT_0 + EVENT_READ:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_READ], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_READ)");
            stop = TRUE;
            break;
          }
        }
        ResetEvent(hEvents[EVENT_READ]);
        waitingRead = FALSE;
        protocol.Send(cbufRead, done);
        break;
      case WAIT_OBJECT_0 + EVENT_SENT:
        if (!GetOverlappedResult((HANDLE)hSock, &overlaps[EVENT_SENT], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_SENT)");
            stop = TRUE;
            break;
          }
          done = 0;
        }
        ResetEvent(hEvents[EVENT_SENT]);
        cbufSendDone += done;
        if (cbufSendDone >= cbufSendSize)
          cbufSendDone = cbufSendSize = 0;
        waitingSend = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_RECEIVED:
        if (!GetOverlappedResult((HANDLE)hSock, &overlaps[EVENT_RECEIVED], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_RECEIVED)");
            stop = TRUE;
            break;
          }
          done = 0;
        } else if (!done) {
          ResetEvent(hEvents[EVENT_RECEIVED]);
          printf("Received EOF\n");
          break;
        }
        ResetEvent(hEvents[EVENT_RECEIVED]);
        waitingRecv = FALSE;
        protocol.Write(cbufRecv, done);
        break;
      case WAIT_OBJECT_0 + EVENT_WRITTEN:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_WRITTEN], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_WRITTEN)");
            stop = TRUE;
            break;
          }
          done = 0;
        }
        ResetEvent(hEvents[EVENT_WRITTEN]);
        cbufWriteDone += done;
        if (cbufWriteDone >= cbufWriteSize)
          cbufWriteDone = cbufWriteSize = 0;
        waitingWrite = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_STAT:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_STAT], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_STAT)");
            stop = TRUE;
            break;
          }
        }
        waitingStat = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_CLOSE:
        ResetEvent(hEvents[EVENT_CLOSE]);
        printf("EVENT_CLOSE\n");
        if (waitingWrite)
          Sleep(1000);
        stop = TRUE;
        break;
      case WAIT_OBJECT_0 + EVENT_ACCEPT: {
        ResetEvent(hEvents[EVENT_ACCEPT]);
        printf("EVENT_ACCEPT\n");

        SOCKET hSockTmp = Accept(hSockListen);

        if (hSockTmp != INVALID_SOCKET) {
          char msg[] = "*** Serial port is busy ***\n";

          send(hSockTmp, msg, (int) strlen(msg), 0);
          Disconnect(hSockTmp);
        }
        break;
      }
      case WAIT_TIMEOUT:
        break;
      default:
        TraceLastError("InOut(): WaitForMultipleObjects()");
        stop = TRUE;
      }
    }
  }

  CancelIo(hC0C);
  CancelIo((HANDLE)hSock);

  if (hSockListen != INVALID_SOCKET) {
    WSAEventSelect(hSockListen, hEvents[EVENT_ACCEPT], 0);

    u_long blocking = 0;

    ioctlsocket(hSockListen, FIONBIO, &blocking);
  }

  CloseEvents(EVENT_NUM, hEvents);

  printf("InOut() - STOP\n");
}
///////////////////////////////////////////////////////////////
static BOOL WaitComReady(HANDLE hC0C, BOOL ignoreDSR, const BYTE *pAwakSeq)
{
  BOOL waitAwakSeq = (pAwakSeq && *pAwakSeq);
  BOOL waitDSR = (!ignoreDSR && !waitAwakSeq);

  if (!waitAwakSeq && !waitDSR)
    return TRUE;

  enum {
    EVENT_READ,
    EVENT_STAT,
    EVENT_NUM
  };

  HANDLE hEvents[EVENT_NUM];
  OVERLAPPED overlaps[EVENT_NUM];

  if (!PrepareEvents(EVENT_NUM, hEvents, overlaps))
    return FALSE;

  BOOL fault = FALSE;

  if (!SetCommMask(hC0C, EV_DSR)) {
    TraceLastError("WaitComReady(): SetCommMask()");
    fault = TRUE;
  }

  DWORD not_used;

  const BYTE *pAwakSeqNext = pAwakSeq;

  BYTE cbufRead[1];
  BOOL waitingRead = !waitAwakSeq;
  BOOL waitingStat = !waitDSR;

  while (!fault) {
    if (!waitingRead) {
      if (!pAwakSeqNext || !*pAwakSeqNext)
        break;

      if (!ReadFile(hC0C, cbufRead, sizeof(cbufRead), &not_used, &overlaps[EVENT_READ])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("WaitComReady(): ReadFile()");
          break;
        }
      }
      waitingRead = TRUE;
    }

    if (!waitingStat) {
      if (!WaitCommEvent(hC0C, &not_used, &overlaps[EVENT_STAT])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("WaitComReady(): WaitCommEvent()");
          fault = TRUE;
          break;
        }
      }
      waitingStat = TRUE;

      DWORD stat;

      if (!GetCommModemStatus(hC0C, &stat)) {
        TraceLastError("WaitComReady(): GetCommModemStatus()");
        fault = TRUE;
        break;
      }

      if (stat & MS_DSR_ON) {
        printf("DSR is ON\n");

        Sleep(1000);

        if (!GetCommModemStatus(hC0C, &stat)) {
          TraceLastError("WaitComReady(): GetCommModemStatus()");
          fault = TRUE;
          break;
        }

        if (stat & MS_DSR_ON)
          break;                // OK if DSR is still ON

        printf("DSR is OFF\n");
      }
    }

    if (waitingRead && waitingStat) {
      DWORD done;

      switch (WaitForMultipleObjects(EVENT_NUM, hEvents, FALSE, 5000)) {
      case WAIT_OBJECT_0 + EVENT_READ:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_READ], &done, FALSE)) {
          TraceLastError("WaitComReady(): GetOverlappedResult(EVENT_READ)");
          fault = TRUE;
        }
        ResetEvent(hEvents[EVENT_READ]);
        if (done && pAwakSeqNext) {
          if (*pAwakSeqNext == *cbufRead) {
            pAwakSeqNext++;
          } else {
            pAwakSeqNext = pAwakSeq;
            if (*pAwakSeqNext == *cbufRead)
              pAwakSeqNext++;
          }
          printf("Skipped character 0x%02.2X\n", (int)*cbufRead);
        }
        waitingRead = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_STAT:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_STAT], &not_used, FALSE)) {
          TraceLastError("WaitComReady(): GetOverlappedResult(EVENT_STAT)");
          fault = TRUE;
        }
        waitingStat = FALSE;
        break;
      case WAIT_TIMEOUT:
        break;
      default:
        TraceLastError("WaitComReady(): WaitForMultipleObjects()");
        fault = TRUE;
      }
    }
  }

  CancelIo(hC0C);

  CloseEvents(EVENT_NUM, hEvents);

  printf("WaitComReady() - %s\n", fault ? "FAIL" : "OK");

  return !fault;
}
///////////////////////////////////////////////////////////////
static HANDLE OpenC0C(const char *pPath, const ComParams &comParams)
{
  HANDLE hC0C = CreateFile(pPath,
                    GENERIC_READ|GENERIC_WRITE,
                    0,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED,
                    NULL);

  if (hC0C == INVALID_HANDLE_VALUE) {
    TraceLastError("OpenC0C(): CreateFile(\"%s\")", pPath);
    return INVALID_HANDLE_VALUE;
  }

  DCB dcb;

  if (!myGetCommState(hC0C, &dcb)) {
    CloseHandle(hC0C);
    return INVALID_HANDLE_VALUE;
  }

  if (comParams.BaudRate() > 0)
    dcb.BaudRate = (DWORD)comParams.BaudRate();

  if (comParams.ByteSize() > 0)
    dcb.ByteSize = (BYTE)comParams.ByteSize();

  if (comParams.Parity() >= 0)
    dcb.Parity = (BYTE)comParams.Parity();

  if (comParams.StopBits() >= 0)
    dcb.StopBits = (BYTE)comParams.StopBits();

  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDsrSensitivity = !comParams.IgnoreDSR();
  dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fOutX = FALSE;
  dcb.fInX = TRUE;
  dcb.XonChar = 0x11;
  dcb.XoffChar = 0x13;
  dcb.XonLim = 100;
  dcb.XoffLim = 100;
  dcb.fParity = FALSE;
  dcb.fNull = FALSE;
  dcb.fAbortOnError = FALSE;
  dcb.fErrorChar = FALSE;

  if (!mySetCommState(hC0C, &dcb)) {
    CloseHandle(hC0C);
    return INVALID_HANDLE_VALUE;
  }

  COMMTIMEOUTS timeouts;

  if (!GetCommTimeouts(hC0C, &timeouts)) {
    TraceLastError("OpenC0C(): GetCommTimeouts()");
    CloseHandle(hC0C);
    return INVALID_HANDLE_VALUE;
  }

  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = MAXDWORD - 1;
  timeouts.ReadIntervalTimeout = MAXDWORD;

  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 0;

  if (!SetCommTimeouts(hC0C, &timeouts)) {
    TraceLastError("OpenC0C(): SetCommTimeouts()");
    CloseHandle(hC0C);
    return INVALID_HANDLE_VALUE;
  }

  printf("OpenC0C(\"%s\", baud=%ld, data=%ld, parity=%s, stop=%s) - OK\n",
         pPath,
         (long)dcb.BaudRate,
         (long)dcb.ByteSize,
         ComParams::ParityStr(dcb.Parity),
         ComParams::StopBitsStr(dcb.StopBits));

  return hC0C;
}
///////////////////////////////////////////////////////////////
static const char *pProtoName = "tcp";

static BOOL SetAddr(struct sockaddr_in &sn, const char *pAddr, const char *pPort)
{
  memset(&sn, 0, sizeof(sn));
  sn.sin_family = AF_INET;

  if (pPort) {
    struct servent *pServEnt;

    pServEnt = getservbyname(pPort, pProtoName);

    sn.sin_port = pServEnt ? pServEnt->s_port : htons((u_short)atoi(pPort));
  }

  sn.sin_addr.s_addr = pAddr ? inet_addr(pAddr) : INADDR_ANY;

  if (sn.sin_addr.s_addr == INADDR_NONE) {
    const struct hostent *pHostEnt = gethostbyname(pAddr);

    if (!pHostEnt) {
      TraceLastError("SetAddr(): gethostbyname(\"%s\")", pAddr);
      return FALSE;
    }

    memcpy(&sn.sin_addr, pHostEnt->h_addr, pHostEnt->h_length);
  }
  return TRUE;
}

static SOCKET Socket(
    const char *pIF,
    const char *pPort = NULL)
{
  const struct protoent *pProtoEnt;

  pProtoEnt = getprotobyname(pProtoName);

  if (!pProtoEnt) {
    TraceLastError("Socket(): getprotobyname(\"%s\")", pProtoName);
    return INVALID_SOCKET;
  }

  SOCKET hSock = socket(AF_INET, SOCK_STREAM, pProtoEnt->p_proto);

  if (hSock == INVALID_SOCKET) {
    TraceLastError("Socket(): socket()");
    return INVALID_SOCKET;
  }

  if (pIF || pPort) {
    struct sockaddr_in sn;

    if (!SetAddr(sn, pIF, pPort))
      return INVALID_SOCKET;

    if (bind(hSock, (struct sockaddr *)&sn, sizeof(sn)) == SOCKET_ERROR) {
      TraceLastError("Socket(): bind(\"%s\", \"%s\")", pIF, pPort);
      closesocket(hSock);
      return INVALID_SOCKET;
    }
  }

  return hSock;
}

static void Disconnect(SOCKET hSock)
{
  if (shutdown(hSock, SD_BOTH) != 0)
    TraceLastError("Disconnect(): shutdown()");

  if (closesocket(hSock) != 0)
    TraceLastError("Disconnect(): closesocket()");

  printf("Disconnect() - OK\n");
}
///////////////////////////////////////////////////////////////

static SOCKET Accept(SOCKET hSockListen)
{
  struct sockaddr_in sn;
  int snlen = sizeof(sn);
  SOCKET hSock = accept(hSockListen, (struct sockaddr *)&sn, &snlen);

  if (hSock == INVALID_SOCKET) {
    TraceLastError("tcp2com(): accept()");
    return INVALID_SOCKET;
  }

  u_long addr = ntohl(sn.sin_addr.s_addr);

  printf("Accept(%d.%d.%d.%d) - OK\n",
      (addr >> 24) & 0xFF,
      (addr >> 16) & 0xFF,
      (addr >>  8) & 0xFF,
       addr        & 0xFF);

  return hSock;
}

static int tcp2com(
    const char *pPath,
    const ComParams &comParams,
    const char *pIF,
    const char *pPort,
    Protocol &protocol)
{
  SOCKET hSockListen = Socket(pIF, pPort);

  if (hSockListen == INVALID_SOCKET)
    return 2;

  if (listen(hSockListen, SOMAXCONN) == SOCKET_ERROR) {
    TraceLastError("tcp2com(): listen(\"%s\", \"%s\")", pIF, pPort);
    closesocket(hSockListen);
    return 2;
  }

  for (;;) {
    SOCKET hSock = Accept(hSockListen);

    if (hSock == INVALID_SOCKET)
      break;

    HANDLE hC0C = OpenC0C(pPath, comParams);

    if (hC0C != INVALID_HANDLE_VALUE) {
      InOut(hC0C, hSock, protocol, comParams.IgnoreDSR(), hSockListen);
      CloseHandle(hC0C);
    }

    Disconnect(hSock);
  }

  closesocket(hSockListen);

  return 2;
}
///////////////////////////////////////////////////////////////
static SOCKET Connect(
    const char *pIF,
    const char *pAddr,
    const char *pPort)
{
  struct sockaddr_in sn;

  if (!SetAddr(sn, pAddr, pPort))
    return INVALID_SOCKET;

  SOCKET hSock = Socket(pIF);

  if (hSock == INVALID_SOCKET)
    return INVALID_SOCKET;

  if (connect(hSock, (struct sockaddr *)&sn, sizeof(sn)) == SOCKET_ERROR) {
    TraceLastError("Connect(): connect(\"%s\", \"%s\")", pAddr, pPort);
    closesocket(hSock);
    return INVALID_SOCKET;
  }

  printf("Connect(\"%s\", \"%s\") - OK\n", pAddr, pPort);

  return hSock;
}

static int com2tcp(
    const char *pPath,
    const ComParams &comParams,
    const char *pIF,
    const char *pAddr,
    const char *pPort,
    Protocol &protocol,
    const BYTE *pAwakSeq)
{
  HANDLE hC0C = OpenC0C(pPath, comParams);

  if (hC0C == INVALID_HANDLE_VALUE) {
    return 2;
  }

  while (WaitComReady(hC0C, comParams.IgnoreDSR(), pAwakSeq)) {
    SOCKET hSock = Connect(pIF, pAddr, pPort);

    if (hSock == INVALID_SOCKET)
      break;

    InOut(hC0C, hSock, protocol, comParams.IgnoreDSR());

    Disconnect(hSock);
  }

  CloseHandle(hC0C);

  return 2;
}
///////////////////////////////////////////////////////////////
static void Usage(const char *pProgName)
{
  fprintf(stderr, "Usage (client mode):\n");
  fprintf(stderr, "    %s [options] \\\\.\\<com port> <host addr> <host port>\n", pProgName);
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage (server mode):\n");
  fprintf(stderr, "    %s [options] \\\\.\\<com port> <listen port>\n", pProgName);
  fprintf(stderr, "\n");
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, "    --telnet              - use Telnet protocol.\n");
  fprintf(stderr, "    --terminal <type>     - use terminal <type> (RFC 1091).\n");
  fprintf(stderr, "    --help                - show this help.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "COM port options:\n");
  fprintf(stderr, "    --baud <b>            - set baud rate to <b> (default is %ld),\n",
                                               (long)ComParams().BaudRate());
  fprintf(stderr, "                            where <b> is %s.\n",
                                               ComParams::BaudRateLst());
  fprintf(stderr, "    --data <d>            - set data bits to <d> (default is %ld), where <d> is\n",
                                               (long)ComParams().ByteSize());
  fprintf(stderr, "                            %s.\n",
                                               ComParams::ByteSizeLst());
  fprintf(stderr, "    --parity <p>          - set parity to <p> (default is %s), where <p> is\n",
                                               ComParams::ParityStr(ComParams().Parity()));
  fprintf(stderr, "                            %s.\n",
                                               ComParams::ParityLst());
  fprintf(stderr, "    --stop <s>            - set stop bits to <s> (default is %s), where <s> is\n",
                                               ComParams::StopBitsStr(ComParams().StopBits()));
  fprintf(stderr, "                            %s.\n",
                                               ComParams::StopBitsLst());
  fprintf(stderr, "    --ignore-dsr          - ignore DSR state (do not wait DSR to be ON before\n");
  fprintf(stderr, "                            connecting to host, do not close connection after\n");
  fprintf(stderr, "                            DSR is OFF and do not ignore any bytes received\n");
  fprintf(stderr, "                            while DSR is OFF).\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "    The value d[efault] above means to use current COM port settings.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Client mode options:\n");
  fprintf(stderr, "    --awak-seq <sequence> - wait for awakening <sequence> from com port\n"
                  "                            before connecting to host. All data before\n"
                  "                            <sequence> and <sequence> itself will not be sent.\n");
  fprintf(stderr, "    --interface <if>      - use interface <if> for connecting.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Server mode options:\n");
  fprintf(stderr, "    --interface <if>      - use interface <if> for listening.\n");
  exit(1);
}
///////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
  enum {prNone, prTelnet} protocol = prNone;
  const char *pTermType = NULL;
  const BYTE *pAwakSeq = NULL;
  const char *pIF = NULL;
  char **pArgs = &argv[1];
  ComParams comParams;

  while (argc > 1) {
    if (**pArgs != '-')
      break;

    if (!strcmp(*pArgs, "--help")) {
      Usage(argv[0]);
    } else
    if (!strcmp(*pArgs, "--telnet")) {
      protocol = prTelnet;
      pArgs++;
      argc--;
    } else
    if (!strcmp(*pArgs, "--ignore-dsr")) {
      pArgs++;
      argc--;
      comParams.SetIgnoreDSR(TRUE);
    } else
    if (argc < 3) {
      Usage(argv[0]);
    } else
    if (!strcmp(*pArgs, "--terminal")) {
      pArgs++;
      argc--;
      pTermType = *pArgs;
      pArgs++;
      argc--;
    } else
    if (!strcmp(*pArgs, "--baud")) {
      pArgs++;
      argc--;
      comParams.SetBaudRate(*pArgs);
      pArgs++;
      argc--;
    } else
    if (!strcmp(*pArgs, "--data")) {
      pArgs++;
      argc--;
      comParams.SetByteSize(*pArgs);
      pArgs++;
      argc--;
    } else
    if (!strcmp(*pArgs, "--parity")) {
      pArgs++;
      argc--;
      if (!comParams.SetParity(*pArgs)) {
        fprintf(stderr, "Unknown parity value %s\n", *pArgs);
        exit(1);
      }
      pArgs++;
      argc--;
    } else
    if (!strcmp(*pArgs, "--stop")) {
      pArgs++;
      argc--;
      if (!comParams.SetStopBits(*pArgs)) {
        fprintf(stderr, "Unknown stop bits value %s\n", *pArgs);
        exit(1);
      }
      pArgs++;
      argc--;
    } else
    if (!strcmp(*pArgs, "--awak-seq")) {
      pArgs++;
      argc--;
      pAwakSeq = (const BYTE *)*pArgs;
      pArgs++;
      argc--;
    } else
    if (!strcmp(*pArgs, "--interface")) {
      pArgs++;
      argc--;
      pIF = *pArgs;
      pArgs++;
      argc--;
    } else {
      fprintf(stderr, "Unknown option %s\n", *pArgs);
      exit(1);
    }
  }

  if (argc < 3 || argc > 4)
    Usage(argv[0]);

  WSADATA wsaData;

  WSAStartup(MAKEWORD(1, 1), &wsaData);

  Protocol *pProtocol;

  switch (protocol) {
  case prTelnet:
    pProtocol = new TelnetProtocol(10, 10);
    ((TelnetProtocol *)pProtocol)->SetTerminalType(pTermType);
    break;
  default:
    pProtocol = new Protocol(10, 10);
  };

  int res;

  if (argc == 4)
    res = com2tcp(pArgs[0], comParams, pIF, pArgs[1], pArgs[2], *pProtocol, pAwakSeq);
  else
    res = tcp2com(pArgs[0], comParams, pIF, pArgs[1], *pProtocol);

  delete pProtocol;

  WSACleanup();
  return res;
}
///////////////////////////////////////////////////////////////
