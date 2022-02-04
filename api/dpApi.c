/* API to connect to a Tcl-DP server.  Applications that do not
 * include Tcl can use this API to contact a Tcl-DP server as a
 * client and send RPCs and RDOs for evaluation by the server.
 *
 * Since your application does not contain Tcl, it cannot act
 * as a Tcl-DP server.
 *
 * Note that unlike the Tcl and DP sources, this file requires
 * an ANSI C compiler.
 *
 * This API is extremely multithread unsafe.
 *
 * Original author: presumably Mike Perham, circa 1997
 *
 * Updated by John McGehee, www.voom.net 4/30/2010
 *   - Compile under Red Hat Enterprise Linux (RHEL) 5
 *   - Works with Tcl-DP 4.0 server running under Tcl 8.4
 *   - Fixed bugs
 */

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
	#include <windows.h>
#else /* Unix */
	#include <stdlib.h>
	#include <errno.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
#endif
#include "dpApi.h"

/*------------------ DP RPC protocol defines -----------------*/

#define TOK_RPC     		'e'
#define TOK_RDO     		'd'
#define TOK_RET     		'r'
#define TOK_ERR     		'x'

/*
 * The following strings are used to provide callback and/or error
 * catching for RDOs. c = callback, e = onerror, ce = both
 */
/*
 * sprintf template when both callback and onerror are specified.
 * Params are cmd, onerror, callback
 */
static char *ceCmdTemplate =
"if [catch {%s} dp_rv] {\
    dp_RDO $dp_rpcFile set errorInfo \"$errorInfo\n    while remotely executing\n%s\"; \
    dp_RDO $dp_rpcFile eval \"%s \\{$dp_rv\\}\"\
} else {\
    dp_RDO $dp_rpcFile eval \"%s \\{$dp_rv\\}\"\
}";

/*
 * sprintf template when just onerror is specified.
 *  Params are cmd, onerror
 */
static char *eCmdTemplate =
"if [catch {%s} dp_rv] {\
    dp_RDO $dp_rpcFile set errorInfo \"$errorInfo\n    while remotely executing\n%s\"; \
    dp_RDO $dp_rpcFile eval \"%s \\{$dp_rv\\}\"\
}";

/*
 * sprintf template when just callback is specified.
 *  Params are cmd, callback
 */
static char *cCmdTemplate =
"set dp_rv [%s]; dp_RDO $dp_rpcFile eval \"%s \\{$dp_rv\\}\"";

/*----------------------- Globals --------------------------*/

/*
 * Holds the latest message received.
 * bufPtr points to the free space
 */
#define DP_BUFFER_SIZE		8192

char retStr[DP_BUFFER_SIZE];
static char *bufPtr;

/*-------------------- Internal Routines -------------------*/

static int SendRPCMessage	(DPServer server, char token,
				    int id, const char *msgStr);

/*
 *--------------------------------------------------------------
 *
 * Dp_RPC --
 *
 *	Send an RPC message to the given server.
 *
 *	If tv is NULL, there is no timeout, otherwise
 *	the RPC will timeout once tv's amount of time
 *	has passed.
 *
 * Results:
 *	errorPtr is non-zero if there was an error.
 *
 *	Returns the Tcl result in a static string.
 *
 *	If there was an error, returns a string representing a two element Tcl list
 *	describing the error:
 *     	    {result} {$::errorInfo}
 *	This is different from the Tcl version of this command, which  uses
 *	Tcl_Split() to split the list, and return only the result value.  Here
 *	the Tcl API is unavailable, so Tcl_Split() is unavailable.  Instead of
 *	using a fake Tcl_Split() just for the sake of discarding information,
 *	Dp_RPC() returns the whole string,
 *	    {result} {$::errorInfo}
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

const char *
Dp_RPC(server, mesgStr, tv, errorPtr)
    DPServer server;		/* in: TCP socket to send on */
    const char *mesgStr;		/* in: RPC string to send */
    struct timeval *tv;		/* in: Timeout value for select() */
    int *errorPtr;		/* out: POSIX errorcode */
{
    int rc, len;
    int amtRecv = -1, totalAmt = 0;

    *errorPtr = 0;

    /*
     * Send the RPC to the remote server.  Note the 12 is
     * just a random ID since we don't use IDs.
     */
    rc = SendRPCMessage(server, TOK_RPC, 12, mesgStr);
    if (rc <= 0) {
    	*errorPtr = errno;
    	strcpy(retStr, "Error writing on socket");
    	return retStr;
    }

    /*
     * Now we want to recv the reply.
     * We spin in a loop waiting for the entire message
     * to arrive.  This gets a bit messy.
     */
    bufPtr = retStr;
    while (amtRecv < totalAmt) {
    	char lengthStr[7];

	rc = Dp_WaitForServer(server, tv);
	if (rc <= 0) {
	    if (rc == 0) {
		*errorPtr = -1;
		strcpy(retStr, "RPC timed out");
		return retStr;
	    } else {
		*errorPtr = errno;
		strcpy(retStr, "Select error");
		return retStr;
	    }
	}
	amtRecv = recv(server, bufPtr, (retStr + DP_BUFFER_SIZE) - bufPtr, 0);
	if ((amtRecv >= 6) && (totalAmt == 0)) {
	    /*
	     * Extract the length field from the incoming message
	     * so we know when we have recv'd the entire message.
	     */
	    strncpy(lengthStr, retStr, 6);
	    lengthStr[6] = '\0';
	    totalAmt = atoi(lengthStr);
	} else if (amtRecv == 0) {
	    /*
	     * EOF
	     */
	     break;
	}
	bufPtr += amtRecv;
    }

    if (retStr[7] == 'x') {
    	*errorPtr = -1;
    }
    len = totalAmt - 16;
    memcpy(retStr, &retStr[16], len);
    retStr[len] = '\0';
    return retStr;
}

/*
 *--------------------------------------------------------------
 *
 * Dp_RDOSend --
 *
 *	Send an RDO message to the given server.
 *	It is the caller's responsibilty to call
 *	Dp_RDORead if we need a return code or error.
 *	The size of the RDO is limited to BUFFER_SIZE - 1.
 *
 * Results:
 *	Amount sent.
 *
 * Side effects:
 *	Destroys previous value of retStr.
 *
 *--------------------------------------------------------------
 */

int
Dp_RDOSend(server, mesgStr, flags)
    DPServer server;
    const char *mesgStr;
    int flags;
{
    char bigBuf[8096];
    /*
     * This is our fake callback/error Tcl script.
     * This is necessary for compatibility with DP 4.0.
     * Note since we will never evaluate the return
     * script, this can be anything.
     */
    char *dummy = "mperham";
    int len, amt;

    switch (flags) {
    	case DP_REPORT_ERROR:
	    sprintf(bigBuf, eCmdTemplate, mesgStr, mesgStr, dummy);
	    break;
    	case DP_RETURN_VALUE:
	    sprintf(bigBuf, cCmdTemplate, mesgStr, mesgStr, dummy);
	    break;
    	case DP_RETURN_VALUE | DP_REPORT_ERROR:
	    sprintf(bigBuf, ceCmdTemplate, mesgStr, mesgStr, dummy, dummy);
	    break;
    	default:
	    strcpy(bigBuf, mesgStr);
	    break;
    }

    amt = SendRPCMessage(server, TOK_RDO, 0, bigBuf);

    /*
     * Now we need to hack the amount sent because
     * the caller knows nothing about the templates
     * used above and thus will think an error
     * has happened because amtSent != strlen(mesgStr)
     */

    len = strlen(mesgStr);
    if (amt >= len) {
	return len;
    } else {
	return amt;
    }
}

/*
 *--------------------------------------------------------------
 *
 * Dp_RDORead --
 *
 *	Recv's an RDO response from the given server.
 *
 * Results:
 *	Pointer to the string from the DP server or
 *	NULL with errorPtr set to a POSIX error.
 *
 * Side effects:
 *	Destroys previous value of retStr.
 *
 *--------------------------------------------------------------
 */

const char *
Dp_RDORead(server, errorPtr)
    DPServer server;
    int *errorPtr;
{
    int amount, len;

    amount = recv(server, retStr, DP_BUFFER_SIZE, 0);
    if (amount <= 0) {
    	if (amount == 0) {
	    /*
	     * EOF on socket
	     */
	    *errorPtr = ECONNRESET;
	    return NULL;
	}
	*errorPtr = errno;
	return NULL;
    }

    len = amount - 16;
    memcpy(retStr, &retStr[16], len);
    retStr[len] = '\0';
    return retStr;
}


/*
 *--------------------------------------------------------------
 *
 * Dp_WaitForServer --
 *
 *	Waits until the given socket is readable.
 *
 * Results:
 *
 *	0 if we timeout before the socket is readable
 *	< 0 if there is an error.
 *	> 0 if the socket is now readable.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
Dp_WaitForServer(server, tv)
    DPServer server;
    struct timeval *tv;
{
    fd_set readFD;

    FD_ZERO(&readFD);
    FD_SET(server, &readFD);
    return select(server + 1, &readFD, NULL, NULL, tv);
}

/*
 *--------------------------------------------------------------
 *
 * Dp_ConnectToServer --
 *
 *	Creates a TCP connection to a given IP addr and port.
 *
 * Results:
 *	returns the socket or -1 on error.
 *
 * Side effects:
 *	Creates a new socket.  This function can block forever
 *	at the Dp_WaitForServer() or recv() calls.  Be careful.
 *
 *--------------------------------------------------------------
 */

DPServer
Dp_ConnectToServer(int inetAddr, int port)
{
    DPServer sock;
    struct sockaddr_in myAddr;
    struct sockaddr_in destAddr;
    int rc;
    char EOL;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
    	return -1;
    }

    memset((char *)&myAddr, 0, sizeof(myAddr));
    myAddr.sin_addr.s_addr = INADDR_ANY;
    myAddr.sin_family = AF_INET;
    myAddr.sin_port = 0;

    rc = bind(sock, (struct sockaddr *) &myAddr, sizeof(myAddr));
    if (rc < 0) {
    	return -1;
    }

    memset((char *)&destAddr, 0, sizeof(destAddr));
    destAddr.sin_addr.s_addr = htonl(inetAddr);
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons((unsigned short)port);
    rc = connect(sock, (struct sockaddr *) &destAddr,
	    sizeof(destAddr));
    if (rc < 0) {
    	return -1;
    }

    /*
     * The Tcl-DP RPC library sends "Connection accepted"
     * upon successful linkup with a DP RPC server.  We
     * need to strip that off now so that future reads
     * don't get confused.
     *
     * There is a slight problem here in that we don't know
     * what the EOL indictator is.  We'll read in 20, which
     * is 19 + 1 character for EOL.  If the last char is
     * \r, we'll read in another byte since the server
     * sent \r\n.  This completely overlooks the fact
     * that Macs send \r as their EOL, but since DP
     * isn't suppose to run on Macs, this is an acceptable
     * hack.
     */

    Dp_WaitForServer(sock, NULL);

    rc = recv(sock, retStr, 20, 0);
    while (rc < 20) {
	rc += recv(sock, &retStr[rc], 20 - rc, 0);
    }

    EOL = retStr[19];
    retStr[19] = '\0';
    if (strcmp("Connection accepted", retStr)) {
	return -1;
    }

    if (EOL == '\r') {
	if (recv(sock, &EOL, 1, 0) != 1) {
	    return -1;
	}
    }

    return sock;
}

/* ============================================================= *
 * =================== Internal Routines ======================= *
 * ============================================================= */

/*
 *--------------------------------------------------------------
 *
 * SendRPCMessage --
 *
 *	Send an RPC message on the given socket.
 *
 * Results:
 *	Number of bytes sent on the socket.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static int
SendRPCMessage(server, token, id, msgStr)
    DPServer server;
    char token;
    int id;
    const char *msgStr;
{
    char *bufStr;
    int result, totalLength;

    totalLength = strlen(msgStr) + 16;
    bufStr = malloc(totalLength + 1);
    sprintf(bufStr, "%6d %c %6d %s", totalLength, token, id, msgStr);
    result = send(server, bufStr, totalLength, 0);
    free(bufStr);
    if (result >= 16) {
	result -= 16;
    }
    return result;
}
