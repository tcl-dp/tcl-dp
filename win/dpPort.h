/*
 * win/dpPort.h --
 *
 *	This file is included by all of the DP C files.  It contains
 *	information that are specific for the MS Windows environment,
 *	such as header files and a few other things.
 *
 * Copyright (c) 1995-1996 The Regents of Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _DPWINPORT
#define _DPWINPORT

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*
 * VC++ has an alternate entry point called DllMain, so we need to rename
 * our entry point.
 */

#   if defined(_MSC_VER)
#	define EXPORT(a,b) __declspec(dllexport) a b
#	define DllEntryPoint DllMain
#   else
#	if defined(__BORLANDC__)
#	    define EXPORT(a,b) a _export b
#	else
#	    define EXPORT(a,b) a b
#	endif
#   endif

#ifndef _WINSOCKAPI_
#    include <winsock.h>
#endif

#define NONBLOCKING(flags)	(flags & O_NONBLOCK)
#define DP_INVALID_SOCKET	INVALID_SOCKET
#define DP_SOCKET		TCL_WIN_SOCKET
#define SERIAL_HANDLE		TCL_WIN_FILE
#define DP_SOCKET_ERROR		SOCKET_ERROR
#define DP_INADDR_NONE		INADDR_NONE
#define ASYNC_CONNECT_ERROR	EWOULDBLOCK
#define DP_INADDR_ANY		INADDR_ANY


#if (TCL_MAJOR_VERSION > 7)

typedef ClientData FileHandle;

typedef struct SocketInfo {
    Tcl_Channel channel;	   /* Channel associated with this socket. */
    SOCKET socket;		   /* Windows SOCKET handle. */
    int flags;			   /* Bit field comprised of the flags*/
    int watchEvents;		   /* OR'ed combination of FD_READ, ,*/
    int selectEvents;		   /* OR'ed combination of FD_READ, ,*/
    int readyEvents;		   /* OR'ed combination of FD_READ, ,*/
    int lastError;		   /* Error code from last message. */
    struct SocketInfo *nextPtr;	   /* The next socket on the global socket*/
} SocketInfo;

typedef struct SocketEvent {
    Tcl_Event header;		/* Information that is standard for */
    SOCKET socket;		/* Socket descriptor that is ready.  Use*/
} SocketEvent;

EXTERN HWND hwnd;
EXTERN int initd;
EXTERN SocketInfo *dpSocketList;

#define SOCKET_MESSAGE		WM_USER+1
#define SOCKET_ASYNC		(1<<0)	/* The socket is in blocking mode. */
#define SOCKET_EOF		(1<<1)	/* A zero read happened on
					 * the socket. */
#define SOCKET_ASYNC_CONNECT	(1<<2)	/* This socket uses async connect. */
#define SOCKET_PENDING		(1<<3)	/* A message has been sent
					 * for this socket */
int SocketEventProc(Tcl_Event *evPtr, int flags);
void SocketExitHandler(ClientData clientData);
void InitDpSockets(void);
void SocketSetupProc(ClientData data, int flags);
void SocketCheckProc(ClientData data, int flags);
SocketInfo *NewSocketInfo(SOCKET socket);
LRESULT CALLBACK SocketProc(HWND hwnd, UINT message, WPARAM wParam, 
				LPARAM lParam);

int SockGetFile(ClientData instanceData, int direction, 
		FileHandle *handlePtr);
int SockClose(ClientData instanceData, Tcl_Interp *interp);
int SockBlockMode(ClientData instanceData, int mode);
void SockWatch(ClientData instanceData, int mask);
int UdpIpmOutput(ClientData instanceData, char *buf, int toWrite, 
		int *errorCodePtr);

#endif


/*
 * The following are abstract types for socket handles and IP addresses
 */
typedef SOCKET			DpSocket;
typedef struct sockaddr		DpSocketAddress;
typedef struct sockaddr_in	DpSocketAddressIP;
typedef HANDLE			SerialHandle;

/*
 * The following structure is initialized by WSAStartup in DppInit().
 * It contains general information about the winsock library and
 * underlying network.  It contains the following elements:
 *
 *  WORD             wVersion		The version number that we're using
 *					(two bytes)
 *  WORD             wHighVersion	The highest version # supported by dll
 *  char             szDescription[]	A description of the Windows Sockets
 *					implementation, including vendor
 *					identification. Can contain
 *					any characters.
 *  char             szSystemStatus[]	Relevant status or configuration
 *					information.
 *  unsigned short   iMaxSockets	The maximum number of sockets
 *					which a single process can potentially
 *					open.
 *  unsigned short   iMaxUdpDg		The size in bytes of the largest UDP
 *					datagram that can be sent or received
 *					by a Windows Sockets application.
 *					If the implementation imposes no
 *					limit, iMaxUdpDg is zero.
 *  char FAR *       lpVendorInfo	A pointer to a vendor-specific data
 *					structure. See Programming with
 *					Sockets in the Win32 SDK documentation.
 */
WSADATA dpStartUpInfo; 

/*
 * Serial Port stuff
 */

/*
 * Type of parity - these constants may also be
 * defined in winbase.h so if they are already defined
 * we don't bother....
 */

#if !defined(PARITY_NONE)
#  define PARITY_NONE		0
#  define PARITY_ODD		1
#  define PARITY_EVEN		2
#endif

#endif /* _DPWINPORT */


