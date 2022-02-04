/*
 * unix/dpSock.c --
 *
 *  This file implements generic Unix-specific routines for the
 *  socket code.
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include <fcntl.h>
#include "generic/dpInt.h"



/*
 *--------------------------------------------------------------
 *
 * DppCloseSocket --
 *
 *	Close the socket passed in.
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
DppCloseSocket (sock)
    DpSocket sock;
{
    return close(sock);
}


/*
 *--------------------------------------------------------------
 *
 * DppSetBlock --
 *
 *	Put the socket into a blocking or non-blocking state.
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
    int flags;
    flags = fcntl(sock, F_GETFL, 0);
    if (block) {
	/* Set blocking mode */
	fcntl(sock, F_SETFL, flags & ~NBIO_FLAG);
    } else {
	/* Set non-blocking mode */
	fcntl(sock, F_SETFL, flags | NBIO_FLAG);
    }
    return 0;
}

/*
 * -------------------------------------------------------------
 *
 * DppGetErrno --
 *
 *	Returns the POSIX error code.
 *
 * Results:
 *	The POSIX errorcode.
 *
 * Side Effects:
 *	None.
 *
 * -------------------------------------------------------------
 */

int
DppGetErrno()
{
    return Tcl_GetErrno();
}

#ifndef _TCL76


/*
 *--------------------------------------------------------------
 *
 * SockBlockMode --
 *
 *	Sets the tcp socket to blocking or non-blocking.  Just
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

int
SockBlockMode (instanceData, mode)
    ClientData instanceData;	/* Pointer to tcpState struct */
    int mode;			/* TCL_MODE_BLOCKING or TCL_MODE_NONBLOCKING */
{
    SocketState *statePtr = (SocketState *)instanceData;

    if (mode == TCL_MODE_BLOCKING) {
	return DppSetBlock(statePtr->sock, 1);
    } else {
	return DppSetBlock(statePtr->sock, 0);
    }
}

/*
 *--------------------------------------------------------------
 *
 * SockClose --
 *
 *	This function is called by the Tcl channel driver when the
 *	caller want to close the socket. It releases the instanceData
 *	and closes the scoket.  All queued output will have been flushed
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
    int result;

    SDBG(("Closing socket %d\n", statePtr->sock));

    result = DppCloseSocket(statePtr->sock);
    if ((result != 0) && (interp != NULL)) {
        DppGetErrno();
        Tcl_SetResult(interp, (char *) Tcl_PosixError(interp), TCL_STATIC);
    }

    /*
     * IPM only
     */
    if (statePtr->flags & SOCKET_IPM) {
	Tcl_DStringFree(&statePtr->groupList);
    }

    ckfree((char *)statePtr);
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * SockWatch --
 *
 *	Creates an event callback for this socket or deletes
 *	the current callback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The callback can do anything.
 *
 *--------------------------------------------------------------
 */

void
SockWatch (instanceData, mask)
    ClientData instanceData;
    int mask;
{
    SocketState *infoPtr = (SocketState *) instanceData;

    if (mask) {
	Tcl_CreateFileHandler(infoPtr->sock, mask,
		(Tcl_FileProc *) Tcl_NotifyChannel,
		(ClientData) infoPtr->channel);
	SDBG(("Creating callback for socket %d\n", infoPtr->sock));
    } else {
	Tcl_DeleteFileHandler(infoPtr->sock);
	SDBG(("Deleting callback for socket %d\n", infoPtr->sock));
    }
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
int
UdpIpmOutput (instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to udpState struct */
    CONST84 char *buf;			/* (in) Buffer to write */
    int toWrite;		/* (in) Number of bytes to write */
    int *errorCodePtr;		/* (out) POSIX error code (if any) */
{
    SocketState *statePtr = (SocketState *) instanceData;
    int result;

    result = sendto(statePtr->sock, buf, toWrite, 0,
	    (struct sockaddr *) &statePtr->sockaddr,
	    sizeof(statePtr->sockaddr));
    if (result == DP_SOCKET_ERROR) {
	*errorCodePtr = DppGetErrno();
    }
    return result;
}
#endif
