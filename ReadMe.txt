               ====================================
               COM port to TCP redirector (com2tcp)
               ====================================

INTRODUCTION
============

The COM port to TCP redirector is a Windows application and is a part
of the com0com project.

In conjunction with the Null-modem emulator (com0com) the com2tcp
enables to use a COM port based applications to communicate with the
TCP/IP based applications. It also allows communication with a remote
serial port via the TCP/IP.

The homepage for com0com project is http://com0com.sourceforge.net/.

BUILDING
========

Start Microsoft Visual C++ 2005 with hub4com.vcproj file.
Set Active Configuration to "com2tcp - Win32 Release".
Build com2tcp.exe.

EXAMPLE OF USAGE
================

You have old TERM95.EXE application from the Norton Commander 5.0 for
MS-DOS and you'd like to use it to communicate with your.telnet.server
telnet server. You can do so this way:

  1. With the com0com driver create a virtual COM port pair with
     port names COM2 and CNCB0 (see com0com's ReadMe.txt for details).

  2. Start the com2tcp.exe on CNCB0 port. For example:

       com2tcp --telnet \\.\CNCB0 your.telnet.server telnet

  3. Start the TERM95.EXE on COM2 port.
