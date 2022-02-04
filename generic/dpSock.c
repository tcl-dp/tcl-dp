/*
 * generic/dpSock.c --
 *
 *  This file implements the socket code for various channel drivers.
 *  In other words, platforms that support Berkeley sockets can use routines
 *  in this module as part of their platform specific driver.
 *  This code is supported by the routines in the file dpSock.c in the
 *  platform directories (i.e., win/dpSock.c, unix/dpSock.c, mac/dpSock.c)
 *  which handle platform-specific error translation and other non-portable
 *  functions.
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include <string.h>
#ifdef _WIN32
#	include <windows.h>
#else
#	include <netdb.h>
#endif
#include "generic/dpInt.h"


/*
 *--------------------------------------------------------------
 *
 * DpHostToIpAddr --
 *
 *	Find the IP address corresponding to a hostname.
 *
 * Results:
 *	1 on success, 0 if host is unknown
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
int
DpHostToIpAddr (host, ipAddrPtr)
    CONST84 char *host;			/* (in) Hostname (human readable) */
    int *ipAddrPtr;		/* (out) IP address of host */
{
    struct hostent *hostent;

    if (strcmp (host, "localhost") == 0) {
	*ipAddrPtr = 0x7F000001;
	return 1;
    }

    /*
     * Gotta watch this one -- on NT, gethostbyname on "" goes out
     * to lunch
     */
    if ((host == NULL) || (host[0] == 0)) {
        return 0;
    }

    /*
     * Try looking host up by address (i.e., host is something
     * like "128.84.253.1").  Since the value is returned in
     * network byte order, we change it to host byte order.
     * We do this first because it's much faster (doesn't require
     * a trip to the DNS).
     */
    *ipAddrPtr = inet_addr(host);
    if (*ipAddrPtr != DP_INADDR_NONE) {
	*ipAddrPtr = ntohl(*ipAddrPtr);
	return 1;
    }

    /*
     * Looking up the host by address failed.  Try looking it up by
     * name. If successful, the IP address is in network byte order
     * in hostent->h_addr_list[0]
     */
    hostent = gethostbyname(host);
    if (hostent != NULL) {
	memcpy ((char *)ipAddrPtr,
	       (char *) hostent->h_addr_list[0],
	       (size_t) hostent->h_length);
	*ipAddrPtr = ntohl(*ipAddrPtr);
	return 1;
    }

    /*
     * Total failure
     */
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * DpIpAddrToHost --
 *
 *	Find the hostname corresponding to an IP address
 *
 * Results:
 *	1 on success, 0 for failure
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
int
DpIpAddrToHost (ipAddr, hostPtr)
    int ipAddr;			/* (in) IP addr */
    char *hostPtr;		/* (out) Corresponding hostname */
{
    struct hostent *hEnt;

    if (ipAddr == 0x7F000001) {
    	strcpy(hostPtr, "localhost");
    } else {
    	hEnt = gethostbyaddr((char *)&ipAddr, sizeof(int), AF_INET);
    	if (hEnt == NULL) {
	    return 0;
	}
    	strcpy(hostPtr, hEnt->h_name);
    }
    return 1;
}

