/*
 * $Id: utils.cxx,v 1.4 2006/11/16 12:51:43 vfrolov Exp $
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
 * $Log: utils.cxx,v $
 * Revision 1.4  2006/11/16 12:51:43  vfrolov
 * Added ability to set COM port parameters
 *
 * Revision 1.3  2005/10/03 13:44:17  vfrolov
 * Added Clean() method
 *
 * Revision 1.2  2005/06/10 15:51:26  vfrolov
 * Included precomp.h
 *
 * Revision 1.1  2005/06/06 15:19:02  vfrolov
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
int ChunkStream::write(const void *pBuf, int count)
{
  int len = sizeof(data) - last;

  if (!len)
    return -1;

  if (len > count)
    len = count;

  memcpy(data + last, pBuf, len);
  last += len;

  return len;
}

int ChunkStream::read(void *pBuf, int count)
{
  if (sizeof(data) == first)
    return -1;

  int len = last - first;

  if (len > count)
    len = count;

  memcpy(pBuf, data + first, len);
  first += len;

  return len;
}
///////////////////////////////////////////////////////////////
void ChunkStreamQ::toQueue(ChunkStream *pChunk)
{
  if (!pChunk)
    return;

  if (pLast) {
    pChunk->pNext = pLast->pNext;
    pLast->pNext = pChunk;
  } else {
    pChunk->pNext = NULL;
    pFirst = pChunk;
  }
  pLast = pChunk;
}

ChunkStream *ChunkStreamQ::fromQueue()
{
  ChunkStream *pChunk = pFirst;

  if (pChunk) {
    pFirst = pChunk->pNext;
    if (!pFirst)
      pLast = NULL;
  }

  return pChunk;
}
///////////////////////////////////////////////////////////////
int DataStream::PutData(const void *_pBuf, int count)
{
  if (eof)
    return -1;

  int done = 0;
  const BYTE *pBuf = (const BYTE *)_pBuf;

  while (count) {
    if (!bufQ.last())
      bufQ.toQueue(new ChunkStream());

    int len = bufQ.last()->write(pBuf, count);

    if (len < 0) {
      bufQ.toQueue(new ChunkStream());
      continue;
    } else {
      pBuf += len;
      count -= len;
      done += len;
    }
  }

  busy += done;

  return done;
}

int DataStream::GetData(void *_pBuf, int count)
{
  if (!busy) {
    if (eof)
      return -1;
    else
      return 0;
  }

  int done = 0;
  BYTE *pBuf = (BYTE *)_pBuf;

  while (count && bufQ.first()) {
    int len = bufQ.first()->read(pBuf, count);

    if (len < 0) {
      delete bufQ.fromQueue();
      continue;
    } else {
      if (!len)
        break;
      pBuf += len;
      count -= len;
      done += len;
    }
  }

  busy -= done;

  return done;
}

void DataStream::Clean()
{
  while (bufQ.first())
    delete bufQ.fromQueue();

  busy = 0;
  eof = FALSE;
}
///////////////////////////////////////////////////////////////
int Protocol::Send(const void *pBuf, int count)
{
  return SendRaw(pBuf, count);
}

int Protocol::Write(const void *pBuf, int count)
{
  return WriteRaw(pBuf, count);
}

void Protocol::Clean()
{
  streamSendRead.Clean();
  streamWriteRecv.Clean();
}
///////////////////////////////////////////////////////////////
ComParams::ComParams()
  : baudRate(CBR_19200),
    byteSize(8),
    parity(NOPARITY),
    stopBits(ONESTOPBIT),
    ignoreDSR(FALSE)
{
}

BOOL ComParams::SetParity(const char *pParity)
{
  switch (*pParity) {
    case 'n': parity = NOPARITY; break;
    case 'o': parity = ODDPARITY; break;
    case 'e': parity = EVENPARITY; break;
    case 'm': parity = MARKPARITY; break;
    case 's': parity = SPACEPARITY; break;
    case 'd': parity = -1; break;
    default : return FALSE;
  }
  return TRUE;
}

BOOL ComParams::SetStopBits(const char *pStopBits)
{
  switch (*pStopBits) {
    case '1':
      if ((pStopBits[1] == '.' || pStopBits[1] == ',') && pStopBits[2] == '5')
        stopBits = ONE5STOPBITS;
      else
        stopBits = ONESTOPBIT;
      break;
    case '2': stopBits = TWOSTOPBITS; break;
    case 'd': stopBits = -1; break;
    default : return FALSE;
  }
  return TRUE;
}

const char *ComParams::ParityStr(int parity)
{
  switch (parity) {
    case NOPARITY: return "no";
    case ODDPARITY: return "odd";
    case EVENPARITY: return "even";
    case MARKPARITY: return "mark";
    case SPACEPARITY: return "space";
    case -1: return "default";
  }
  return "?";
}

const char *ComParams::StopBitsStr(int stopBits)
{
  switch (stopBits) {
    case ONESTOPBIT: return "1";
    case ONE5STOPBITS: return "1.5";
    case TWOSTOPBITS: return "2";
    case -1: return "default";
  }
  return "?";
}

const char *ComParams::BaudRateLst()
{
  return "positive number or d[efault]";
}

const char *ComParams::ByteSizeLst()
{
  return "positive number or d[efault]";
}

const char *ComParams::ParityLst()
{
  return "n[o], o[dd], e[ven], m[ark], s[pace] or d[efault]";
}

const char *ComParams::StopBitsLst()
{
  return "1, 1.5, 2 or d[efault]";
}
///////////////////////////////////////////////////////////////
