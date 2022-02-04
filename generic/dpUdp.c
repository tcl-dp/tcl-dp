/*
 * generic/dpUdp.c --
 *
 *  This file implements the generic code for a udp channel driver.  These
 *  are channels that are created by evaluating "dp_connect udp".
 *
 *  The architecture consists of a generic layer and a platform specific
 *  layer.  The rational is that platform specific code goes in its layer,
 *  while platform independent code goes in its layer.  However, most
 *  socket implementations use the Berkeley sockets interface, which is
 *  similar across platforms with a few annoying differences.  These are
 *  separated into two files, dpSockUdp.c contains the generic socket code,
 *  which makes calls on routines in win/dpSock.c, which contains the
 *  platform specific code.  We retain the two level architecture, though,
 *  so non-Berkeley socket interfaces can be built (if any still exist).
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "generic/dpInt.h"
#include "generic/dpPort.h"

/*
 * Below are all the channel driver procedures that must be supplied for
 * a channel.  Replace Udp with the name of this channel type.
 * In many cases, the DpGeneric driver procedures can be used (e.g.,
 * "DpGenericBlockMode)"
 */

static int		    UdpBlockMode _ANSI_ARGS_((ClientData instanceData,
					int mode));
static int		    UdpInput _ANSI_ARGS_((ClientData instanceData,
					char *buf, int bufSize,
					int *errorCodePtr));
static int		    UdpOutput _ANSI_ARGS_((ClientData instanceData,
					CONST84 char *buf, int toWrite,
					int *errorCodePtr));
static int		    UdpClose _ANSI_ARGS_((ClientData instanceData,
					Tcl_Interp *interp));
static int		    UdpSetOption _ANSI_ARGS_((ClientData instanceData,
					Tcl_Interp *interp, CONST char *optionName,
					CONST char *optionValue));
static int		    UdpGetOption _ANSI_ARGS_((ClientData instanceData,
					CONST84 char *optionName, Tcl_DString *dsPtr));

static void		    UdpWatch _ANSI_ARGS_((ClientData instanceData,
					int mask));
static int		    UdpReady _ANSI_ARGS_((ClientData instanceData,
					int mask));

static Tcl_File	    UdpGetFile _ANSI_ARGS_((ClientData instanceData,
					int direction));

typedef SocketState UdpState;

static Tcl_ChannelType udpChannelType = {
     "udp",		/* Name of channel */
     DP_CHANNEL_VERSION,	/* TCL_CHANNEL_VERSION_1, TCL_CHANNEL_VERSION_2, and so on */
     UdpClose,		/* Proc to close a socket */
     UdpInput,		/* Proc to get input from a socket */
     UdpOutput,		/* Proc to send output to a socket */
     NULL,              /* Can't seek on a socket! */
     UdpSetOption,	/* Proc to set a socket option */
     UdpGetOption,	/* Proc to set a socket option */
     UdpWatch,		/* Proc called to set event loop wait params */
     UdpGetFile,		/* Proc to return a handle assoc with socket */
     NULL,			/* Proc to call to close the channel if the device
					 * supports closing the read & write sides */
     UdpBlockMode,	/* Proc to set blocking mode on socket */
     /* Only valid in TCL_CHANNEL_VERSION_2 channels or later */
     NULL,			/* Proc to call to flush a channel */
     NULL,			/* Proc to call to handle a channel event */
     /* Only valid in TCL_CHANNEL_VERSION_3 channels or later */
     NULL,			/* Proc to call to seek on the channel which can handle 64-bit offsets */
     /* Only valid in TCL_CHANNEL_VERSION_4 channels or later */
     NULL			/* Proc to notify the driver of thread specific activity for a channel */
};

#define	PEEK_MODE	(1<<1)	/* Read without consuming? */

static int udpCount = 0;        /* Number of udp files opened -- used to
                                 * generate unique ids for channels */


/*
 *--------------------------------------------------------------
 *
 * UdpBlockMode --
 *
 *	Sets the udp socket to blocking or non-blocking.  Just
 *	a wrapper around the platform specific function.
 *
 * Results:
 *	Zero if the operation was successful, or a nonzero POSIX
 *	error code if the operation failed.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
static int
UdpBlockMode (instanceData, mode)
    ClientData instanceData;	/* Pointer to udpState struct */
    int mode;			/* TCL_MODE_BLOCKING or TCL_MODE_NONBLOCKING */
{
    if (mode == TCL_MODE_BLOCKING) {
	return DpUdpSetSocketOption(instanceData, DP_BLOCK, 1);
    } else {
	return DpUdpSetSocketOption(instanceData, DP_BLOCK, 0);
    }
}

/*
 *--------------------------------------------------------------
 *
 * UdpClose --
 *
 *	This function is called by the Tcl channel driver when
 *	the caller want to close the socket.
 *	It releases the instanceData and closes the scoket
 *	All queued output will have been flushed to the device
 *	before this function is called.
 *
 * Results:
 *	Zero for success, otherwise a nonzero POSIX error code and,
 *	if interp is not NULL, an error message in interp->result
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
static int
UdpClose (instanceData, interp)
    ClientData instanceData;	/* (in) Pointer to udpState struct */
    Tcl_Interp *interp;		/* (in) For error reporting */
{
    UdpState *statePtr = (UdpState *)instanceData;
    int result;

    result = DppCloseSocket(statePtr->sock);
    if ((result != 0) && (interp != NULL)) {
        DppGetErrno();
        Tcl_SetResult(interp, Tcl_PosixError(interp), TCL_STATIC);
    }
    ckfree((char *)statePtr);
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * UdpInput --
 *
 *	This function is called by the Tcl channel driver whenever
 *	the user wants to get input from the UDP socket.
 *	If the socket has some data available but
 *	less than requested by the bufSize argument, we only read
 *	as much data as is available and return without blocking.
 *	If the socket has no data available whatsoever and is
 *	blocking, we block until at least one byte of data can be
 *	read from the socket.
 *
 * Results:
 *	A nonnegative integer indicating how many bytes were read,
 *	or -1 in case of error (with errorCodePtr set to the POSIX
 *	error code).
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
static int
UdpInput (instanceData, buf, bufSize, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to udpState struct */
    char *buf;			/* (in/out) Buffer to fill */
    int bufSize;		/* (in) Size of buffer */
    int *errorCodePtr;		/* (out) POSIX error code (if any) */
{
    UdpState *statePtr = (UdpState *)instanceData;
    int result, peek;
    int fromHost, fromPort;
    char str[256];
    DpSocketAddressIP fromAddr;
    int bytesRead, flags = 0, fromLen;

    peek = (statePtr->flags & PEEK_MODE);
    if (peek) {
        flags = MSG_PEEK;
    } else {
        flags = 0;
    }
    fromLen = sizeof(fromAddr);
    bytesRead = recvfrom(statePtr->sock, buf, bufSize, flags,
	    (DpSocketAddress *)&fromAddr, &fromLen);
    if (bytesRead == DP_SOCKET_ERROR) {
	*errorCodePtr = DppGetErrno();
	return -1;
    }
    if (statePtr->interp != NULL) {
	fromHost = ntohl(fromAddr.sin_addr.s_addr);
	fromPort = ntohs(fromAddr.sin_port);

	sprintf (str, "{%d.%d.%d.%d %d}",  (fromHost>>24),
		(fromHost>>16) & 0xFF, (fromHost>>8) & 0xFF, fromHost & 0xFF,
		fromPort);
	Tcl_SetVar(statePtr->interp, "dp_from", str, TCL_GLOBAL_ONLY);
    }
    return bytesRead;
}

/*
 *--------------------------------------------------------------
 *
 * UdpOutput --
 *
 *	This function is called by the Tcl channel driver whenever
 *	the user wants to send output to the UDP socket.
 *	The function writes toWrite bytes from buf to the socket.
 *
 * Results:
 *	A nonnegative integer indicating how many bytes were written
 *	to the socket. The return value is normally the same as toWrite,
 *	but may be less in some cases such as if the output operation
 *	is interrupted by a signal.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
static int
UdpOutput (instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to udpState struct */
    CONST84 char *buf;			/* (in) Buffer to write */
    int toWrite;		/* (in) Number of bytes to write */
    int *errorCodePtr;		/* (out) POSIX error code (if any) */
{
    UdpState *statePtr = (UdpState *) instanceData;
    DpSocketAddressIP dsa;
    int result;

    dsa.sin_family = AF_INET;
    dsa.sin_addr.s_addr = htonl(statePtr->destIpAddr);
    dsa.sin_port = htons((unsigned short)statePtr->destPort);

    result = sendto(statePtr->sock, buf, toWrite, 0,
	    (DpSocketAddress *)&dsa, sizeof(dsa));
    if (result == DP_SOCKET_ERROR) {
	*errorCodePtr = DppGetErrno();
    }
    return result;

}

/*
 *--------------------------------------------------------------
 *
 * UdpSetOption --
 *
 *	This function is called by the Tcl channel driver
 *	whenever Tcl evaluates and fconfigure call to set
 *	some property of the udp socket (e.g., the buffer
 *	size).  The valid options are "sendBuffer" and
 *	"recvBuffer"
 *
 * Results:
 *	Standard Tcl return value.
 *
 * Side effects:
 *	Depends on the option.  Generally changes the maximum
 *	message size that can be sent/received.
 *
 *--------------------------------------------------------------
 */
static int
UdpSetOption (instanceData, interp, optionName, optionValue)
    ClientData instanceData;
    Tcl_Interp *interp;
    CONST char *optionName;
    CONST char *optionValue;
{
    int option;
    int value;
    UdpState *statePtr = (UdpState *)instanceData;

    /*
     * Set the option specified by optionName
     */
    if (optionName[0] == '-') {
        option = DpTranslateOption(optionName+1);
    } else {
        option = -1;
    }
    switch (option) {
	case DP_SEND_BUFFER_SIZE:
	case DP_RECV_BUFFER_SIZE:
	    if (Tcl_GetInt(interp, optionValue, &value) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (value <=0) {
		Tcl_AppendResult (interp, "Buffer size must be > 0", NULL);
		return TCL_ERROR;
	    }
	    return DpUdpSetSocketOption (statePtr, option, value);

	case DP_PEEK:
	    if (Tcl_GetBoolean(interp, optionValue, &value) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (value == 0) {
		statePtr->flags &= ~PEEK_MODE;
	    } else {
		statePtr->flags |= PEEK_MODE;
	    }
	    break;

	case DP_HOST:
	    if (DpHostToIpAddr (optionValue, &value) == 0) {
		Tcl_AppendResult (interp,
			"Expected IP address or hostname but got \"",
			optionValue, "\"", NULL);
		return TCL_ERROR;
	    }
	    statePtr->destIpAddr = value;
	    break;

	case DP_PORT:
	    if (Tcl_GetInt(interp, optionValue, &value) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (value <= 0) {
		Tcl_AppendResult (interp, "Port number must be > 0", NULL);
		return TCL_ERROR;
	    }
	    statePtr->destPort = (unsigned short) value;
	    break;

	case DP_MYPORT:
	    Tcl_AppendResult (interp, "Can't set port after socket is opened",
		    NULL);
	    return TCL_ERROR;

	default:
	    Tcl_AppendResult (interp, "Illegal option \"", optionName,
		    "\" -- must be sendBuffer, recvBuffer, peek, ",
		    "host, port, or a standard fconfigure option", NULL);
	    return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * UdpGetOption --
 *
 *	This function is called by the Tcl channel code to
 *	retrieve a parameter of the socket (e.g., a buffer size).
 *	The valid options are "sendBuffer" and "recvBuffer"
 *
 * Results:
 *	A standard Tcl result
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
static int
UdpGetOption (instanceData, optionName, dsPtr)
    ClientData instanceData;
    CONST84 char *optionName;
    Tcl_DString *dsPtr;
{
    int option;
    int size;
    unsigned int addr;
    char str[256];
    UdpState *statePtr = (UdpState *)instanceData;

    /*
     * If optionName is NULL, then store an alternating list of
     * all supported options and their current values in dsPtr
     */
    if (optionName == NULL) {
	Tcl_DStringAppend (dsPtr, " -sendBuffer ", -1);
	UdpGetOption(instanceData, "-sendBuffer", dsPtr);
	Tcl_DStringAppend (dsPtr, " -recvBuffer ", -1);
	UdpGetOption(instanceData, "-recvBuffer", dsPtr);
	Tcl_DStringAppend (dsPtr, " -peek ", -1);
	UdpGetOption(instanceData, "-peek", dsPtr);
	Tcl_DStringAppend (dsPtr, " -host ", -1);
	UdpGetOption(instanceData, "-host", dsPtr);
	Tcl_DStringAppend (dsPtr, " -port ", -1);
	UdpGetOption(instanceData, "-port", dsPtr);
	Tcl_DStringAppend (dsPtr, " -myport ", -1);
	UdpGetOption(instanceData, "-myport", dsPtr);
        return TCL_OK;
    }

    /*
     * Retrive the value of the option specified by optionName
     */

    if (optionName[0] == '-') {
        option = DpTranslateOption(optionName+1);
    } else {
        option = -1;
    }

    switch (option) {
	case DP_SEND_BUFFER_SIZE:
	case DP_RECV_BUFFER_SIZE:
	    DpUdpGetSocketOption (statePtr, option, &size);
	    sprintf (str, "%d", size);
	    Tcl_DStringAppend (dsPtr, str, -1);
	    break;

	case DP_PEEK:
	    if (statePtr->flags & PEEK_MODE) {
		Tcl_DStringAppend (dsPtr, "1", -1);
	    } else {
		Tcl_DStringAppend (dsPtr, "0", -1);
	    }
	    break;

	case DP_HOST:
	    addr = statePtr->destIpAddr;
	    sprintf (str, "%d.%d.%d.%d",
		    (addr >>24), (addr >>16) & 0xff,
		    (addr >> 8) & 0xff, (addr) & 0xff);
	    Tcl_DStringAppend (dsPtr, str, -1);
	    break;

	case DP_PORT:
	    sprintf (str, "%d", (unsigned short) statePtr->destPort);
	    Tcl_DStringAppend (dsPtr, str, -1);
	    break;

	case DP_MYPORT:
	    sprintf (str, "%d", (unsigned short) statePtr->myPort);
	    Tcl_DStringAppend (dsPtr, str, -1);
	    break;

	default:
	    {
	    	char errStr[128];
	    	sprintf(errStr, "bad option \"%s\": must be -blocking,"
	    		"-buffering, -buffersize, -eofchar, -translation,"
	    		" or a channel type specific option", optionName);
	    	Tcl_DStringAppend(dsPtr, errStr, -1);
	    }
	    Tcl_SetErrno (EINVAL);
	    return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 *  DpOpenUdpChannel --
 *
 *	Opens a new channel that uses the UDP protocol.
 *
 * Results:
 *	Returns a pointer to the newly created Tcl_Channel.  This
 *	is the structure with all the function pointers Tcl needs
 *	to communicate with (read, write, close, etc) the channel.
 *
 * Side effects:
 *	A socket is created with the specified port.  No other
 *	socket can use that port until this channel is closed.
 *
 *--------------------------------------------------------------
 */
Tcl_Channel
DpOpenUdpChannel(interp, argc, argv)
    Tcl_Interp *interp;		/* For error reporting; can be NULL. */
    int argc;			/* Number of arguments. */
    CONST84 char **argv;                /* Argument strings. */
{
    Tcl_Channel chan;
    UdpState *statePtr;
    char channelName[20];
    int i, result;

    /*
     * The default values for the value-option pairs
     */

    int hostIp = 0;
    int port = 0;
    int myIpAddr = DP_INADDR_ANY;
    int myport = 0;

    for (i=0; i<argc; i+=2) {
        int v = i+1;
	size_t len = strlen(argv[i]);

	if (strncmp(argv[i], "-host", len)==0) {
	    if (v==argc) {goto arg_missing;}
	    if (!DpHostToIpAddr (argv[v], &hostIp)) {
	        Tcl_AppendResult (interp, "Unknown host \"", argv[v],
	        	"\"", NULL);
		return NULL;
	    }
	} else if (strncmp(argv[i], "-port", len)==0) {
	    if (v==argc) {goto arg_missing;}
	    if (Tcl_GetInt(interp, argv[v], &port) != TCL_OK) {
		return NULL;
	    }
	    if (port <= 0) {
		Tcl_AppendResult (interp, "Port number must be > 0", NULL);
		return NULL;
	    }
	} else if (strncmp(argv[i], "-myaddr", len)==0) {
	    if (v==argc) {goto arg_missing;}
	    if (strcmp (argv[v], "any") == 0) {
		myIpAddr = DP_INADDR_ANY;
	    } else if (!DpHostToIpAddr (argv[v], &myIpAddr)) {
		Tcl_AppendResult (interp, "Illegal value for -myaddr \"",
			 argv[v], "\"", NULL);
		return NULL;
	    }
	} else if (strncmp(argv[i], "-myport", len)==0) {
	    if (v==argc) {goto arg_missing;}
	    if (Tcl_GetInt(interp, argv[v], &myport) != TCL_OK) {
		return NULL;
	    }
	    if (myport <= 0) {
		Tcl_AppendResult (interp,
			"Port number for -myport must be > 0", NULL);
		return NULL;
	    }
	} else {
    	    Tcl_AppendResult(interp, "unknown option \"",
		    argv[i], "\", must be -host, -myaddr, -myport ",
		    "or -port", NULL);
	    return NULL;
	}
    }

    /*
     * Create a new socket and wrap it in a channel.
     */

    statePtr		    = (UdpState *)ckalloc(sizeof(UdpState));
    statePtr->flags	    = 0;
    statePtr->interp	    = interp;
    statePtr->myPort	    = myport;
    statePtr->destIpAddr    = hostIp;
    statePtr->destPort	    = port;
    result = DpCreateUdpSocket(interp, myIpAddr, statePtr);

    if (result != TCL_OK) {
	ckfree((char *)statePtr);
	return NULL;
    }
    sprintf(channelName, "udp%d", udpCount++);
    chan = Tcl_CreateChannel(&udpChannelType, channelName,
	    (ClientData)statePtr, TCL_READABLE|TCL_WRITABLE);
	Tcl_RegisterChannel(interp, chan);

    /*
     * Set the initial state of the channel.
     * Make sure the socket's blocking, set the default buffer sizes,
     * set the destination address as specified, disable Tcl buffering
     * and translation.
     */

    DpUdpSetSocketOption(statePtr, DP_SEND_BUFFER_SIZE, 8192);
    DpUdpSetSocketOption(statePtr, DP_RECV_BUFFER_SIZE, 8192);

    DpUdpGetSocketOption(statePtr, DP_RECV_BUFFER_SIZE,
    	    &statePtr->recvBufSize);

    if (Tcl_SetChannelOption(interp, chan, "-translation", "binary") !=
            TCL_OK) {
        DpClose(interp, chan);
        ckfree((char *)statePtr);
        return NULL;
    }
    if (Tcl_SetChannelOption(interp, chan, "-blocking", "1") !=
            TCL_OK) {
        DpClose(interp, chan);
        ckfree((char *)statePtr);
	return NULL;
    }
    if (Tcl_SetChannelOption(interp, chan, "-buffering", "none") !=
            TCL_OK) {
        DpClose(interp, chan);
        ckfree((char *)statePtr);
        return NULL;
    }

    return chan;

arg_missing:
    Tcl_AppendResult(interp, "value for \"", argv[argc-1], "\" missing", NULL);
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * UdpWatch --
 *
 *	Gives a short overview (a few sentences), what other
 *	functions are related to this one.
 *
 *	All changes to the module made that decrease resource usage,
 *	but make the function harder to understand, modify, and debug.
 *
 * Results:
 *	Description of return values.
 *
 * Side effects:
 *	Global variables touched.
 *	I/O operations performed.
 *	Delayed effects.
 *
 *--------------------------------------------------------------
 */
static void
UdpWatch (instanceData, mask)
    ClientData instanceData;
    int mask;
{
    UdpState *infoPtr = (UdpState *) instanceData;

    Tcl_WatchFile(infoPtr->sockFile, mask);
}


/*
 *--------------------------------------------------------------
 *
 * UdpReady --
 *
 *	Gives a short overview (a few sentences), what other
 *	functions are related to this one.
 *
 *	All changes to the module made that decrease resource usage,
 *	but make the function harder to understand, modify, and debug.
 *
 * Results:
 *	Description of return values.
 *
 * Side effects:
 *	Global variables touched.
 *	I/O operations performed.
 *	Delayed effects.
 *
 *--------------------------------------------------------------
 */
static int
UdpReady (instanceData, mask)
    ClientData instanceData;
    int mask;
{
    UdpState *statePtr = (UdpState *) instanceData;
    return Tcl_FileReady(statePtr->sockFile, mask);
}


/*
 *--------------------------------------------------------------
 *
 * UdpGetFile --
 *
 *	Gives a short overview (a few sentences), what other
 *	functions are related to this one.
 *
 *	All changes to the module made that decrease resource usage,
 *	but make the function harder to understand, modify, and debug.
 *
 * Results:
 *	Description of return values.
 *
 * Side effects:
 *	Global variables touched.
 *	I/O operations performed.
 *	Delayed effects.
 *
 *--------------------------------------------------------------
 */
static Tcl_File
UdpGetFile(instanceData, direction)
    ClientData instanceData;
    int direction;
{
    UdpState *statePtr = (UdpState *)instanceData;

    return statePtr->sockFile;
}


/*
 *--------------------------------------------------------------
 *
 * DpUdpSetSocketOption --
 *
 *	Sets a socket option.  The allowable options for UDP
 *	sockets are
 *		DP_SEND_BUFFER_SIZE	(int)
 *		DP_RECV_BUFFER_SIZE	(int)
 *		DP_BLOCK		(T/F)
 *
 * Results:
 *	Zero if the operation was successful, or a nonzero POSIX
 *	error code if the operation failed.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
int
DpUdpSetSocketOption (clientData, option, value)
    ClientData clientData;	/* (in) UdpState structure */
    int option;			/* (in) Option to set */
    int value;			/* (in) new value for option */
{
    UdpState *statePtr = (UdpState *)clientData;
    int sock, result;

    sock = statePtr->sock;
    switch (option) {
        case DP_SEND_BUFFER_SIZE:
	    result = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&value,
	    	   sizeof(value));
	    break;

	case DP_RECV_BUFFER_SIZE:
	    result = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&value,
	    	   sizeof(value));
	    break;

	case DP_BLOCK:
	    result = DppSetBlock (sock, value);
	    break;

	default:
	    return EINVAL;
    }

    if (result != 0) {
	return Tcl_GetErrno();
    }
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * DpUdpGetSocketOption --
 *
 *	Sets a socket option.  The allowable options for UDP
 *	sockets are
 *		DP_SEND_BUFFER_SIZE	(int)
 *		DP_RECV_BUFFER_SIZE	(int)
 *	Note that we can't determine whether a socket is blocking,
 *	so DP_BLOCK is not allowed.
 *
 * Results:
 *	Zero if the operation was successful, or a nonzero POSIX
 *	error code if the operation failed.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
int
DpUdpGetSocketOption (clientData, option, valuePtr)
    ClientData clientData;	/* (in) UdpState structure */
    int option;			/* (in) Option to set */
    int *valuePtr;		/* (out) current value of option */
{
    UdpState *statePtr = (UdpState *)clientData;
    int sock, result, len;

    sock = statePtr->sock;
    len = sizeof(int);
    switch (option) {
        case DP_SEND_BUFFER_SIZE:
	    result = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)valuePtr,
	    	   &len);
	    break;

	case DP_RECV_BUFFER_SIZE:
	    result = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)valuePtr,
	    	   &len);
	    break;

	default:
	    return EINVAL;
    }

    if (result != 0) {
	return Tcl_GetErrno();
    }
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 *  DpCreateUdpSocket  --
 *
 *	Create a udp socket.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
int
DpCreateUdpSocket(interp, myIpAddr, statePtr)
    Tcl_Interp *interp;			/* (in) For error reporting. */
    int myIpAddr;			/* (in) IP addr of interface to use.
    					 *   DP_INADDR_ANY = default port */
    UdpState *statePtr;			/* (out) Pointer to local structure */
{
    DpSocketAddressIP sockAddr;
    DpSocket sock;
    int len;

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock == DP_SOCKET_ERROR) {
	goto socketError;
    }
    statePtr->sock = sock;

    /*
     * Bind the socket.
     * This is a bit of a mess, but it's Berkeley sockets.  The sin_family
     * is set to AF_INET, indicating IP addressing.  The sin_addr.s_addr
     * field says what interface to use.  It can be INADDR_ANY to let
     * the system choose a default interface.  The port number can be
     * zero (which tells the system to choose a port number) or > 1024,
     * which is then used as the port number
     */

    memset((char *)&sockAddr, 0, sizeof(sockAddr));
    sockAddr.sin_family = AF_INET;
    if (myIpAddr == DP_INADDR_ANY) {
	sockAddr.sin_addr.s_addr = INADDR_ANY;
    } else {
	sockAddr.sin_addr.s_addr = htonl(myIpAddr);
    }
    sockAddr.sin_port = htons((unsigned short) statePtr->myPort);
    if (bind(sock, (DpSocketAddress *)&sockAddr, sizeof(sockAddr)) ==
	    DP_SOCKET_ERROR) {
        goto bindError;
    }

    /*
     * Figure out what port number we got if we let the system chose it.
     */

    if (statePtr->myPort == 0) {
	len = sizeof(sockAddr);
	getsockname (sock, (DpSocketAddress *)&sockAddr, &len);
	statePtr->myPort = ntohs(sockAddr.sin_port);
    }
    statePtr->sockFile = Tcl_GetFile((ClientData)statePtr->sock, DP_SOCKET);

    return TCL_OK;

bindError:
    DppGetErrno();
    Tcl_AppendResult(interp, "Error binding UDP socket to port: ",
	    Tcl_PosixError(interp), NULL);
    DppCloseSocket (sock);
    return TCL_ERROR;

socketError:
    DppGetErrno();
    Tcl_AppendResult(interp, "Error creating UDP socket: ",
	    Tcl_PosixError(interp), NULL);
    return TCL_ERROR;
}
