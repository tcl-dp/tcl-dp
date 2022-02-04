/*
 * generic/dpTcp.c --
 *
 *	This file implements the generic code for a tcp channel
 *	driver.  These are channels that are created by evaluating
 *	"dp_connect tcp".
 *
 *	This file was drerived from generic/dpUdp.c. See the
 *	header of that file for more detailed information about
 *	the way DP uses sockets.
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
 * The maximum length of the queue of pending connections to a
 * Tcp server socket.
 */

#define DP_LISTEN_LIMIT 100

/*
 * The default send and receive buffer size.
 */
#define DP_TCP_SENDBUFSIZE 8192
#define DP_TCP_RECVBUFSIZE 8192

typedef SocketState TcpState;

#define	PEEK_MODE	(1<<1)	/* Read without consuming? */
#define	ASYNC_CONNECT	(1<<2)	/* Asynchronous connection? */
#define	IS_SERVER	(1<<3)	/* Is this a server Tcp socket? */

/*
 * Procedures that are used in this file only.
 */

static int 		TcpInput _ANSI_ARGS_((ClientData instanceData, 
                            char *buf, int bufSize, int *errorCodePtr));
static int		TcpOutput _ANSI_ARGS_((ClientData instanceData, 
                            char *buf, int toWrite, int *errorCodePtr));
static int		TcpSetOption _ANSI_ARGS_((ClientData instanceData,
                            Tcl_Interp *interp, char *optionName,
                            char *optionValue));
static int		TcpGetOption _ANSI_ARGS_((ClientData instanceData,
			    Tcl_Interp *interp, char *optionName, 
			    Tcl_DString *dsPtr));

static Tcl_Channel	CreateClientChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    int destIpAddr, int destPort, int myIpAddr,
			    int myPort, int async));
static Tcl_Channel	CreateServerChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    int myIpAddr, int myPort));
static int		SetDefaultOptions _ANSI_ARGS_((Tcl_Interp * interp,
			    Tcl_Channel chan));
static int 		DpTcpSetSocketOption _ANSI_ARGS_((TcpState *statePtr,
			    int option, int value));
static int 		DpTcpGetSocketOption _ANSI_ARGS_((TcpState *statePtr,
			    int option, int *valuePtr));
static int		DpSetAddress _ANSI_ARGS_((TcpState *statePtr));


static Tcl_ChannelType tcpChannelType = {
     "tcp",		/* Name of channel */
     SockBlockMode,	/* Proc to set blocking mode on socket */
     SockClose,		/* Proc to close a socket */
     TcpInput,		/* Proc to get input from a socket */
     TcpOutput,		/* Proc to send output to a socket */
     NULL,              /* Can't seek on a socket! */
     TcpSetOption,	/* Proc to set a socket option */
     TcpGetOption,	/* Proc to set a socket option */
     SockWatch,		/* Proc called to set event loop wait params */
     SockGetFile	/* Proc to return a handle assoc with socket */
};

static int tcpCount = 0;        /* Number of tcp files opened -- used to
                                 * generate unique ids for channels */


/*
 *--------------------------------------------------------------
 *
 *  DpOpenTcpChannel --
 *
 *	Opens a new channel that uses the TCP protocol.
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
DpOpenTcpChannel(interp, argc, argv)
    Tcl_Interp *interp;		/* For error reporting; can be NULL. */
    int argc;			/* Number of arguments. */
    char **argv;                /* Argument strings. */
{
    Tcl_Channel chan;
    TcpState *statePtr = NULL;
    char channelName[20];
    int i;

    /*
     * The default values for the value-option pairs
     */
    int async      = 0;
    int destIpAddr = 0;
    int destPort   = 0;
    int myIpAddr   = DP_INADDR_ANY;
    int myPort     = 0;
    int isServer   = 0;

    /*
     * Flags to indicate that a certain option has been set by the
     * command line
     */
    int setAsync  = 0;
    int setHost   = 0;
    int setMyPort = 0;
    int setPort   = 0;

    if (!initd) {
    	InitDpSockets();
    	initd = TRUE;
    }

    for (i=0; i<argc; i+=2) {
        int v = i+1;
	size_t len = strlen(argv[i]);

	if (strncmp(argv[i], "-async", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (Tcl_GetBoolean(interp, argv[v], &async) != TCL_OK) {
		return NULL;
	    }
	    setAsync = 1;
	}
	else if (strncmp(argv[i], "-host", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (!DpHostToIpAddr (argv[v], &destIpAddr)) {
		Tcl_AppendResult (interp, "Illegal value for -host \"",
			argv[v], "\"", NULL);
		return NULL;
	    }
	    setHost = 1;
	}
	else if (strncmp(argv[i], "-myaddr", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (strcmp (argv[v], "any") == 0) {
		myIpAddr = DP_INADDR_ANY;
	    } else if (!DpHostToIpAddr (argv[v], &myIpAddr)) {
		Tcl_AppendResult (interp, "Illegal value for -myaddr \"",
			 argv[v], "\"", NULL);
		return NULL;
	    }
	}
	else if (strncmp(argv[i], "-myport", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (Tcl_GetInt(interp, argv[v], &myPort) != TCL_OK) {
		return NULL;
	    }
	    setMyPort = 1;
	}
	else if (strncmp(argv[i], "-port", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (Tcl_GetInt(interp, argv[v], &destPort) != TCL_OK) {
		return NULL;
	    }
	    setPort = 1;
	}
	else if (strncmp(argv[i], "-server", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (Tcl_GetBoolean(interp, argv[v], &isServer) != TCL_OK) {
		return NULL;
	    }
	}
	else {
    	    Tcl_AppendResult(interp, "unknown option \"", 
		    argv[i], "\", must be -async, -host, -myaddr, -myport ",
		    "-port or -server", NULL);
	    return NULL;
	}
    }

    /*
     * Check the options that must or must not be specified, depending on
     * the -server option.
     */

    if (isServer) {
	if (!setMyPort) {
	    Tcl_AppendResult(interp, "option -myport must be specified ",
		    "for servers",
		    NULL);
	    return NULL;
	}
	if (setAsync) {
	    Tcl_AppendResult(interp, "option -async is not valid for servers",
		    NULL);
	    return NULL;
	}
	if (setHost) {
	    Tcl_AppendResult(interp, "option -host is not valid for servers",
		    NULL);
	    return NULL;
	}
	if (setPort) {
	    Tcl_AppendResult(interp, "option -port is not valid for servers",
		    NULL);
	    return NULL;
	}
    } else {
	if (!setPort) {
	    Tcl_AppendResult(interp, "option -port must be specified ",
		    "for clients",
		    NULL);
	    return NULL;
	}
	if (!setHost) {
	    Tcl_AppendResult(interp, "option -host must be specified",
		    NULL);
	    return NULL;
	}
    }

    /*
     * Create a new socket and wrap it in a channel.
     */
    
    if (isServer) {
	chan = CreateServerChannel(interp, myIpAddr, myPort);
    } else {
	chan = CreateClientChannel(interp, destIpAddr, destPort, myIpAddr,
		myPort, async);
    }
    if (chan == NULL) {
	return NULL;
    }

    Tcl_RegisterChannel(interp, chan);            

    if (SetDefaultOptions(interp, chan) != TCL_OK) {
        DpClose(interp, chan);
	return NULL;
    }

    return chan;

arg_missing:
    Tcl_AppendResult(interp, "value for \"", argv[argc-1], "\" missing", NULL);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Dp_TcpAccept --
 *
 *	This procedure blocks until a client is connected to a given
 *	Tcp server channel.
 *
 * Results:
 *	On success, returns the Tcl_Channel of the connection. On failure,
 *	returns NULL.
 *
 * Side effects:
 *	The first client on the "queue of connected clients" (maintained
 *	by the OS) is removed from the queue.
 *
 *----------------------------------------------------------------------
 */
Tcl_Channel
Dp_TcpAccept(interp, channelId)
    Tcl_Interp *interp;		/* For error reporting. */
    char *channelId;            /* Name of a server Tcp channel. */
{
    struct sockaddr_in destSockAddr;	/* Address of client host. */
    TcpState *svrStatePtr = NULL;	/* State of the server socket. */
    TcpState *statePtr = NULL;		/* State of the new socket. */
    int sock;
    Tcl_Channel chan;
    int len, mode;
    char channelName[20];
    int argc, ip;
    char *argv[2], *argRet;
    char ipStr[16];

    chan = Tcl_GetChannel(interp, channelId, &mode);
    if (chan == NULL) {
        return NULL;
    }

    if (Tcl_GetChannelType(chan) != &tcpChannelType) {
	Tcl_AppendResult(interp, "\"", channelId,
	        "\" must be a TCP server channel", NULL);
	return NULL;
    }
    svrStatePtr = (TcpState *)Tcl_GetChannelInstanceData(chan);

    if (!(svrStatePtr->flags & IS_SERVER)) {
	Tcl_AppendResult(interp, "\"", channelId,
	        "\" is a TCP client channel, not a server", NULL);
	return NULL;
    }

    len = sizeof(destSockAddr);
    sock = accept(svrStatePtr->sock, (struct sockaddr *)&destSockAddr, &len);
    if (sock < 0) {
	Tcl_AppendResult(interp, "couldn't accept from socket: ",
                Tcl_PosixError(interp), (char *) NULL);
	return NULL;
    }
    
    statePtr		 = (TcpState *) ckalloc(sizeof(TcpState));
    statePtr->flags      = 0;
    statePtr->sock       = sock;
    statePtr->sockFile   = (ClientData)sock;
    statePtr->interp     = interp;
    statePtr->myIpAddr	 = 0;
    statePtr->myPort	 = 0;
    statePtr->destIpAddr = 0;
    statePtr->destPort	 = 0;

    svrStatePtr->sockInfo->readyEvents &= ~(FD_ACCEPT);

    /*
     * Setup Win32 event handlers for this socket
     */
    DppSetupSocketEvents(statePtr, sock, 0, 0);

    sprintf(channelName, "tcp%d", tcpCount++);
    chan = Tcl_CreateChannel(&tcpChannelType, channelName, 
	    (ClientData)statePtr, TCL_READABLE | TCL_WRITABLE);

    statePtr->sockInfo->channel = chan;
    statePtr->channel = chan;

    Tcl_RegisterChannel(interp, chan);

    if (SetDefaultOptions(interp, chan) != TCL_OK) {
        DpClose(interp, chan);
        ckfree((char *)statePtr);
	return NULL;
    }

    /*
     * Construct return value list { <chanID> <ipAdr> }
     */

    DpTcpGetSocketOption(statePtr, DP_REMOTEIPADDR, &ip);
    sprintf(ipStr, "%d.%d.%d.%d", (ip>>24)&0xFF, (ip>>16)&0xFF, 
	    (ip>>8)&0xFF, ip&0xFF);
    argc = 2;
    argv[0] = channelName;
    argv[1] = ipStr;
    argRet = Tcl_Merge(argc, argv);
    Tcl_AppendResult(interp, argRet, (char *) NULL);
    ckfree((char *)argRet);

    return chan;
}

/*
 *--------------------------------------------------------------
 *
 * TcpInput --
 *
 *	This function is called by the Tcl channel driver whenever the
 *	user wants to get input from the TCP socket. If the socket
 *	has some data available but less than requested by the bufSize
 *	argument, we only read as much data as is available and return
 *	without blocking.  If the socket has no data available
 *	whatsoever and is blocking, we block until at least one byte
 *	of data can be read from the socket.
 *
 * Results:
 *	A nonnegative integer indicating how many bytes were read, or
 *	DP_SOCKET_ERROR in case of error (with errorCodePtr set to the
 *	POSIX error code).
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */

static int
TcpInput (instanceData, buf, bufSize, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to tcpState struct */
    char *buf;			/* (in/out) Buffer to fill */
    int bufSize;		/* (in) Size of buffer */
    int *errorCodePtr;		/* (out) POSIX error code (if any) */
{
    TcpState *statePtr = (TcpState *)instanceData;
    SocketInfo *infoPtr = statePtr->sockInfo;
    int result;

    if ((statePtr->flags & ASYNC_CONNECT)
	    && !WaitForSocketEvent(statePtr->sockInfo,  
				    FD_CONNECT, errorCodePtr)) 
    {
	return -1;
    }

    while (1) {
	if (infoPtr->readyEvents & (FD_CLOSE|FD_READ)) {

	    result = recv(statePtr->sock, buf, bufSize, 0);
	    statePtr->sockInfo->readyEvents &= ~(FD_READ);


	    if (result >= 0) {
#ifdef TCPDEBUG
		{
		    char msg[1024];
		    memcpy(msg, buf, result);
		    msg[result] = '\0';
		    printf("Received TCP data: %s\n", msg);
		}
#endif
	    	return result;
	    } else {
	    	int error;
		error = WSAGetLastError();
		TclWinConvertWSAError(error);
		if (Tcl_GetErrno() == ECONNRESET) {
		    /*
		     * Turn ECONNRESET into a soft EOF condition.
		     */
		    return 0;
		}

		if ((infoPtr->flags & SOCKET_ASYNC) ||
			(error != WSAEWOULDBLOCK)) {
		    *errorCodePtr = Tcl_GetErrno();
		    return -1;
		}
	    }
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
}

/*
 *--------------------------------------------------------------
 *
 * TcpOutput --
 *
 *	This function is called by the Tcl channel driver whenever the
 *	user wants to send output to the TCP socket. The function
 *	writes toWrite bytes from buf to the socket.
 *
 * Results:
 *	On success, returns a nonnegative integer indicating how many
 *	bytes were written to the socket. The return value is normally
 *	the same as toWrite, but may be less in some cases such as if
 *	the output operation is interrupted by a signal.
 *
 *	On failure, returns DP_SOCKET_ERROR;
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
static int
TcpOutput (instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to tcpState struct */
    char *buf;			/* (in) Buffer to write */
    int toWrite;		/* (in) Number of bytes to write */
    int *errorCodePtr;		/* (out) POSIX error code (if any) */
{
    TcpState *statePtr = (TcpState *)instanceData;
    SocketInfo *infoPtr = statePtr->sockInfo;
    int result;
    int error;

    if ((infoPtr->flags & SOCKET_ASYNC_CONNECT)
	    && ! WaitForSocketEvent(infoPtr,  FD_CONNECT, errorCodePtr)) {
	return -1;
    }

    while (1) {
#ifdef TCPDEBUG
	{
	    char msg[1024];
	    memcpy(msg, buf, toWrite);
	    msg[toWrite] = '\0';
	    printf("Sending TCP data: %s\n", msg);
	}
#endif
	result = send(statePtr->sock, buf, toWrite, 0);
	if (result != SOCKET_ERROR) {
	    /*
	     * Since Windows won't generate a new write event until we hit
	     * an overflow condition, we need to force the event loop to
	     * poll until the condition changes.
	     */

	    if (infoPtr->watchEvents & FD_WRITE) {
		Tcl_Time blockTime = { 0, 0 };
		Tcl_SetMaxBlockTime(&blockTime);
	    }		
	    break;
	}
	
	/*
	 * Check for error condition or overflow.  In the event of overflow, we
	 * need to clear the FD_WRITE flag so we can detect the next writable
	 * event.  Note that Windows only sends a new writable event after a
	 * send fails with WSAEWOULDBLOCK.
	 */

	error = WSAGetLastError();
	if (error == WSAEWOULDBLOCK) {
	    infoPtr->readyEvents &= ~(FD_WRITE);
	    if (statePtr->flags & ASYNC_CONNECT) {
		*errorCodePtr = EWOULDBLOCK;
		return -1;
	    } 
	} else {
	    TclWinConvertWSAError(error);
	    *errorCodePtr = Tcl_GetErrno();
	    return -1;
	}

	/*
	 * In the blocking case, wait until the file becomes writable
	 * or closed and try again.
	 */

	if (!WaitForSocketEvent(infoPtr, FD_WRITE|FD_CLOSE, errorCodePtr)) {
	    return -1;
	}
    }

    return result;
}

/*
 *--------------------------------------------------------------
 *
 * TcpSetOption --
 *
 *	This function is called by the Tcl channel driver
 *	whenever Tcl evaluates and fconfigure call to set
 *	some property of the tcp socket (e.g., the buffer
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
TcpSetOption (instanceData, interp, optionName, optionValue)
    ClientData instanceData;
    Tcl_Interp *interp;
    char *optionName;
    char *optionValue;
{
    int option;
    int value;
    TcpState *statePtr = (TcpState *)instanceData;

    /*
     * Set the option specified by optionName
     */
    if (optionName[0] == '-') {
        option = DpTranslateOption(optionName+1);
    } else {
        option = -1;
    }
    switch (option) {
      case DP_KEEP_ALIVE:
      case DP_REUSEADDR:
	if (Tcl_GetBoolean(interp, optionValue, &value) != TCL_OK) {
	    return TCL_ERROR;
	}
	return DpTcpSetSocketOption(statePtr, option, value);

      case DP_LINGER:
	if (Tcl_GetInt(interp, optionValue, &value) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (value <=0) {
	    Tcl_AppendResult(interp, "linger value must be > 0", NULL);
	    return TCL_ERROR;
	}
	return DpTcpSetSocketOption(statePtr, option, value);

      case DP_RECV_BUFFER_SIZE:
      case DP_SEND_BUFFER_SIZE:
	if (Tcl_GetInt(interp, optionValue, &value) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (value <=0) {
	    Tcl_AppendResult(interp, "Buffer size must be > 0", NULL);
	    return TCL_ERROR;
	}
	return DpTcpSetSocketOption(statePtr, option, value);

      default:
	Tcl_AppendResult (interp, "bad option \"", optionName,
  		"\": must be -keepalive, -linger, -recvbuffer, -reuseaddr, ",
		"-sendBuffer or a standard fconfigure option", NULL);
	return TCL_ERROR;
    }
}

/*
 *--------------------------------------------------------------
 *
 * TcpGetOption --
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
TcpGetOption (instanceData, 
		interp,
		optionName, dsPtr)
    ClientData instanceData;
    Tcl_Interp *interp; 
    char *optionName;
    Tcl_DString *dsPtr;
{
    int option;
    int value, rc;
    char str[256];
    TcpState *statePtr = (TcpState *)instanceData;

    /*
     * If optionName is NULL, then return all options in option-value
     * pairs.
     */

    if (optionName == NULL) {
	Tcl_DStringAppend (dsPtr, " -keepAlive ", -1);
	TcpGetOption(instanceData, interp, "-keepAlive", dsPtr);
	Tcl_DStringAppend (dsPtr, " -linger ", -1);
	TcpGetOption(instanceData, interp, "-linger", dsPtr);
	Tcl_DStringAppend (dsPtr, " -recvBuffer ", -1);
	TcpGetOption(instanceData, interp, "-recvBuffer", dsPtr);
	Tcl_DStringAppend (dsPtr, " -reuseAddr ", -1);
	TcpGetOption(instanceData, interp, "-reuseAddr", dsPtr);
	Tcl_DStringAppend (dsPtr, " -sendBuffer ", -1);
	TcpGetOption(instanceData, interp, "-sendBuffer", dsPtr);
	Tcl_DStringAppend (dsPtr, " -myIpAddr ", -1);
	TcpGetOption(instanceData, interp, "-myIpAddr", dsPtr);
	Tcl_DStringAppend (dsPtr, " -myport ", -1);
	TcpGetOption(instanceData, interp, "-myport", dsPtr);
	Tcl_DStringAppend (dsPtr, " -destIpAddr ", -1);
	TcpGetOption(instanceData, interp, "-destIpAddr", dsPtr);
	Tcl_DStringAppend (dsPtr, " -destport ", -1);
	TcpGetOption(instanceData, interp, "-destport", dsPtr);

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
      	case DP_LINGER:
      	case DP_RECV_BUFFER_SIZE:
      	case DP_SEND_BUFFER_SIZE:
      	case DP_MYPORT:
      	case DP_REMOTEPORT:
	    if (DpTcpGetSocketOption(statePtr, option, &value) != 0) {
		return TCL_ERROR;
	    }
	    sprintf (str, "%d", value);
	    Tcl_DStringAppend(dsPtr, str, -1);
	    break;

      	case DP_KEEP_ALIVE:
      	case DP_REUSEADDR:
	    if (DpTcpGetSocketOption(statePtr, option, &value) != 0) {
		return TCL_ERROR;
	    }
	    if (value) {
		/*
		 * Some systems returns a non-zero value 
		 * (not necessarily 1) to indicate "true".
		 */
		value = 1;
	    }
	    sprintf (str, "%d", value);
	    Tcl_DStringAppend(dsPtr, str, -1);
	    break;

	case DP_MYIPADDR:
	case DP_REMOTEIPADDR:
	    if (DpTcpGetSocketOption(statePtr, option, &value) != 0) {
		return TCL_ERROR;
	    }
	    sprintf(str, "%d.%d.%d.%d", (value>>24)&0xFF, (value>>16)&0xFF, 
		    (value>>8)&0xFF, value&0xFF);
	    Tcl_DStringAppend(dsPtr, str, -1);
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
 *----------------------------------------------------------------------
 *
 * CreateClientChannel --
 *
 *	This function opens a new socket in client mode
 *	and initializes the TcpState structure.
 *
 * Results:
 *	Returns a new TcpState, or NULL with an error in interp->result,
 *	if interp is not NULL.
 *
 * Side effects:
 *	Opens a socket.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Channel
CreateClientChannel(interp, destIpAddr, destPort, myIpAddr, myPort, async)
    Tcl_Interp *interp;		/* For error reporting; can be NULL. */
    int destIpAddr;		/* IP address of the host to connect to. */
    int destPort;		/* Port number to open. */
    int myIpAddr;		/* Client-side address */
    int myPort;			/* Client-side port */
    int async;			/* If nonzero and creating a client socket,
                                 * attempt to do an async connect. Otherwise
                                 * do a synchronous connect or bind. */
{
    int status, sock, asyncConnect, origState, len;
    struct sockaddr_in destSockAddr;	/* socket address for host */
    struct sockaddr_in mySockAddr;	/* Socket address for client */
    TcpState *statePtr = NULL;
    char channelName[20];

    sock = -1;
    origState = 0;

    memset((char *)&destSockAddr, 0, sizeof(destSockAddr));
    destSockAddr.sin_family = AF_INET;
    destSockAddr.sin_port = htons((short)destPort);
    destSockAddr.sin_addr.s_addr = htonl(destIpAddr);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	goto error;
    }

    asyncConnect = 0;
    status = 0;

    memset((char *)&mySockAddr, 0, sizeof(mySockAddr));
    mySockAddr.sin_family = AF_INET;
    mySockAddr.sin_port = htons((unsigned short)myPort);
    if (myIpAddr == DP_INADDR_ANY) {
	mySockAddr.sin_addr.s_addr = INADDR_ANY; 
    } else {
	mySockAddr.sin_addr.s_addr = htonl(myIpAddr);
    }

    status = bind(sock, (struct sockaddr *) &mySockAddr,
	    sizeof(mySockAddr));
    if (status < 0) {
	goto error;
    }

    /*
     * Attempt to connect. The connect may fail at present with an
     * EINPROGRESS but at a later time it will complete. The caller
     * will set up a file handler on the socket if she is interested in
     * being informed when the connect completes.
     */

    if (async) {
	status = DppSetBlock(sock, 0);
	if (status < 0) {
	    goto error;
	}
    } else {
	status = 0;
    }

    if (status > -1) {
	status = connect(sock, (struct sockaddr *) &destSockAddr,
                sizeof(destSockAddr));
	if (status < 0) {
	    if (DppGetErrno() == ASYNC_CONNECT_ERROR) {
		asyncConnect = 1;
		status = 0;
	    } else {
		goto error;
	    }
	}
    }

    /*
     * Allocate a new TcpState for this socket.
     */

    statePtr = (TcpState *)ckalloc(sizeof(TcpState));
    statePtr->flags = 0;
    if (asyncConnect) {
        statePtr->flags = ASYNC_CONNECT;
    }
    statePtr->sock		= sock;
    statePtr->sockFile		= (ClientData)sock;
    statePtr->interp		= interp;
    /*
     * These variables are set whem they are needed.
     * A call to fconfigure will prompt the call to
     * SetAddress().
     */
    statePtr->myIpAddr		= 0;
    statePtr->myPort		= 0;
    statePtr->destIpAddr	= 0;
    statePtr->destPort		= 0;

    DppSetupSocketEvents(statePtr, sock, async, FALSE);

    sprintf(channelName, "tcp%d", tcpCount++);
    statePtr->channel = Tcl_CreateChannel(&tcpChannelType, channelName, 
		    (ClientData)statePtr, TCL_READABLE | TCL_WRITABLE);

    statePtr->sockInfo->channel = statePtr->channel;
    return statePtr->channel;
    
error:
    /*
     * Call DppGetErrno() to translate the error into a POSIX code.
     */
    DppGetErrno();
    if (interp != NULL) {
	Tcl_AppendResult(interp, "couldn't open socket: ",
        	Tcl_PosixError(interp), (char *) NULL);
    }
    if (statePtr != NULL) {
	ckfree((char*)statePtr);
    }
    if (sock != -1) {
	close(sock);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateServerChannel --
 *
 *	This function opens a new socket in server mode and
 *	initializes the TcpState structure.
 *
 * Results:
 *	Returns a new TcpState, or NULL with an error in interp->result,
 *	if interp is not NULL.
 *
 * Side effects:
 *	Opens a socket.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Channel
CreateServerChannel(interp, myIpAddr, myPort)
    Tcl_Interp *interp;		/* For error reporting; can be NULL. */
    int myIpAddr;		/* Address of the server. */
    int myPort;			/* Port number to listen to */
{
    int status, sock, len;
    struct sockaddr_in mySockAddr;	/* socket address to use. */
    TcpState *statePtr = NULL;
    char channelName[20];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	goto error;
    }

    memset((char *)&mySockAddr, 0, sizeof(mySockAddr));
    mySockAddr.sin_family = AF_INET;
    if (myIpAddr == DP_INADDR_ANY) {
	mySockAddr.sin_addr.s_addr = INADDR_ANY; 
    } else {
	mySockAddr.sin_addr.s_addr = htonl(myIpAddr);
    }
    mySockAddr.sin_port = htons((unsigned short)myPort);

    status = bind(sock, (struct sockaddr *) &mySockAddr,
	    sizeof(mySockAddr));
    if (status >= 0) {
	status = listen(sock, DP_LISTEN_LIMIT);
    } 
    if (status < 0) {
	goto error;
    }

    /*
     * Allocate a new TcpState for this server socket.
     */

    statePtr 			= (TcpState *)ckalloc(sizeof(TcpState));
    statePtr->flags 		= IS_SERVER;
    statePtr->sock 		= sock;
    statePtr->sockFile 		= (ClientData)sock;
    statePtr->interp 		= interp;

    /*
     * These are set in DpSetAddress.
     */
    statePtr->destIpAddr 	= 0;
    statePtr->destPort 		= 0;
    statePtr->myIpAddr 		= 0;
    statePtr->myPort 		= 0;

    DppSetupSocketEvents(statePtr, sock, 0, TRUE);

    sprintf(channelName, "tcp%d", tcpCount++);
    statePtr->channel = Tcl_CreateChannel(&tcpChannelType, channelName, 
		    (ClientData)statePtr, TCL_READABLE | TCL_WRITABLE);

    statePtr->sockInfo->channel = statePtr->channel;

    return statePtr->channel;
    
error:
    /*
     * Call DppGetErrno() to translate the error into a POSIX code.
     */
    DppGetErrno();
    if (interp != NULL) {
	Tcl_AppendResult(interp, "error opening socket: ",
        	Tcl_PosixError(interp), (char *) NULL);
    }
    if (statePtr != NULL) {
	ckfree((char*)statePtr);
    }
    if (sock != -1) {
	close(sock);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * SetDefaultOptions --
 *
 *	Sets the default options for a Tcp socket.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Some options of this socket are changed.
 *
 *----------------------------------------------------------------------
 */
static int
SetDefaultOptions(interp, chan)
    Tcl_Interp *interp;
    Tcl_Channel chan;
{
    TcpState *statePtr = (TcpState *) Tcl_GetChannelInstanceData(chan);

    if (DpTcpSetSocketOption(statePtr, DP_RECV_BUFFER_SIZE,
	    DP_TCP_RECVBUFSIZE) != TCL_OK) {
	goto error;
    }
    if (DpTcpSetSocketOption(statePtr, DP_REUSEADDR, 1) != TCL_OK) {
	goto error;
    }
    if (DpTcpSetSocketOption(statePtr, DP_KEEP_ALIVE, 0) != TCL_OK) {
	goto error;
    }
    if (DpTcpSetSocketOption(statePtr, DP_SEND_BUFFER_SIZE,
	    DP_TCP_SENDBUFSIZE) != TCL_OK) {
	goto error;
    }

    if (Tcl_SetChannelOption(interp, chan, "-translation", "binary") !=
            TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_SetChannelOption(interp, chan, "-buffering", "none") !=
            TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_SetChannelOption(interp, chan, "-eofchar", "") != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_SetChannelOption(interp, chan, "-blocking", "yes") != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;

error:
    Tcl_AppendResult(interp, "couldn't set default options for socket: ",
            Tcl_PosixError(interp), (char *) NULL);
    return TCL_ERROR;

}

/*
 *--------------------------------------------------------------
 *
 * DpTcpSetSocketOption --
 *
 *	Sets a socket option.  Note that we hardcode a linger
 *	time of 30 seconds.
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
DpTcpSetSocketOption(statePtr, option, value)
    TcpState *statePtr;		/* (in) TcpState structure */
    int option;			/* (in) Option to set */
    int value;			/* (in) new value for option */
{
    int sock, result;
    struct linger l;

    sock = statePtr->sock;
    switch (option) {
      	case DP_BLOCK:
	    result = DppSetBlock(sock, value);
	    break;
      	case DP_KEEP_ALIVE:
	    result = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&value,
		    sizeof(value));
	    break;
      	case DP_LINGER:
	    l.l_onoff = value;
	    l.l_linger = 30;
	    result = setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&l,
		    sizeof(struct linger));
	    break;
      	case DP_RECV_BUFFER_SIZE:
	    result = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&value,
		    sizeof(value));
	    break;
      	case DP_REUSEADDR:
	    result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&value,
		    sizeof(value));
	    break;
      	case DP_SEND_BUFFER_SIZE:
	    result = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&value,
		    sizeof(value));
	    break;
      	default:
	    return EINVAL;
    }

    if (result != 0) {
	return DppGetErrno();
    }
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * DpTcpGetSocketOption --
 *
 *	Sets a socket option.  The allowable options for Tcp
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

static int
DpTcpGetSocketOption (statePtr, option, valuePtr)
    TcpState *statePtr;		/* (in) TcpState structure */
    int option;			/* (in) Option to set */
    int *valuePtr;		/* (out) current value of option */
{
    int sock, result, len, len1;
    struct linger l;

    *valuePtr = 0;
    sock = statePtr->sock;
    len = sizeof(int);
    len1 = 1;

    if (statePtr->myPort == 0) {
    	result = DpSetAddress(statePtr);
    	if (result != TCL_OK) {
	    return DppGetErrno();
	}
    }

    switch (option) {
	case DP_KEEP_ALIVE:
	    result = getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)valuePtr,
		    &len);
	    break;
	case DP_LINGER:
	    len = sizeof(struct linger);
	    result = getsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&l, &len);
	    *valuePtr = l.l_onoff;
	    break;
	case DP_RECV_BUFFER_SIZE:
	    result = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)valuePtr,
	   	    &len);
	    break;
	case DP_REUSEADDR:
	    result = getsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)valuePtr,
		    &len);
	    break;
	case DP_SEND_BUFFER_SIZE:
	    result = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)valuePtr,
	   	    &len);
	    break;
	case DP_MYPORT:
	    *valuePtr = statePtr->myPort;
	    return 0;
	case DP_REMOTEPORT:
	    *valuePtr = statePtr->destPort;
	    return 0;
	case DP_MYIPADDR:
	    *valuePtr = statePtr->myIpAddr;
	    return 0;
	case DP_REMOTEIPADDR:
	    *valuePtr = statePtr->destIpAddr;
	    return 0;
      	default:
	    return EINVAL;
    }

    if (result != 0) {
	return DppGetErrno();
    }
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * DpSetAddress --
 *
 *	Sets the local and remote address info of a socket.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 64
#endif

static int
DpSetAddress(statePtr)
    TcpState *statePtr;
{
    struct sockaddr_in destSockAddr;
    struct sockaddr_in mySockAddr;
    struct hostent *h;
    int len;
    int *ipAddrPtr;
    char host[MAXHOSTNAMELEN];

    /*
     * Set the destination port and addr only if
     * we are not a server socket.  They are set to
     * zero in CreateServerChannel otherwise.
     */    
    len = sizeof(struct sockaddr_in);
    if (!(statePtr->flags & IS_SERVER)) {
	if (getpeername(statePtr->sock, (struct sockaddr *)&destSockAddr, 
		&len) != 0) {
	    return TCL_ERROR;
	}
	statePtr->destIpAddr = ntohl(destSockAddr.sin_addr.s_addr);
	statePtr->destPort   = ntohs((unsigned short)destSockAddr.sin_port);
    }

    /*
     * Set the local port and IP address
     */
    len = sizeof(struct sockaddr_in);
    if (getsockname(statePtr->sock, (struct sockaddr *)&mySockAddr, 
	    &len) != 0) {
	return TCL_ERROR;
    }
    if (gethostname(host, sizeof(host)) != 0) {
	return TCL_ERROR;
    }
    h = gethostbyname(host);
    if (h == NULL) {
	return TCL_ERROR;
    }
    ipAddrPtr = &(statePtr->myIpAddr);
    memcpy ((char *)ipAddrPtr, (char *) h->h_addr_list[0],
	   (size_t) h->h_length);
    *ipAddrPtr = ntohl(*ipAddrPtr);
    statePtr->myPort = ntohs((unsigned short)mySockAddr.sin_port);
    return TCL_OK;
}

