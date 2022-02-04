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


static int	    UdpInput _ANSI_ARGS_((ClientData instanceData, 
				char *buf, int bufSize, 
				int *errorCodePtr));
static int	    UdpOutput _ANSI_ARGS_((ClientData instanceData, 
				char *buf, int toWrite, 
				int *errorCodePtr));
static int	    UdpSetOption _ANSI_ARGS_((ClientData instanceData,
				Tcl_Interp *interp, char *optionName,
				char *optionValue));
static int	    UdpGetOption _ANSI_ARGS_((ClientData instanceData,
				Tcl_Interp *interp, char *optionName, 
				Tcl_DString *dsPtr));

typedef SocketState UdpState;

static Tcl_ChannelType udpChannelType = {
     "udp",		/* Name of channel */
     SockBlockMode,	/* Proc to set blocking mode on socket */
     SockClose,		/* Proc to close a socket */
     UdpInput,		/* Proc to get input from a socket */
     UdpIpmOutput,	/* Proc to send output to a socket */
     NULL,              /* Can't seek on a socket! */
     UdpSetOption,	/* Proc to set a socket option */
     UdpGetOption,	/* Proc to set a socket option */
     SockWatch,		/* Proc called to set event loop wait params */
     SockGetFile	/* Proc to return a handle assoc with socket */
};

#define	PEEK_MODE	(1<<1)	/* Read without consuming? */

static int udpCount = 0;        /* Number of udp files opened -- used to
                                 * generate unique ids for channels */

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
    char **argv;                /* Argument strings. */
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

    if (!initd) {
    	InitDpSockets();
    	initd = TRUE;
    }

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
	memset(statePtr, 0, sizeof(UdpState));
    statePtr->flags	    = 0;
    statePtr->interp	    = interp;
    statePtr->myPort	    = myport;

    result = CreateUdpSocket(interp, myIpAddr, statePtr);

    statePtr->sockaddr.sin_port = htons((unsigned short)port);
    statePtr->sockaddr.sin_family = AF_INET;
    statePtr->sockaddr.sin_addr.s_addr = htonl(hostIp);

    if (result != TCL_OK) {
	ckfree((char *)statePtr);
	return NULL;
    }
    sprintf(channelName, "udp%d", udpCount++);
    chan = Tcl_CreateChannel(&udpChannelType, channelName, 
	    (ClientData)statePtr, TCL_READABLE|TCL_WRITABLE);

    DppSetupSocketEvents(statePtr, statePtr->sock, 0, 0);

    Tcl_RegisterChannel(interp, chan);            

    statePtr->sockInfo->channel = chan;

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
CreateUdpSocket(interp, myIpAddr, statePtr)
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

    memset(&statePtr->sockaddr, 0, sizeof(statePtr->sockaddr));
    statePtr->sockaddr.sin_family      = AF_INET;
    statePtr->sockaddr.sin_port        = 
	    htons((unsigned short) statePtr->myPort);
    statePtr->sockaddr.sin_addr.s_addr = 
	    (myIpAddr == DP_INADDR_ANY ? INADDR_ANY : htonl(myIpAddr));

    if (bind(sock, (DpSocketAddress *)&statePtr->sockaddr, 
	    sizeof(statePtr->sockaddr)) == DP_SOCKET_ERROR) {
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
    statePtr->sockFile = (ClientData)statePtr->sock;

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
    SocketInfo *infoPtr = statePtr->sockInfo;
    int peek;
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

    while (1) {
	if (infoPtr->readyEvents & (FD_CLOSE|FD_READ)) {

	    fromLen = sizeof(fromAddr);
	    bytesRead = recvfrom(statePtr->sock, buf, bufSize, flags,
		    (DpSocketAddress *)&fromAddr, &fromLen);
#ifdef UDPDEBUG
	    {
		char msg[1024];
		memcpy(msg, buf, bytesRead);
		msg[bytesRead] = '\0';
		printf("Received UDP data: %s\n", msg);
	    }
#endif

	    statePtr->sockInfo->readyEvents &= ~(FD_READ);

	    if (bytesRead < 0) {
	    	int error;
		error = WSAGetLastError();
		TclWinConvertWSAError(error);
		if ((infoPtr->flags & SOCKET_ASYNC) ||
			(error != WSAEWOULDBLOCK)) {
		    *errorCodePtr = Tcl_GetErrno();
		    return -1;
		}
	    }
		break;

	} else if (infoPtr->flags & SOCKET_ASYNC) {
	    *errorCodePtr = EWOULDBLOCK;
	    return -1;
	}

    	/*
	 * In the blocking case, wait until the file becomes readable
	 * or closed and try again.
	 */

	if (!WaitForSocketEvent(infoPtr, FD_READ|FD_CLOSE, errorCodePtr)) {
	    return -1;
	}
    }

    if (statePtr->interp != NULL) {
	fromHost = ntohl(fromAddr.sin_addr.s_addr);
	fromPort = ntohs(fromAddr.sin_port);
    
	sprintf (str, "{%d.%d.%d.%d %d}",  (fromHost>>24),
		(fromHost>>16) & 0xFF, (fromHost>>8) & 0xFF, 
		fromHost & 0xFF, fromPort);
	Tcl_SetVar(statePtr->interp, "dp_from", str, TCL_GLOBAL_ONLY);
    }	
    return bytesRead;
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
    char *optionName;
    char *optionValue;
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
	    statePtr->sockaddr.sin_addr.s_addr = htonl(value);
	    break;

	case DP_PORT:
	    if (Tcl_GetInt(interp, optionValue, &value) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (value <= 0) {
		Tcl_AppendResult (interp, "Port number must be > 0", NULL);
		return TCL_ERROR;
	    }
	    statePtr->sockaddr.sin_port = htons((unsigned short) value);
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
UdpGetOption (instanceData,
		interp,
		optionName, dsPtr)
    ClientData instanceData;
    Tcl_Interp *interp; 
    char *optionName;
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
	UdpGetOption(instanceData, interp, "-sendBuffer", dsPtr);
	Tcl_DStringAppend (dsPtr, " -recvBuffer ", -1);
	UdpGetOption(instanceData, interp, "-recvBuffer", dsPtr);
	Tcl_DStringAppend (dsPtr, " -peek ", -1);
	UdpGetOption(instanceData, interp, "-peek", dsPtr);
	Tcl_DStringAppend (dsPtr, " -host ", -1);
	UdpGetOption(instanceData, interp, "-host", dsPtr);
	Tcl_DStringAppend (dsPtr, " -port ", -1);
	UdpGetOption(instanceData, interp, "-port", dsPtr);
	Tcl_DStringAppend (dsPtr, " -myport ", -1);
	UdpGetOption(instanceData, interp, "-myport", dsPtr);
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
	    addr = ntohl(statePtr->sockaddr.sin_addr.s_addr);
	    sprintf (str, "%d.%d.%d.%d",
		    (addr >>24), (addr >>16) & 0xff,
		    (addr >> 8) & 0xff, (addr) & 0xff);
	    Tcl_DStringAppend (dsPtr, str, -1);
	    break;

	case DP_PORT:
	    sprintf (str, "%d", ntohs(statePtr->sockaddr.sin_port));
	    Tcl_DStringAppend (dsPtr, str, -1);
	    break;

	case DP_MYPORT:
	    sprintf (str, "%d", (unsigned short) statePtr->myPort);
	    Tcl_DStringAppend (dsPtr, str, -1);
	    break;

	default:
	    Tcl_AppendResult(interp, 
		    "bad option \"", optionName,"\": must be -blocking,",
		    " -buffering, -buffersize, -eofchar, -translation,"
		    " or a channel type specific option", NULL);
	    Tcl_SetErrno (EINVAL);
	    return TCL_ERROR;
    }
    return TCL_OK;
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

