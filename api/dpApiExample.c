/*
 * api/dpExample.c
 *
 * This file provides an example of how to use the DP C API
 * to perform RPCs and RDOs with a Tcl-DP server.
 *
 * This code also serves as the unit test, which is executed by
 * ../tests/api.test.
 */

#include <stdio.h>

#ifdef _WIN32
    #define CLOSESOCKET closesocket
#else /* unix */
    #include <string.h>
    #include <stdlib.h>
    #include <unistd.h>
    #define CLOSESOCKET close
#endif
#include "dpApi.h"


// Close sockets, shut down server
void
cleanup(DPServer server) {
    // When variable "forever" is set on the server, it will exit
    Dp_RDOSend(server, "set forever 42", 0);
    CLOSESOCKET(server);
}


int main(int argc, char **argv) {
	int host = 0x7F000001; // "127.0.0.1" == localhost
	int port = 8259; // Same as S_PORT in ../tests/make-server
	DPServer server;
	int rc = 1;
	int error = 0;
	struct timeval tv;
	const char *rdoStr = "set s 5";
	const char *rcStr = "Variable rcStr is unset";
	const char* expectedRcStr = "Variable expectedRcStr is unset";

#ifdef _WIN32
	/*
	 * Win32 requires an application to initialize
	 * the sockets library before using it. I suppose
	 * this has to do with multithreading...
	 */
	WORD ver = 0x0101;
	WSADATA garbage;

	if (WSAStartup(ver, &garbage)) {
		return 1;
	}
#endif


	/*
	 * Set our RPC timeout to 5 seconds.
	 */

	tv.tv_sec = 5;
	tv.tv_usec = 0;

	/*
	 * Simple RDO - no options
	 */

	server = Dp_ConnectToServer(host, port);
	if (server == -1) {
		printf("Failed to connect to the RPC server that was supposed\n"
		    "\t to have been created by tests/server.\n");
		return 2;
	}

	rc = Dp_RDOSend(server, rdoStr, 0);
	if (rc != (int) strlen(rdoStr)) {
		printf("Dp_RDOSend() returned %d, not the number of characters in the command that was sent, %d.\n",
			rc, strlen(rdoStr));
		cleanup(server);
		return 3;
	}

	/*
	 * Simple RPC --
	 *
	 * Error must always be initialized to zero
	 * so that we can tell if there was an error
	 * on this call.
	 */

	error = 0;
	rcStr = Dp_RPC(server, "set s", NULL, &error);
	if (error != 0) {
		printf("Error - Dp_RPC() returned non-zero error code %d.  The result was '%s'.\n", error, rcStr);
		cleanup(server);
		return 4;
	}
	if (strcmp(rcStr, "5")) {
		printf("Error - Dp_RPC() gave result '%s'.  It should have been the value of variable s on the server, '5'.\n", rcStr);
		cleanup(server);
		return 5;
	}

	/*
	 * RPC with an error --
	 *
	 * As always, the Tcl-DP RPC server sends a string representing a two element Tcl list
	 * describing the error:
	 *     {result} {$::errorInfo}
	 * The Tcl version of this command uses Tcl_Split() to split the list, and it returns only
	 * the result value.  Here the Tcl API is unavailable, so Tcl_Split() is unavailable.
	 * Instead of using a fake Tcl_Split() just for the sake of discarding information, Dp_RPC()
	 * returns the whole string "{result} {$::errorInfo}".
	 */
	error = 0;
	rcStr = Dp_RPC(server, "set nonexistentVariable", NULL, &error);
	if (error == 0) {
		printf("Error - Dp_RPC() should have returned error code %d.  The result was '%s'.\n", error, rcStr);
		cleanup(server);
		return 4;
	}
	expectedRcStr = "{can't read \"nonexistentVariable\": no such variable} {can't read \"nonexistentVariable\": no such variable\n"
			"    while executing\n"
			"\"set nonexistentVariable\"}";
	if (strcmp(rcStr, expectedRcStr)) {
		printf("Error - Dp_RPC() gave result:\n%s\nIt should have been the error message:\n%s\n", rcStr, expectedRcStr);
		cleanup(server);
		return 5;
	}

	/*
	 * Callback RDO
	 */

	rdoStr = "set x 600";
	rc = Dp_RDOSend(server, rdoStr, DP_RETURN_VALUE);
	if (rc != (int) strlen(rdoStr)) {
		printf("Dp_RDOSend() returned %d, not the number of characters in the command that was sent, %d.\n",
			rc, strlen(rdoStr));
		cleanup(server);
		return 6;
	}

	rc = Dp_WaitForServer(server, &tv);
	if (rc <= 0) {
		printf("Error waiting for reply\n");
		cleanup(server);
		return 7;
	}

	rcStr = Dp_RDORead(server, &error);
	if (rcStr == NULL) {
		printf("Problem with recv\n");
		cleanup(server);
		return 8;
	}

	printf("All Tcl-DP C-API tests passed.\n");
	cleanup(server);
	return 0;
}
