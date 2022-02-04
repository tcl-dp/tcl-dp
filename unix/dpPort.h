/*
 * unix/dpPort.h --
 *
 *	This file is included by all of the Dp C files.  It contains
 *	information that may be configuration-dependent, such as
 *	#includes for system include files and a few other things.
 *
 * Copyright (c) 1995-1996 The Regents of Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _DPUNIXPORT
#define _DPUNIXPORT

#include <stdlib.h>
#include <errno.h>

#ifndef DP_HAS_MULTICAST
#    include <netinet/in.h>
#endif

#define NONBLOCKING(flags)	(flags & O_NONBLOCK)
#define DP_SOCKET		TCL_UNIX_FD
#define SERIAL_HANDLE		TCL_UNIX_FD
#define DP_SOCKET_ERROR		(-1)
#define DP_INADDR_NONE		(-1)
#define ASYNC_CONNECT_ERROR	EINPROGRESS

/*
 * The following are abstract data types for sockets and internet addresses
 */
typedef int			DpSocket;
typedef struct sockaddr		DpSocketAddress;
typedef struct sockaddr_in	DpSocketAddressIP;
typedef int			SerialHandle;

#ifndef _TCL76

typedef ClientData FileHandle;

int SockGetFile 	_ANSI_ARGS_((ClientData instanceData, int direction,
					FileHandle *handlePtr));
int SockClose		_ANSI_ARGS_((ClientData instanceData,
					Tcl_Interp *interp));
int SockBlockMode	_ANSI_ARGS_((ClientData instanceData, int mode));
void SockWatch		_ANSI_ARGS_((ClientData instanceData, int mask));
int UdpIpmOutput	_ANSI_ARGS_((ClientData instanceData, CONST84 char *buf,
					int toWrite, int *errorCodePtr));

#endif

/*
 * The flag for setting non-blocking I/O varies a bit from Unix to Unix
 */
#ifdef HPUX
#  define NBIO_FLAG O_NONBLOCK
#else
#  define NBIO_FLAG O_NDELAY
#endif

#define DP_INADDR_ANY		INADDR_ANY

/*
 * Serial Port stuff
 */

#if !defined(PARITY_NONE)
#  define PARITY_NONE		0
#  define PARITY_ODD		1
#  define PARITY_EVEN		2
#endif

#endif /* _DPUNIXPORT */



