/* API to connect to a Tcl-DP server.  Applications that do not
 * include Tcl can use this API to contact a Tcl-DP server as a
 * client and send RPCs and RDOs for evaluation by the server.
 *
 * Since your application does not contain Tcl, it cannot act
 * as a Tcl-DP server.
 *
 * Note that unlike the Tcl and DP sources, this file requires
 * an ANSI C compiler.  This API is extremely multithread unsafe.
 *
 * Original author: presumably Mike Perham, circa 1997
 *
 * Updated by John McGehee, www.voom.net 4/30/2010
 *   - Compile under Red Hat Enterprise Linux (RHEL) 5
 *   - Although this was originally developed under Windows,
 *     I did not try Windows
 *   - Works with Tcl-DP 4.0 server running under Tcl 8.4
 *   - Fixed bugs
 */


#ifndef _DPAPI_H
#define _DPAPI_H

#ifdef _WIN32
    #include <winsock.h>
    #define ECONNRESET	WSAECONNRESET
#else /* Unix */
	#include <sys/types.h>
#endif

typedef int DPServer;

/*
 * RDO flags
 */

#define DP_REPORT_ERROR		(1<<0)
#define DP_RETURN_VALUE		(1<<1)

/*---------------------- Externally visible API ----------------------*/
#ifdef __cplusplus
extern "C" {
#endif

const char *Dp_RPC(DPServer server, const char *mesgStr,
				    struct timeval *tv, int *errorPtr);
int Dp_RDOSend			(DPServer server, const char *mesgStr,
				    int flags);
const char *Dp_RDORead		(DPServer server, int *errorPtr);
int Dp_WaitForServer		(DPServer server, struct timeval *tv);
DPServer Dp_ConnectToServer	(int inetAddr, int port);

#ifdef __cplusplus
}
#endif

#endif
