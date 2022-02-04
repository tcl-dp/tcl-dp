/*
 * dpIPM.c --
 *
 *	This file implements the generic code for an IP multicasting
 *	channel driver. These are channels that are created by
 *	evaluating "dp_connect ipm".
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "generic/dpInt.h"


#ifndef IN_MULTICAST
/*
 * This just checks to make sure the IP address is a valid
 * IP multicast address: 224.0.0.0 < addr < 239.255.255.255
 */
#define IN_MULTICAST(i)         (((i) & 0xf0000000) == 0xe0000000)
#endif

/*
 * The default send and receive buffer size.
 */
#define DP_IPM_SENDBUFSIZE 8192
#define DP_IPM_RECVBUFSIZE 8192

typedef SocketState IpmState;

#define	PEEK_MODE	(1<<1)	/* Read without consuming? */
#define	ASYNC_CONNECT	(1<<2)	/* Asynchronous connection? */
#define	IS_SERVER	(1<<3)	/* Is this a server Ipm socket? */

/*
 * Procedures that are used in this file only.
 */

static IpmState *	CreateIPMSocket _ANSI_ARGS_((Tcl_Interp *interp,
			    int ipAddr, int port, int ttl));
static int 		IpmBlockMode _ANSI_ARGS_((ClientData instanceData,
			    int mode));
static int 		IpmInput _ANSI_ARGS_((ClientData instanceData,
                            char *buf, int bufSize, int *errorCodePtr));
static int		IpmOutput _ANSI_ARGS_((ClientData instanceData,
                            CONST84 char *buf, int toWrite, int *errorCodePtr));
static int		IpmClose _ANSI_ARGS_((ClientData instanceData,
			    Tcl_Interp *interp));
static int		IpmSetOption _ANSI_ARGS_((ClientData instanceData,
                            Tcl_Interp *interp, CONST char *optionName,
                            CONST char *optionValue));
static int		IpmGetOption _ANSI_ARGS_((ClientData instanceData,
			    CONST84 char *optionName, Tcl_DString *dsPtr));

static void		IpmWatch _ANSI_ARGS_((ClientData instanceData,
			    int mask));
static int		IpmReady _ANSI_ARGS_((ClientData instanceData,
			    int mask));
static Tcl_File	        IpmGetFile _ANSI_ARGS_((ClientData instanceData,
			    int direction));

static Tcl_ChannelType ipmChannelType = {
     "ipm",		/* Name of channel */
     DP_CHANNEL_VERSION,	/* TCL_CHANNEL_VERSION_1, TCL_CHANNEL_VERSION_2, and so on */
     IpmClose,		/* Proc to close a socket */
     IpmInput,		/* Proc to get input from a socket */
     IpmOutput,		/* Proc to send output to a socket */
     NULL,              /* Can't seek on a socket! */
     IpmSetOption,	/* Proc to set a socket option */
     IpmGetOption,	/* Proc to set a socket option */
     IpmWatch,		/* Proc called to set event loop wait params */
     IpmGetFile		/* Proc to return a handle assoc with socket */
     NULL,			/* Proc to call to close the channel if the device
					 * supports closing the read & write sides */
     IpmBlockMode,	/* Proc to set blocking mode on socket */
     /* Only valid in TCL_CHANNEL_VERSION_2 channels or later */
     NULL,			/* Proc to call to flush a channel */
     NULL,			/* Proc to call to handle a channel event */
     /* Only valid in TCL_CHANNEL_VERSION_3 channels or later */
     NULL,			/* Proc to call to seek on the channel which can handle 64-bit offsets */
     /* Only valid in TCL_CHANNEL_VERSION_4 channels or later */
     NULL			/* Proc to notify the driver of thread specific activity for a channel */
};

static int ipmCount = 0;        /* Number of ipm files opened -- used to
                                 * generate unique ids for channels */


/*
 *--------------------------------------------------------------
 *
 *  DpOpenIpmChannel --
 *
 *	Opens a new channel that uses the IPM protocol.
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
DpOpenIpmChannel(interp, argc, argv)
    Tcl_Interp *interp;		/* For error reporting; can be NULL. */
    int argc;			/* Number of arguments. */
    CONST84 char **argv;                /* Argument strings. */
{
#ifdef NO_MULTICAST_DEF
    Tcl_AppendResult(interp, "IP multicast is not available on this system",
	    NULL);
    return NULL;
#else
    Tcl_Channel chan;
    IpmState *statePtr = NULL;
    char channelName[20];
    char *groupName, *str;
    char **av;
    int i, ac;

    /*
     * The default values for the value-option pairs
     */
    int ipAddr = 0;
    int port   = 0;
    int ttl    = 1;

    /*
     * Flags to indicate that a certain option has been set by the
     * command line
     */
    int setGroup  = 0;
    int setPort   = 0;

    for (i=0; i<argc; i+=2) {
        int v = i+1;
	size_t len = strlen(argv[i]);

	if (strncmp(argv[i], "-group", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (!DpHostToIpAddr(argv[v], &ipAddr)) {
		Tcl_AppendResult (interp, "Illegal value for -group \"",
			argv[v], "\"", NULL);
		return NULL;
	    }
	    groupName = argv[v];
	    setGroup = 1;
	} else if (strncmp(argv[i], "-myport", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (Tcl_GetInt(interp, argv[v], &port) != TCL_OK) {
		return NULL;
	    }
	    if (port < 0) {
		Tcl_AppendResult(interp, "expected non-negative integer ",
		        "but got \"", argv[v], "\"", NULL);
		return NULL;
	    }
	    setPort = 1;
	} else if (strncmp(argv[i], "-ttl", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (Tcl_GetInt(interp, argv[v], &ttl) != TCL_OK) {
		return NULL;
	    }
	} else {
    	    Tcl_AppendResult(interp, "unknown option \"",
		    argv[i], "\", must be -group, ",
		    "-myport or -ttl", NULL);
	    return NULL;
	}
    }

    /*
     * Check the options that must or must not be specified, depending on
     * the -server option.
     */

    if (!setGroup) {
	Tcl_AppendResult(interp, "option -group must be specified",
		NULL);
	return NULL;
    }
    if (!setPort) {
	Tcl_AppendResult(interp, "option -myport must be specified",
		NULL);
	return NULL;
    }

    /*
     * Create a new socket and wrap it in a channel.
     */
    statePtr = CreateIPMSocket(interp, ipAddr, port, ttl);
    if (statePtr == NULL) {
	return NULL;
    }
    sprintf(channelName, "ipm%d", ipmCount++);
    chan = Tcl_CreateChannel(&ipmChannelType, channelName,
	    (ClientData)statePtr, TCL_READABLE|TCL_WRITABLE);
    Tcl_RegisterChannel(interp, chan);

    ac = 1;
    av = &groupName;
    str = Tcl_Merge(ac, av);
    Tcl_DStringAppendElement(&statePtr->groupList, str);
    ckfree(str);

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
#endif
}

/*
 *--------------------------------------------------------------
 *
 * IpmBlockMode --
 *
 *	Sets the ipm socket to blocking or non-blocking.  Just
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
IpmBlockMode (instanceData, mode)
    ClientData instanceData;	/* Pointer to ipmState struct */
    int mode;			/* TCL_MODE_BLOCKING or TCL_MODE_NONBLOCKING */
{
    IpmState *statePtr = (IpmState *)instanceData;

    if (mode == TCL_MODE_BLOCKING) {
	return DpIpmSetSocketOption(statePtr, DP_BLOCK, 1);
    } else {
	return DpIpmSetSocketOption(statePtr, DP_BLOCK, 0);
    }
}

/*
 *--------------------------------------------------------------
 *
 * IpmClose --
 *
 *	This function is called by the Tcl channel driver when the
 *	caller want to close the socket. It releases the instanceData
 *	and closes the scoket All queued output will have been flushed
 *	to the device before this function is called.
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
IpmClose (instanceData, interp)
    ClientData instanceData;	/* (in) Pointer to ipmState struct */
    Tcl_Interp *interp;		/* (in) For error reporting */
{
    IpmState *statePtr = (IpmState *)instanceData;
    int result;

    result = DppCloseSocket(statePtr->sock);
    if ((result != 0) && (interp != NULL)) {
        DppGetErrno();
        Tcl_SetResult(interp, Tcl_PosixError(interp), TCL_STATIC);
    }
    Tcl_DStringFree(&statePtr->groupList);
    ckfree((char *)statePtr);
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * IpmInput --
 *
 *	This function is called by the Tcl channel driver whenever the
 *	user wants to get input from the IPM socket. If the socket
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
IpmInput(instanceData, buf, bufSize, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to ipmState struct */
    char *buf;			/* (in/out) Buffer to fill */
    int bufSize;		/* (in) Size of buffer */
    int *errorCodePtr;		/* (out) POSIX error code (if any) */
{
    IpmState *statePtr = (IpmState *)instanceData;
    DpSocketAddressIP fromAddr;
    int bytesRead, fromLen;
    unsigned int fromHost, fromPort;
    char str[64];

    fromLen = sizeof(fromAddr);
    bytesRead = recvfrom(statePtr->sock, buf, bufSize, 0,
	    (struct sockaddr *)&fromAddr, &fromLen);
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
 * IpmOutput --
 *
 *	This function is called by the Tcl channel driver whenever the
 *	user wants to send output to the IPM socket. The function
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
IpmOutput (instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to ipmState struct */
    CONST84 char *buf;			/* (in) Buffer to write */
    int toWrite;		/* (in) Number of bytes to write */
    int *errorCodePtr;		/* (out) POSIX error code (if any) */
{
    IpmState *statePtr = (IpmState *)instanceData;
    int result;

    result = sendto(statePtr->sock, buf, toWrite, 0,
	    (struct sockaddr *)&statePtr->sockaddr,
	    sizeof(statePtr->sockaddr));
    if (result == DP_SOCKET_ERROR) {
	*errorCodePtr = DppGetErrno();
	return -1;
    }
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * IpmSetOption --
 *
 *	This function is called by the Tcl channel driver
 *	whenever Tcl evaluates and fconfigure call to set
 *	some property of the ipm socket (e.g., the buffer
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
IpmSetOption (instanceData, interp, optionName, optionValue)
    ClientData instanceData;
    Tcl_Interp *interp;
    CONST char *optionName;
    CONST char *optionValue;
{
    int option;
    int value;
    char c;
    int rc;
    IpmState *statePtr = (IpmState *)instanceData;

    /*
     * Set the option specified by optionName
     */
    if (optionName[0] == '-') {
        option = DpTranslateOption(optionName+1);
    } else {
        option = -1;
    }
    switch (option) {
      	case DP_REUSEADDR:
            if (Tcl_GetBoolean(interp, optionValue, &value) != TCL_OK) {
	    	return TCL_ERROR;
            }
            return DpIpmSetSocketOption(statePtr, option, value);

      	case DP_RECV_BUFFER_SIZE:
      	case DP_SEND_BUFFER_SIZE:
            if (Tcl_GetInt(interp, optionValue, &value) != TCL_OK) {
	    	return TCL_ERROR;
            }
            if (value <=0) {
                Tcl_AppendResult(interp, "Buffer size must be > 0", NULL);
                return TCL_ERROR;
            }
            return DpIpmSetSocketOption(statePtr, option, value);
        case DP_GROUP:
	    c = optionValue[0];
	    if (c != '+' && c != '-') {
		Tcl_AppendResult (interp, "Expected an add/drop token.  ",
			"Please see docs on how to add/drop a group.", NULL);
		return TCL_ERROR;
	    }
	    if (DpHostToIpAddr (&optionValue[1], &value) == 0) {
		Tcl_AppendResult (interp,
			"Expected IP address or hostname but got \"",
			optionValue, "\"", NULL);
		return TCL_ERROR;
	    }
	    if (c == '+') {
	    	option = DP_ADD_MEMBERSHIP;
	    } else {
	    	option = DP_DROP_MEMBERSHIP;
	    }
	    rc = DpIpmSetSocketOption(statePtr, option, value);
	    if (rc == 0) {
		/*
		 * Update the group list
		 */
		if (option == DP_ADD_MEMBERSHIP) {
		    Tcl_DStringAppendElement(&statePtr->groupList,
			    &optionValue[1]);
		} else {
		    int argc, i, j = 0;
		    char **argv;

		    Tcl_SplitList(interp,
			    Tcl_DStringValue(&statePtr->groupList),
			    &argc, &argv);
		    for (i=0;i<argc;i++) {
			if (!strcmp(argv[i], &optionValue[1])) {
			    while (i<argc) {
				argv[i] = argv[i+1];
				i++;
			    }
			    j = 1;
			    break;
			}
		    }
		    if (!j) {
			Tcl_AppendResult(interp, "Group address not found",
				"in list", NULL);
			return TCL_ERROR;
		    } else {
			Tcl_DStringFree(&statePtr->groupList);
			Tcl_DStringInit(&statePtr->groupList);
			Tcl_DStringAppend(&statePtr->groupList,
				Tcl_Merge(argc-1, argv), -1);
		    }
		}
	    }
	    return rc;
	case DP_MULTICAST_LOOP:
#ifdef _WIN32
	    Tcl_AppendResult(interp, "Loopback may not be turned off in Windows.",
		    NULL);
	    return TCL_ERROR;
#else
            if (Tcl_GetInt(interp, optionValue, &value) != TCL_OK) {
	    	return TCL_ERROR;
            }
	    return DpIpmSetSocketOption(statePtr, DP_MULTICAST_LOOP, value);
#endif
	case DP_MYPORT:
	    Tcl_AppendResult(interp, "Port may not be changed",
		    " after creation.", NULL);
	    return TCL_ERROR;

      	default:
            Tcl_AppendResult (interp, "bad option \"", optionName,
                    "\": must be -recvBuffer, -reuseAddr, -group, ",
                    "-sendBuffer or a standard fconfigure option", NULL);
            return TCL_ERROR;
    }
}

/*
 *--------------------------------------------------------------
 *
 * IpmGetOption --
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
IpmGetOption (instanceData,
		optionName, dsPtr)
    ClientData instanceData;
    CONST84 char *optionName;
    Tcl_DString *dsPtr;
{
    int option;
    unsigned int value = 0xFFFFFFFF;
    char str[256];
    IpmState *statePtr = (IpmState *)instanceData;

    /*
     * If optionName is NULL, then return all options in option-value
     * pairs.
     */

    if (optionName == NULL) {
	Tcl_DStringAppend (dsPtr, " -recvBuffer ", -1);
	IpmGetOption(instanceData, "-recvBuffer", dsPtr);
	Tcl_DStringAppend (dsPtr, " -reuseAddr ", -1);
	IpmGetOption(instanceData, "-reuseAddr", dsPtr);
	Tcl_DStringAppend (dsPtr, " -sendBuffer ", -1);
	IpmGetOption(instanceData, "-sendBuffer", dsPtr);
	Tcl_DStringAppend (dsPtr, " -loopback ", -1);
	IpmGetOption(instanceData, "-loopback", dsPtr);
	Tcl_DStringAppend (dsPtr, " -group ", -1);
	IpmGetOption(instanceData, "-group", dsPtr);
	Tcl_DStringAppend (dsPtr, " -myport ", -1);
	IpmGetOption(instanceData, "-myport", dsPtr);
        return TCL_OK;
    }

    /*
     * Retrive the value of the option specified by optionName
     */

    if (optionName[0] == '-') {
        option = DpTranslateOption(&optionName[1]);
    } else {
        option = -1;
    }

    switch (option) {
      	case DP_RECV_BUFFER_SIZE:
      	case DP_SEND_BUFFER_SIZE:
            if (DpIpmGetSocketOption(statePtr, option, &value) != 0) {
		return TCL_ERROR;
	    }
            sprintf (str, "%d", value);
            Tcl_DStringAppend(dsPtr, str, -1);
            break;
      	case DP_REUSEADDR:
            if (DpIpmGetSocketOption(statePtr, option, &value) != 0) {
		return TCL_ERROR;
	    }
            if (value) {
                /*
                 * Some systems returns a non-zero value (not necessarily 1)
                 * to indicate "true".
                 */
                value = 1;
            }
            sprintf (str, "%d", value);
            Tcl_DStringAppend(dsPtr, str, -1);
            break;
	case DP_GROUP:
	    Tcl_DStringAppend(dsPtr, Tcl_DStringValue(&statePtr->groupList),
		    Tcl_DStringLength(&statePtr->groupList));
	    break;

	case DP_MYPORT:
	    sprintf(str, "%d", statePtr->groupPort);
	    Tcl_DStringAppend(dsPtr, str, -1);
	    break;

	case DP_MULTICAST_LOOP:
#ifdef _WIN32
	    Tcl_DStringAppend(dsPtr, "1", -1);
#else
	    if (DpIpmGetSocketOption(statePtr, option, &value) != 0) {
	    	return TCL_ERROR;
	    }
	    sprintf(str, "%d", value);
            Tcl_DStringAppend(dsPtr, str, -1);
#endif
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
 * IpmWatch --
 *
 *	Gives a short overview (a few sentences), what other
 *	functions are related to this one.
 *
 *	All changes to the module made that decrease resource usage,
 *	but make the function harder to understand, modify, and debug.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Global variables touched.
 *	I/O operations performed.
 *	Delayed effects.
 *
 *--------------------------------------------------------------
 */

static void
IpmWatch (instanceData, mask)
    ClientData instanceData;
    int mask;
{
    IpmState *infoPtr = (IpmState *) instanceData;

    Tcl_WatchFile(infoPtr->sockFile, mask);
}

/*
 *--------------------------------------------------------------
 *
 * IpmReady --
 *
 *	Called by the notifier to check whether events of interest are
 *	present on the channel.
 *
 * Results:
 *	Returns OR-ed combination of TCL_READABLE, TCL_WRITABLE and
 *	TCL_EXCEPTION to indicate which events of interest are present.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static int
IpmReady (instanceData, mask)
    ClientData instanceData;
    int mask;
{
    IpmState *statePtr = (IpmState *) instanceData;

    return Tcl_FileReady(statePtr->sockFile, mask);
}


/*
 *--------------------------------------------------------------
 *
 * IpmGetFile --
 *
 *	Called from Tcl_GetChannelFile to retrieve the file handle
 *      from inside a IPM socket based channel.
 *
 * Results:
 *	TCL_OK always
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */
static Tcl_File
IpmGetFile(instanceData, direction)
    ClientData instanceData;
    int direction;
{
    IpmState *statePtr = (IpmState *)instanceData;

    return statePtr->sockFile;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateIPMSocket --
 *
 *	This function opens a new socket in client mode
 *	and initializes the IpmState structure.
 *
 * Results:
 *	Returns a new IpmState, or NULL with an error in interp->result,
 *	if interp is not NULL.
 *
 * Side effects:
 *	Opens a socket.
 *
 *----------------------------------------------------------------------
 */

static IpmState *
CreateIPMSocket(interp, group, port, ttl)
    Tcl_Interp *interp;		/* For error reporting; can be NULL. */
    int group;			/* IP address of the multicast group. */
    int port;			/* Port number. */
    int ttl;			/* Time to live value. */
{
    IpmState * statePtr = NULL;
    DpSocket sock;

    if (!IN_MULTICAST(group)) {
	Tcl_AppendResult(interp, "No such IP multicast group", NULL);
	return NULL;
    }

    /*
     * Create the socket
     */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == DP_SOCKET_ERROR) {
	goto error;
    }

    statePtr		= (IpmState *)ckalloc(sizeof(IpmState));
    statePtr->interp	= interp;
    statePtr->groupPort	= port;
    statePtr->groupAddr	= group;
    statePtr->sock	= sock;
    statePtr->sockFile	= Tcl_GetFile((ClientData)sock, DP_SOCKET);

    Tcl_DStringInit(&statePtr->groupList);

    /*
     * Bind the socket
     */
    memset(&statePtr->sockaddr, 0, sizeof(statePtr->sockaddr));
    statePtr->sockaddr.sin_family      = AF_INET;
    statePtr->sockaddr.sin_port        = htons((unsigned short) port);
    statePtr->sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (DpIpmSetSocketOption(statePtr, DP_REUSEADDR, 1) != 0) {
	goto error;
    }

    if (bind(sock, (struct sockaddr *) &statePtr->sockaddr,
	    sizeof(statePtr->sockaddr)) != 0) {
	goto error;
    }

    statePtr->sockaddr.sin_addr.s_addr = htonl(group);
    statePtr->groupPort = (int) ntohs(statePtr->sockaddr.sin_port);

    /*
     * On some older machines, we need to ask for multicast
     * permission.
     */

    if (DpIpmSetSocketOption(statePtr, DP_BROADCAST, 1) != 0) {
	goto error;
    }

    /*
     * Make this an IPM socket by setting the socket options. Also, set
     * other default options of this socket, such as buffer size.
     */

    if (DpIpmSetSocketOption(statePtr, DP_ADD_MEMBERSHIP, group) != 0) {
	goto error;
    }
    if (DpIpmSetSocketOption(statePtr, DP_MULTICAST_TTL, ttl) != 0) {
	goto error;
    }
    if (DpIpmSetSocketOption(statePtr, DP_MULTICAST_LOOP, 1) != 0) {
	goto error;
    }
    if (DpIpmSetSocketOption(statePtr, DP_RECV_BUFFER_SIZE,
	    DP_IPM_RECVBUFSIZE) != 0) {
	goto error;
    }
    if (DpIpmSetSocketOption(statePtr, DP_SEND_BUFFER_SIZE,
	    DP_IPM_SENDBUFSIZE) != 0) {
	goto error;
    }

    return statePtr;

error:
    /*
     * Translate Windows Socket error to POSIX errorcode
     */
    DppGetErrno();
    Tcl_AppendResult(interp, "Error creating IPM socket: ",
            Tcl_PosixError(interp), NULL);

    if (statePtr) {
	if (statePtr->sock != DP_SOCKET_ERROR) {
	    DppCloseSocket(statePtr->sock);
	}
	ckfree((char*)statePtr);
    }
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DpIpmSetSocketOption --
 *
 *	Sets a socket option.  The allowable options for Ipm
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
DpIpmSetSocketOption(statePtr, option, value)
    IpmState *statePtr;		/* (in) IpmState structure */
    int option;			/* (in) Option to set */
    int value;			/* (in) new value for option */
{
    int sock, result = -2;
    struct ip_mreq mreq;
    struct linger l;

    /*
     * NT 3.5 seems to have a bug when passing a size of char down.
     * It comes back with an error indicating a segfault.  When
     * increased to the size of an int, everything seems to be OK.
     * Pad on both sides of the value so NT doesn't segfault.
     */

    unsigned char buf[8], *p;

    p = buf+sizeof(int);
    *p = (unsigned char)value;

    sock = statePtr->sock;
    switch (option) {
    	case DP_ADD_MEMBERSHIP:
	    if (!IN_MULTICAST(value)) {
	    	Tcl_SetErrno(EINVAL);
	    	return -1;
	    }
            mreq.imr_multiaddr.s_addr = htonl(value);
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            result = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq,
		    sizeof(mreq));
            break;
    	case DP_DROP_MEMBERSHIP:
            mreq.imr_multiaddr.s_addr = htonl(value);
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            result = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *)&mreq,
		    sizeof(mreq));
            break;
      	case DP_BLOCK:
            result = DppSetBlock (sock, value);
            break;
        case DP_BROADCAST:
            result = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&value,
                    sizeof(value));
            break;
      	case DP_MULTICAST_TTL:
#ifdef _WIN32
	    result = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&value,
		    sizeof(int));
#else
	    result = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)p,
		    sizeof(unsigned char));
#endif
            break;
      	case DP_MULTICAST_LOOP:
#ifndef _WIN32
            result = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP,(char*)p,
		    sizeof(unsigned char));
#else
            result = 0;
#endif
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
 * DpIpmGetSocketOption --
 *
 *	Sets a socket option.  The allowable options for Ipm
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
DpIpmGetSocketOption (statePtr, option, valuePtr)
    IpmState *statePtr;		/* (in) IpmState structure */
    int option;			/* (in) Option to set */
    int *valuePtr;		/* (out) current value of option */
{
    int sock, result, len;
    struct linger l;
    char p;

    sock = statePtr->sock;
    len = sizeof(int);
    switch (option) {
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
        case DP_MULTICAST_LOOP:
	    /*
	     * This call should only happen in Unix since it is invalid
	     * under Windows.  IpmGetOption should have caught it.
	     */
	    len = sizeof(unsigned char);
            result = getsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP,(char*)&p,
		    &len);
	    *valuePtr = p;
	    break;

	default:
            return EINVAL;
    }

    if (result != 0) {
        return DppGetErrno();
    }
    return 0;
}
