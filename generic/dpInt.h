/*
 * generic/dpInt.h --
 *
 *	Declarations for things used internally by Dp
 *	procedures but not exported outside the module.
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _DPINT
#define _DPINT


#ifdef SOCKDEBUG
#define SDBG(a)		printf("0x%X - ", getpid()); printf a
#else
#define SDBG(a)
#endif


#include "generic/dp.h"
#ifndef _DPPORT
#include "generic/dpPort.h"
#endif

#ifndef max
#  define max(a, b)  (((a) > (b)) ? (a) : (b))
#endif

/*
 *----------------------------------------------------------------------
 * Constant definitions
 *----------------------------------------------------------------------
 */

#define SOCKET_IPM		(1<<31)

/*
 * The following are used by the various SetSocketOption and
 * GetSocketOption functions
 */

/*
 * UDP/TCP/Generic
 */
#define DP_SEND_BUFFER_SIZE	 1
#define DP_RECV_BUFFER_SIZE	 2
#define DP_BLOCK		 3
#define DP_REUSEADDR		 4
#define DP_PEEK			 6
#define DP_HOST			 7
#define DP_PORT			 8
#define DP_MYPORT		 9
#define DP_KEEP_ALIVE		10
#define DP_LINGER		11
#define DP_REMOTEPORT		12
#define DP_MYIPADDR		13
#define DP_REMOTEIPADDR		14

#define DP_GROUP		20
#define DP_MULTICAST_TTL	21
#define DP_MULTICAST_LOOP	22
#define DP_ADD_MEMBERSHIP	23
#define DP_DROP_MEMBERSHIP	24
#define DP_BROADCAST		25

/*
 * Serial port options
 */
#define DP_STOPBITS             101
#define DP_CHARSIZE             102
#define DP_BAUDRATE             103
#define DP_PARITY               104
#define DP_DEVICENAME		105

/*
 * Email channel
 */
#define DP_ADDRESS		205
#define DP_IDENTIFIER		206
#define DP_SEQUENCE		207

/*
 * Identity filter and other filters
 */
#define DP_CHANNEL		210
#define DP_INFILTER		211
#define DP_OUTFILTER		212
#define DP_INSET                213
#define DP_OUTSET               214

#if ( TCL_MAJOR_VERSION < 8 )
typedef Tcl_File FileHandle;
#endif

#if ( TCL_MAJOR_VERSION < 8 ) || !defined(_WIN32)
typedef struct SocketInfo {int dummy;} SocketInfo;
#endif
/*
 * A collection of all the data necessary for
 * all the different types of sockets.  We buy
 * reusability at the cost of size.
 */
typedef struct SocketState {
    Tcl_Interp *	interp;
    DpSocket 		sock;
    FileHandle		sockFile;
    Tcl_Channel		channel;
    Tcl_DString		groupList;	/* IPM */
    DpSocketAddressIP	sockaddr;	/* UDP/IPM */
    SocketInfo *	sockInfo;
    int 		flags;
    int			myIpAddr;
    int			myPort;
    int			recvBufSize;	/* UDP/IPM */
    int			groupAddr;	/* IPM */
    int 		groupPort;	/* IPM */
    int			destIpAddr;	/* TCP */
    int			destPort;	/* TCP */
} SocketState;



typedef struct SerialState {
    SerialHandle 	fd;
    FileHandle 		theFile;
    char 		deviceName[20];
    Tcl_Channel		channel;
} SerialState;

/*
 *----------------------------------------------------------------------
 * Internal procedures shared among DP modules but not exported
 * to the outside world:
 *----------------------------------------------------------------------
 */

/*
 * Library routines that aren't part of any particular protocol (or support
 * multiple protocols)
 */

EXTERN int              DpTranslateOption _ANSI_ARGS_((CONST84 char *optionName));
EXTERN int              DpHostToIpAddr _ANSI_ARGS_((CONST84 char *hostname,
				int *ipAddrPtr));
EXTERN int              DpIpAddrToHost _ANSI_ARGS_((int ipAddr,
				char *hostPtr));

EXTERN int		DppCloseSocket _ANSI_ARGS_((DpSocket sock));
EXTERN int		DppSetBlock _ANSI_ARGS_((DpSocket sock, int block));
EXTERN int		DppGetErrno _ANSI_ARGS_(());
EXTERN int		DppInit _ANSI_ARGS_((Tcl_Interp *interp));

/*
 *----------------------------------------------------------------------
 * Externally visible procedures that form the C API:
 *----------------------------------------------------------------------
 */

EXTERN int		DpInitChannels _ANSI_ARGS_((Tcl_Interp * interp));
EXTERN int		DpInitPlugIn   _ANSI_ARGS_((Tcl_Interp * interp));
EXTERN int		DpRPCInit _ANSI_ARGS_((Tcl_Interp * interp));

EXTERN Tcl_Channel	DpCreateEmailChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    int argc, CONST84 char **argv));
EXTERN Tcl_Channel	DpCreateIdChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    int argc, CONST84 char **argv));
EXTERN Tcl_Channel	DpCreatePlugFChannel _ANSI_ARGS_((Tcl_Interp *interp,
   			    int argc, CONST84 char **argv));
EXTERN Tcl_Channel	DpCreatePOChannel _ANSI_ARGS_((Tcl_Interp *interp,
   			    int argc, CONST84 char **argv));
EXTERN Tcl_Channel	DpOpenUdpChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    int argc, CONST84 char **argv));
EXTERN Tcl_Channel	DpOpenSerialChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    int argc, CONST84 char **argv));
EXTERN Tcl_Channel	DpOpenIpmChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    int argc, CONST84 char **argv));
EXTERN Tcl_Channel	DpOpenTcpChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    int argc, CONST84 char **argv));
EXTERN Tcl_Channel      Dp_TcpAccept _ANSI_ARGS_((Tcl_Interp *interp,
                            CONST84 char *channelId));

/*
 *----------------------------------------------------------------------
 * Command procedures in the generic core:
 *----------------------------------------------------------------------
 */

EXTERN int	Dp_AcceptCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_ConnectCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_CopyCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_FromCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_NetInfoCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_RDOCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_RPCCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_AdminCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int     	Dp_CancelRPCCmd _ANSI_ARGS_((ClientData clientData,
	            Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_SendCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));
EXTERN int	Dp_RecvCmd _ANSI_ARGS_((ClientData clientData,
		    Tcl_Interp *interp, int argc, CONST84 char **argv));

/*
 * Plug-in filters.
 */

extern Dp_PlugInFilterProc Identity;
extern Dp_PlugInFilterProc Plug1to2;
extern Dp_PlugInFilterProc Plug2to1;
extern Dp_PlugInFilterProc Xor;
extern Dp_PlugInFilterProc PackOn;
extern Dp_PlugInFilterProc Uuencode;
extern Dp_PlugInFilterProc Uudecode;
extern Dp_PlugInFilterProc TclFilter;
extern Dp_PlugInFilterProc HexOut;
extern Dp_PlugInFilterProc HexIn;


/*
 * Locking functions used in implementing email channels.
 */

extern int	PutLock	_ANSI_ARGS_((char *lockFilePath));
extern int	RemoveLock _ANSI_ARGS_((char *lockFilePath));

#endif /* _DPINT */



