/*
 * $Id: telnet.cpp,v 1.5 2007/02/08 11:52:11 vfrolov Exp $
 *
 * Copyright (c) 2005-2007 Vyacheslav Frolov
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
 * $Log: telnet.cpp,v $
 * Revision 1.5  2007/02/08 11:52:11  vfrolov
 * Added missing IAC escaping
 *
 * Revision 1.4  2006/11/13 10:37:14  vfrolov
 * Fixed type casting
 *
 * Revision 1.3  2005/10/03 13:44:17  vfrolov
 * Added Clean() method
 *
 * Revision 1.2  2005/06/10 15:55:10  vfrolov
 * Implemented --terminal option
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
enum {
  cdSE   = 240,
  cdSB   = 250,
  cdWILL = 251,
  cdWONT = 252,
  cdDO   = 253,
  cdDONT = 254,
  cdIAC  = 255,
};

static const char *code2name(unsigned code)
{
  switch (code) {
    case cdSE:
      return "SE";
      break;
    case cdSB:
      return "SB";
      break;
    case cdWILL:
      return "WILL";
      break;
    case cdWONT:
      return "WONT";
      break;
    case cdDO:
      return "DO";
      break;
    case cdDONT:
      return "DONT";
      break;
  }
  return "UNKNOWN";
}
///////////////////////////////////////////////////////////////
enum {
  opEcho            = 1,
  opTerminalType    = 24,
};
///////////////////////////////////////////////////////////////
enum {
  stData,
  stCode,
  stOption,
  stSubParams,
  stSubCode,
};
///////////////////////////////////////////////////////////////
TelnetProtocol::TelnetProtocol(int _thresholdSend, int _thresholdWrite)
  : Protocol(_thresholdSend, _thresholdWrite)
{
  SetTerminalType(NULL);
  Clean();
}

void TelnetProtocol::SetTerminalType(const char *pTerminalType)
{
  terminalType.clear();

  if (!pTerminalType)
    pTerminalType = "UNKNOWN";

  while (*pTerminalType)
    terminalType.push_back(*pTerminalType++);
}

void TelnetProtocol::Clean()
{
  state = stData;

  for(int i = 0 ; i < sizeof(options)/sizeof(options[0]) ; i++) {
    options[i].remoteOptionState = OptionState::osCant;
    options[i].localOptionState = OptionState::osCant;
  }

  options[opEcho].remoteOptionState = OptionState::osNo;
  options[opTerminalType].localOptionState = OptionState::osNo;

  Protocol::Clean();
}

int TelnetProtocol::Send(const void *pBuf, int count)
{
  for (int i = 0 ; i < count ; i++) {
    BYTE ch = ((const BYTE *)pBuf)[i];

    if (ch == cdIAC)
          SendRaw(&ch, 1);

    SendRaw(&ch, 1);
  }

  return count;
}

int TelnetProtocol::Write(const void *pBuf, int count)
{
  for (int i = 0 ; i < count ; i++) {
    BYTE ch = ((const BYTE *)pBuf)[i];

    switch (state) {
      case stData:
        if (ch == cdIAC)
          state = stCode;
        else
          WriteRaw(&ch, 1);
        break;
      case stCode:
        switch (ch) {
          case cdIAC:
            WriteRaw(&ch, 1);
            state = stData;
            break;
          case cdSB:
          case cdWILL:
          case cdWONT:
          case cdDO:
          case cdDONT:
            code = ch;
            state = stOption;
            break;
          default:
            printf("RECV: unknown code %u\n", (unsigned)ch);
            state = stData;
        }
        break;
      case stOption:
        printf("RECV: %s %u\n", code2name(code), (unsigned)ch);
        switch (code) {
          case cdSB:
            option = ch;
            params.clear();
            state = stSubParams;
            break;
          case cdWILL:
            switch (options[ch].remoteOptionState) {
              case OptionState::osCant:
                SendOption(cdDONT, ch);
                break;
              case OptionState::osNo:
                options[ch].remoteOptionState = OptionState::osYes;
                SendOption(cdDO, ch);
                break;
              case OptionState::osYes:
                break;
            }
            break;
          case cdWONT:
            switch (options[ch].remoteOptionState) {
              case OptionState::osCant:
              case OptionState::osNo:
                break;
              case OptionState::osYes:
                options[ch].remoteOptionState = OptionState::osNo;
                SendOption(cdDONT, ch);
                break;
            }
            break;
          case cdDO:
            switch (options[ch].localOptionState) {
              case OptionState::osCant:
                SendOption(cdWONT, ch);
                break;
              case OptionState::osNo:
                options[ch].localOptionState = OptionState::osYes;
                SendOption(cdWILL, ch);
                break;
              case OptionState::osYes:
                break;
            }
            break;
          case cdDONT:
            switch (options[ch].localOptionState) {
              case OptionState::osCant:
              case OptionState::osNo:
                break;
              case OptionState::osYes:
                options[ch].localOptionState = OptionState::osNo;
                SendOption(cdWONT, ch);
                break;
            }
            break;
          default:
            printf("  ignored\n");
        };
        if (state == stOption)
          state = stData;
        break;
      case stSubParams:
        if (ch == cdIAC)
          state = stSubCode;
        else
          params.push_back(ch);
        break;
      case stSubCode:
        switch (ch) {
          case cdIAC:
            state = stSubParams;
            break;
          case cdSE:
            printf("  ");
            {
              for (BYTE_vector::const_iterator i = params.begin() ; i != params.end() ; i++)
                printf("%u ", (unsigned)*i);
            }
            printf("SE\n");

            switch (option) {
              case opTerminalType:
                params.clear();
                params.push_back(0);
                params.insert(params.end(), terminalType.begin(), terminalType.end());
                SendSubNegotiation(option, params);
                break;
              default:
                printf("  ignored\n");
            }

            state = stData;
            break;
          default:
            printf("RECV: unknown sub code %u\n", (unsigned)ch);
            state = stData;
        };
        break;
    }
  }

  return count;
}

void TelnetProtocol::SendOption(BYTE code, BYTE option)
{
  BYTE buf[3] = {cdIAC, code, option};

  printf("SEND: %s %u\n", code2name(code), (unsigned)option);

  SendRaw(buf, sizeof(buf));
}

void TelnetProtocol::SendSubNegotiation(int option, const BYTE_vector &params)
{
  SendOption(cdSB, (BYTE)option);

  printf("  ");
  for (BYTE_vector::const_iterator i = params.begin() ; i != params.end() ; i++) {
    BYTE b = *i;

    printf("%u ", (unsigned)b);
    SendRaw(&b, sizeof(b));
  }
  printf("SE\n");

  BYTE buf[2] = {cdIAC, cdSE};

  SendRaw(buf, sizeof(buf));
}
///////////////////////////////////////////////////////////////
