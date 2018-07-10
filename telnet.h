/*
 * $Id: telnet.h,v 1.4 2007/02/08 11:52:11 vfrolov Exp $
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
 * $Log: telnet.h,v $
 * Revision 1.4  2007/02/08 11:52:11  vfrolov
 * Added missing IAC escaping
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
 *
 */

#ifndef _TELNET_H
#define _TELNET_H

///////////////////////////////////////////////////////////////
class TelnetProtocol : public Protocol
{
  public:
    TelnetProtocol(int _thresholdSend = 0, int _thresholdWrite = 0);
    void SetTerminalType(const char *pTerminalType);

    virtual int Write(const void *pBuf, int count);
    virtual int Send(const void *pBuf, int count);
    virtual void Clean();
  protected:
    void SendOption(BYTE code, BYTE option);
    void SendSubNegotiation(int option, const BYTE_vector &params);

    int state;
    int code;
    int option;
    BYTE_vector params;

    struct OptionState
    {
      enum {osCant, osNo, osYes};
      int localOptionState  : 2;
      int remoteOptionState : 2;
    };

    OptionState options[256];
    BYTE_vector terminalType;
};
///////////////////////////////////////////////////////////////

#endif  // _TELNET_H
