/*
 * win/dpSock.c --
 *
 *  This file implements the few windows-specific routines for the
 *  socket code.
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "generic/dpInt.h"
#include "generic/dpPort.h"

#ifndef _TCL76

SocketInfo *dpSocketList;
int initd = FALSE;
HWND hwnd;

/*
 *--------------------------------------------------------------
 *
 * DppSetupSocketEvents --
 *
 *	Initializes a new socket structure and the events
 *	we are interested in handling.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	This puts the socket in non-blocking mode
 *
 *--------------------------------------------------------------
 */

void 
DppSetupSocketEvents(statePtr, sock, async, server)
    SocketState *statePtr;
    DpSocket sock;
    int async;
    int server;
{
    SocketInfo *infoPtr;

    infoPtr = NewSocketInfo(sock);
    statePtr->sockInfo = infoPtr;

    /*
     * Set up the select mask for read/write events.  If the connect
     * attempt has not completed, include connect events.
     */

    if (server) {
	infoPtr->selectEvents = FD_ACCEPT;
	infoPtr->watchEvents |= FD_ACCEPT;
    } else {
	infoPtr->selectEvents = FD_READ | FD_WRITE | FD_CLOSE;
	infoPtr->watchEvents |= FD_READ | FD_WRITE | FD_CLOSE;
	if (async) {
	    infoPtr->flags |= SOCKET_ASYNC_CONNECT;
	    infoPtr->selectEvents |= FD_CONNECT;
	}
    }

    /*
     * Register for interest in events in the select mask.  Note that 
     * automatically places the socket into non-blocking mode.
     */

    (void) WSAAsyncSelect(infoPtr->socket, hwnd,
	    SOCKET_MESSAGE, infoPtr->selectEvents);
}

/*
 *--------------------------------------------------------------
 *
 * SocketSetupProc --
 *
 *	Scans for a ready socket.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Might set the event loop to poll.
 *
 *--------------------------------------------------------------
 */

void
SocketSetupProc(data, flags)
    ClientData data;		/* Not used. */
    int flags;			/* Event flags as passed to Tcl_DoOneEvent. */
{
    SocketInfo *infoPtr;
    Tcl_Time blockTime = { 0, 0 };

    if (!(flags & TCL_FILE_EVENTS)) {
	return;
    }
    
    /*
     * Check to see if there is a ready socket.  If so, poll.
     */

    for (infoPtr = dpSocketList; infoPtr != NULL; 
	    infoPtr = infoPtr->nextPtr) {
	if (infoPtr->readyEvents & infoPtr->watchEvents) {
	    Tcl_SetMaxBlockTime(&blockTime);
	    break;
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * NewSocketInfo --
 *
 *	Creates and initialized a new socket structure.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Memory is allocated.
 *
 *--------------------------------------------------------------
 */

SocketInfo *
NewSocketInfo(socket)
    SOCKET socket;
{
    SocketInfo *infoPtr;

    infoPtr = (SocketInfo *) ckalloc(sizeof(SocketInfo));
    infoPtr->socket = socket;
    infoPtr->flags = 0;
    infoPtr->watchEvents = 0;
    infoPtr->readyEvents = 0;
    infoPtr->selectEvents = 0;
    infoPtr->lastError = 0;
    infoPtr->nextPtr = dpSocketList;
    dpSocketList = infoPtr;
    return infoPtr;
}

/*
 *--------------------------------------------------------------
 *
 * SocketCheckProc --
 *
 *	Finds a socket with an event to process and queues
 *	an event if one is found.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	An event handler might execute later.
 *
 *--------------------------------------------------------------
 */

void
SocketCheckProc(data, flags)
    ClientData data;		/* Not used. */
    int flags;			/* Event flags as passed to Tcl_DoOneEvent. */
{
    SocketInfo *infoPtr;
    SocketEvent *evPtr;

    if (!(flags & TCL_FILE_EVENTS)) {
	return;
    }
    
    /*
     * Queue events for any ready sockets that don't already have events
     * queued (caused by persistent states that won't generate WinSock
     * events).
     */

    for (infoPtr = dpSocketList; infoPtr != NULL; 
	    infoPtr = infoPtr->nextPtr) {
	if ((infoPtr->readyEvents & infoPtr->watchEvents)
		&& !(infoPtr->flags & SOCKET_PENDING)) {
	    infoPtr->flags |= SOCKET_PENDING;
	    evPtr = (SocketEvent *) ckalloc(sizeof(SocketEvent));
	    evPtr->header.proc = SocketEventProc;
	    evPtr->socket = infoPtr->socket;
	    Tcl_QueueEvent((Tcl_Event *) evPtr, TCL_QUEUE_TAIL);
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * WaitForSocketEvent --
 *
 *	Waits for one of the specified events to occur on the
 *	socket.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Blocks until event happens.
 *
 *--------------------------------------------------------------
 */

int
WaitForSocketEvent(infoPtr, events, errorCodePtr)
    SocketInfo *infoPtr;	/* Information about this socket. */
    int events;			/* Events to look for. */
    int *errorCodePtr;		/* Where to store errors? */
{
    MSG msg;
    int result = 1;
    int oldMode;

    /*
     * Be sure to disable event servicing so we are truly modal.
     */

    oldMode = Tcl_SetServiceMode(TCL_SERVICE_NONE);
    
    while (!(infoPtr->readyEvents & events)) {
	if (infoPtr->flags & SOCKET_ASYNC) {
	    if (!PeekMessage(&msg, hwnd, SOCKET_MESSAGE,
		    SOCKET_MESSAGE, PM_REMOVE)) {
		*errorCodePtr = EWOULDBLOCK;
		result = 0;
		break;
	    }
	} else {
	    /*
	     * Look for a socket event.  Note that we will be getting
	     * events for all of Tcl's sockets, not just the one we wanted.
	     */

	    result = GetMessage(&msg, hwnd, SOCKET_MESSAGE,
		    SOCKET_MESSAGE);
	    if (result == -1) {
		TclWinConvertError(GetLastError());
		*errorCodePtr = Tcl_GetErrno();
		result = 0;
		break;
	    }

	    /*
	     * I don't think we can get a WM_QUIT during a tight modal
	     * loop, but just in case...
	     */

	    if (result == 0) {
		panic("WaitForSocketEvent: Got WM_QUIT during modal loop!");
	    }
	}

	/*
	 * Dispatch the message and then check for an error on the socket.
	 */

	infoPtr->lastError = 0;
	DispatchMessage(&msg);
	if (infoPtr->lastError) {
	    *errorCodePtr = infoPtr->lastError;
	    result = 0;
	    break;
	}
    }

    (void) Tcl_SetServiceMode(oldMode);
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * SocketEventProc --
 *
 *	Translates the Windows socket event into a Tcl
 *	channel event and notifies the channel.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Channel handler will execute.
 *
 *--------------------------------------------------------------
 */
int
SocketEventProc(evPtr, flags)
    Tcl_Event *evPtr;		/* Event to service. */
    int flags;			/* Flags that indicate what events to
				 * handle, such as TCL_FILE_EVENTS. */
{
    SocketInfo *infoPtr;
    SocketEvent *eventPtr = (SocketEvent *) evPtr;
    int mask = 0;
    u_long nBytes;
    int status, events;

    if (!(flags & TCL_FILE_EVENTS)) {
	return 0;
    }

    /*
     * Find the specified socket on the socket list.
     */

    for (infoPtr = dpSocketList; infoPtr != NULL; infoPtr = infoPtr->nextPtr) {
	if (infoPtr->socket == eventPtr->socket) {
	    break;
	}
    }

    /*
     * Discard events that have gone stale.
     */

    if (!infoPtr) {
	return 1;
    }

    infoPtr->flags &= ~SOCKET_PENDING;

    /*
     * Handle connection requests directly.
     *
    */

    if (infoPtr->readyEvents & FD_ACCEPT) {
	mask |= TCL_READABLE;
	Tcl_NotifyChannel(infoPtr->channel, mask);
	return 1;
    }


    /*
     * Mask off unwanted events and compute the read/write mask so 
     * we can notify the channel.
     */
    events = infoPtr->readyEvents & infoPtr->watchEvents;

    if (events & FD_CLOSE) {
	/*
	 * If the socket was closed and the channel is still interested
	 * in read events, then we need to ensure that we keep polling
	 * for this event until someone does something with the channel.
	 * Note that we do this before calling Tcl_NotifyChannel so we don't
	 * have to watch out for the channel being deleted out from under
	 * us.  This may cause a redundant trip through the event loop, but
	 * it's simpler than trying to do unwind protection.
	 */

	Tcl_Time blockTime = { 0, 0 };
	Tcl_SetMaxBlockTime(&blockTime);
	mask |= TCL_READABLE;
    } else if (events & FD_READ) {
	/*
	 * We must check to see if data is really available, since someone
	 * could have consumed the data in the meantime.
	 */

	status = ioctlsocket(infoPtr->socket, FIONREAD, &nBytes);
	if (status != SOCKET_ERROR && nBytes > 0) {
	    mask |= TCL_READABLE;
	} else {
	    /*
	     * We are in a strange state, probably because someone
	     * besides Tcl is reading from this socket.  Try to
	     * recover by clearing the read event.
	     */
	    
	    infoPtr->readyEvents &= ~(FD_READ);

 	    /*
 	     * Re-issue WSAAsyncSelect() since we are gobbling up an
 	     * event,  without letting the reader do any I/O to re-enable
	     * the notification.
 	     */

 	    (void) WSAAsyncSelect(infoPtr->socket, hwnd,
 		    SOCKET_MESSAGE, infoPtr->selectEvents);
	}
    }
    if (events & FD_WRITE) {
	mask |= TCL_WRITABLE;
    }

    if (mask) {
	infoPtr->readyEvents &= ~FD_WRITE;
	Tcl_NotifyChannel(infoPtr->channel, mask);
    }
    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * SocketExitHandler --
 *
 *	Cleans up the entire DP socket subsystem.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Sockets are no longer viable.
 *
 *--------------------------------------------------------------
 */
void
SocketExitHandler(clientData)
    ClientData clientData;              /* Not used. */
{
    DestroyWindow(hwnd);
    UnregisterClass("DpSocket", TclWinGetTclInstance());
    Tcl_DeleteEventSource(SocketSetupProc, SocketCheckProc, NULL);
}

/*
 *--------------------------------------------------------------
 *
 * SocketProc --
 *
 *	This is the Windows callback which recv's
 *	notification of socket events.  It marks the
 *	socket and tells Tcl to service events.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Event handling will commence.
 *
 *--------------------------------------------------------------
 */
LRESULT CALLBACK
SocketProc(hwnd, message, wParam, lParam)
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
{
    int event, error;
    SOCKET socket;
    SocketInfo *infoPtr;

    if (message != SOCKET_MESSAGE) {
	return DefWindowProc(hwnd, message, wParam, lParam);
    }

    event = WSAGETSELECTEVENT(lParam);
    error = WSAGETSELECTERROR(lParam);
    socket = (SOCKET) wParam;

    /*
     * Find the specified socket on the socket list and update its
     * eventState flag.
     */

    for (infoPtr = dpSocketList; infoPtr != NULL; infoPtr = infoPtr->nextPtr) {
	if (infoPtr->socket == socket) {
	    /*
	     * Update the socket state.
	     */

	    if (event & FD_CLOSE) {
		infoPtr->readyEvents &= ~(FD_WRITE|FD_ACCEPT);
	    }
	    if (event & FD_CONNECT) {
		/*
		 * The socket is now connected, so clear the async connect
		 * flag.
		 */

		infoPtr->flags &= ~(SOCKET_ASYNC_CONNECT);

		/*
		 * Remember any error that occurred so we can report
		 * connection failures.
		 */

		if (error != ERROR_SUCCESS) {
		    TclWinConvertWSAError(error);
		    infoPtr->lastError = Tcl_GetErrno();
		}

	    } 
	    infoPtr->readyEvents |= event;
	    break;
	}
    }

    /*
     * Flush the Tcl event queue before returning to the event loop.
     */

    Tcl_ServiceAll();

    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * InitDpSockets --
 *
 *	Initializes DP's socket subsystem.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	A hidden window is created and a window class 
 *	is registered.
 *
 *--------------------------------------------------------------
 */
void 
InitDpSockets()
{
    WNDCLASS class;

    Tcl_CreateExitHandler(SocketExitHandler, (ClientData) NULL);

    /*
     * Create the async notification window with a new class.  We
     * must create a new class to avoid a Windows 95 bug that causes
     * us to get the wrong message number for socket events if the
     * message window is a subclass of a static control.
     */

    class.style = 0;
    class.cbClsExtra = 0;
    class.cbWndExtra = 0;
    class.hInstance = TclWinGetTclInstance();
    class.hbrBackground = NULL;
    class.lpszMenuName = NULL;
    class.lpszClassName = "DpSocket";
    class.lpfnWndProc = SocketProc;
    class.hIcon = NULL;
    class.hCursor = NULL;

    if (RegisterClass(&class)) {
	hwnd = CreateWindow("DpSocket", "DpSocket", WS_TILED, 0, 0,
		0, 0, NULL, NULL, class.hInstance, NULL);
    } else {
	hwnd = NULL;
    }
    if (hwnd == NULL) {
	TclWinConvertError(GetLastError());
	return;
    }
    Tcl_CreateEventSource(SocketSetupProc, SocketCheckProc, NULL);

    return;
}

/*
 *--------------------------------------------------------------
 *
 * SockWatch --
 *
 *	Sets up which events to watch for on this socket.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Events on this socket are now watched for.
 *
 *--------------------------------------------------------------
 */
void
SockWatch (instanceData, mask)
    ClientData instanceData;
    int mask;
{
    SocketState *statePtr = (SocketState *) instanceData;
    SocketInfo *infoPtr = (SocketInfo *) instanceData;

    /*
     * Update the watch events mask.
     */
    
    infoPtr->watchEvents = 0;
    if (mask & TCL_READABLE) {
	infoPtr->watchEvents |= (FD_READ|FD_CLOSE|FD_ACCEPT);
    }
    if (mask & TCL_WRITABLE) {
	infoPtr->watchEvents |= (FD_WRITE);
    }

    /*
     * If there are any conditions already set, then tell 
     * the notifier to poll rather than block.
     */

    if (infoPtr->readyEvents & infoPtr->watchEvents) {
	Tcl_Time blockTime = { 0, 0 };
	Tcl_SetMaxBlockTime(&blockTime);
    }
}

/*
 *--------------------------------------------------------------
 *
 * SockBlockMode --
 *
 *	Sets the socket to blocking or non-blocking.
 *
 * Results:
 *	Zero always.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */

int
SockBlockMode (instanceData, mode)
    ClientData instanceData;	/* Pointer to SocketState struct */
    int mode;			/* TCL_MODE_BLOCKING or TCL_MODE_NONBLOCKING */
{
    SocketState *statePtr = (SocketState *)instanceData;
    SocketInfo *infoPtr = statePtr->sockInfo;

    if (mode == TCL_MODE_NONBLOCKING) {
	infoPtr->flags |= SOCKET_ASYNC;
    } else {
	infoPtr->flags &= ~(SOCKET_ASYNC);
    }
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * SockClose --
 *
 *	This function is called by the Tcl channel driver when the
 *	caller want to close the socket. It releases the instanceData
 *	and closes the socket.  All queued output will have been flushed
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

int
SockClose (instanceData, interp)
    ClientData instanceData;	/* (in) Pointer to tcpState struct */
    Tcl_Interp *interp;		/* (in) For error reporting */
{
    SocketState *statePtr = (SocketState *)instanceData;
    SocketInfo *infoPtr = statePtr->sockInfo;
    SocketInfo **nextPtrPtr;
    int result;

    result = closesocket(statePtr->sock);
    if ((result != 0) && (interp != NULL)) {
        DppGetErrno();
        Tcl_SetResult(interp, Tcl_PosixError(interp), TCL_STATIC);
    }

    for (nextPtrPtr = &dpSocketList; (*nextPtrPtr) != NULL;
	 nextPtrPtr = &((*nextPtrPtr)->nextPtr)) {
	if ((*nextPtrPtr) == infoPtr) {
	    (*nextPtrPtr) = infoPtr->nextPtr;
	    break;
	}
    }

    /*
     * IPM only
     */

	if (statePtr->flags & SOCKET_IPM) {
		Tcl_DStringFree(&statePtr->groupList);
	}

    ckfree((char *) infoPtr);
    ckfree((char *) statePtr);
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * SockGetFile --
 *
 *	Called from Tcl_GetChannelFile to retrieve the handle 
 *	from inside a socket based channel.
 *
 * Results:
 *	TCL_OK
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */
int
SockGetFile(instanceData, direction, handlePtr)
    ClientData instanceData;
    int direction;
    FileHandle *handlePtr;
{
    SocketState *statePtr = (SocketState *)instanceData;

    *handlePtr = statePtr->sockFile;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * UdpIpmOutput --
 *
 *	This function is called by the Tcl channel driver whenever
 *	the user wants to send output to a UDP/IPM socket.
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
int
UdpIpmOutput(instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to udpState struct */
    char *buf;			/* (in) Buffer to write */
    int toWrite;		/* (in) Number of bytes to write */
    int *errorCodePtr;		/* (out) POSIX error code (if any) */
{
    SocketState *statePtr = (SocketState *) instanceData;
    SocketInfo *infoPtr = statePtr->sockInfo;
    int result;
    int error;

    while (1) {
#ifdef UDPDEBUG
	{
	    char msg[1024];
	    memcpy(msg, buf, toWrite);
	    msg[toWrite] = '\0';
	    printf("Sending UDP data: %s\n", msg);
	}
#endif
	result = sendto(statePtr->sock, buf, toWrite, 0,
		(struct sockaddr *) &statePtr->sockaddr, 
		sizeof(statePtr->sockaddr));
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
	    if (infoPtr->flags & SOCKET_ASYNC) {
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
#endif



/*
 *--------------------------------------------------------------
 *
 * DppSetBlock --
 *
 *	Put the socket into a blocking or non-blocking state.
 *	Note that the way Microsoft phrases it, turning off
 *	blocking requires a "1" (enabling non-blocking mode...)
 *	<sigh>
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
int
DppSetBlock (sock, block)
    DpSocket sock;
    int block;
{
    int result;
    u_long val = 0;
    
    if (block) {
	/* Set blocking mode */
	result = ioctlsocket(sock, FIONBIO, &val);
    } else {
	/* Set non-blocking mode */
	val = 1;
	result = ioctlsocket(sock, FIONBIO, &val);
    }
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * DppCloseSocket --
 *
 *	Closes the Windows socket.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
int DppCloseSocket(sock)
    DpSocket sock;
{
    return closesocket(sock);
}

/*
 *--------------------------------------------------------------
 *
 * DppGetErrno --
 *
 *	Returns the value of the errno variable for the last error
 *	that occurred.
 *
 * Results:
 *	POSIX error number.
 *
 * Side effects:
 *	Changes the Tcl global variable errno.
 *
 *--------------------------------------------------------------
 */
int
DppGetErrno ()
{
    int err;
    int posix;

    err = WSAGetLastError();
    TclWinConvertWSAError(err);
    posix = Tcl_GetErrno();
    return posix;
}
