/*
 * dpCmd.c --
 *
 *	This file contains the command routines for most of
 *	the DP built-in commands.
 *
 * Copyright (c) 1995-1996 The Regents of Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include <tcl.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#	include <windows.h>
#else
#	include <netdb.h>
#endif
#include "generic/dpInt.h"

/*
 * The default number of bytes for "dp_copy" to read in each call
 * to Tcl_Read().
 */

#define	TCL_READ_CHUNK_SIZE	4096


/*
 *----------------------------------------------------------------------
 *
 * Dp_AcceptCmd --
 *
 *	This procedure is invoked to process the "dp_accept" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
int
Dp_AcceptCmd(dummy, interp, argc, argv)
    ClientData dummy;			/* Not used. */
    Tcl_Interp *interp;			/* Current interpreter. */
    int argc;				/* Number of arguments. */
    CONST84 char **argv;			/* Argument strings. */
{
    Tcl_Channel chan;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		argv[0], " channelId\"", NULL);
	return TCL_ERROR;
    }

    if ((chan = Dp_TcpAccept(interp, argv[1])) == NULL) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Dp_ConnectCmd --
 *
 *	This procedure is invoked to process the "dp_connect" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
int
Dp_ConnectCmd(dummy, interp, argc, argv)
    ClientData dummy;			/* Not used. */
    Tcl_Interp *interp;			/* Current interpreter. */
    int argc;				/* Number of arguments. */
    CONST84 char **argv;			/* Argument strings. */
{
    Dp_ChannelType * chanTypePtr;
    Tcl_Channel chan;
    char *validTypes;

    if (argc < 2) {
        validTypes = Dp_ListChannelTypes();
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
            " channelType ?args ...?\"\nValid channel types are: ", validTypes, NULL);
        ckfree (validTypes);
	return TCL_ERROR;
    }

    chanTypePtr = Dp_GetChannelType(interp, argv[1]);
    if (chanTypePtr == NULL) {
        validTypes = Dp_ListChannelTypes();
	Tcl_AppendResult(interp, "Unknown channel type \"", argv[1],
            "\"\nValid channel types are: ", validTypes, NULL);
        ckfree (validTypes);
	return TCL_ERROR;
    }

    chan = chanTypePtr->createProc(interp, argc-2, argv+2);
    if (chan == NULL) {
	return TCL_ERROR;
    } else {
        Tcl_AppendResult(interp, Tcl_GetChannelName(chan), (char *) NULL);
	return TCL_OK;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Dp_CopyCmd --
 *
 *	This procedure is invoked to process the "dp_copy" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
int
Dp_CopyCmd(dummy, interp, argc, argv)
    ClientData dummy;			/* Not used. */
    Tcl_Interp *interp;			/* Current interpreter. */
    int argc;				/* Number of arguments. */
    CONST84 char **argv;			/* Argument strings. */
{
    Tcl_Channel inChan;
    Tcl_Channel *outChans = NULL;
    int numOutChans;
    int i, m;
    int requested = INT_MAX;
    char *bufPtr = NULL;
    int actuallyRead, actuallyWritten, totalRead, toReadNow, mode;


    /*
     * 1. Get the optional -size argument.
     */

    m = 1;
    if (argc > 2) {
	if (argv[1][0] == '-' && strcmp(argv[1], "-size")==0) {
	    if (argc == 2) {
		Tcl_AppendResult(interp, "value missing for \"-size\"", NULL);
		goto error;
	    }
	    if (Tcl_GetInt(interp, argv[2], &requested) != TCL_OK) {
		goto error;
	    }
	    if (requested < 0) {
		requested = INT_MAX;
	    }

	    /*
	     * argv[m] is the in channel and argv[m+1, ...] are the out
	     * channels.
	     */
	    m = 3;
	}
    }

    if (argc-m < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ?-size size? inChanId outChanId ?outChanId ...?\"",
		(char *) NULL);
	goto error;
    }

    /*
     * 2. Get the in channel.
     */

    inChan = Tcl_GetChannel(interp, argv[m], &mode);
    if (inChan == (Tcl_Channel) NULL) {
	goto error;
    }
    if ((mode & TCL_READABLE) == 0) {
        Tcl_AppendResult(interp, "channel \"", argv[m],
                "\" wasn't opened for reading", (char *) NULL);
	goto error;
    }

    /*
     * 3. Get the out channel(s).
     */

    numOutChans = argc - m - 1;
    outChans = (Tcl_Channel*)ckalloc(sizeof(Tcl_Channel) * numOutChans);
    for (i=0; i<numOutChans; i++) {
	outChans[i] = Tcl_GetChannel(interp, argv[i + m+1], &mode);
	if (outChans[i] == (Tcl_Channel) NULL) {
	    goto error;
	}
	if ((mode & TCL_WRITABLE) == 0) {
	    Tcl_AppendResult(interp, "channel \"", argv[i + m+1],
                    "\" wasn't opened for writing", (char *) NULL);
	    goto error;
	}
    }

    /*
     * 4. Copy the data.
     */

    bufPtr = ckalloc((unsigned) TCL_READ_CHUNK_SIZE);
    for (totalRead = 0;
            requested > 0;
            totalRead += actuallyRead, requested -= actuallyRead) {
        toReadNow = requested;
        if (toReadNow > TCL_READ_CHUNK_SIZE) {
            toReadNow = TCL_READ_CHUNK_SIZE;
        }
        actuallyRead = Tcl_Read(inChan, bufPtr, toReadNow);
        if (actuallyRead < 0) {
	    Tcl_AppendResult(interp, argv[0], ": ", Tcl_GetChannelName(inChan),
		    " ", Tcl_PosixError(interp), (char *) NULL);
	    goto error;
        }
        if (actuallyRead == 0) {
            char result[32];
            snprintf(result, 32, "%d", totalRead);
            Tcl_SetResult(interp, result, TCL_VOLATILE);
	    goto done;
        }
	for (i=0; i<numOutChans; i++) {
	    actuallyWritten = Tcl_Write(outChans[i], bufPtr, actuallyRead);
	    if (actuallyWritten < 0) {
		Tcl_AppendResult(interp, argv[0], ": ",
			Tcl_GetChannelName(outChans[i]), " ",
			Tcl_PosixError(interp), (char *) NULL);
		goto error;
	    }
        }
    }

done:
    if (bufPtr != NULL) {
	ckfree(bufPtr);
    }
    if (outChans != NULL) {
	ckfree((char*)outChans);
    }
    char result[32];
    snprintf(result, 32, "%d", totalRead);
    Tcl_SetResult(interp, result, TCL_VOLATILE);
    return TCL_OK;

error:
    if (bufPtr != NULL) {
	ckfree(bufPtr);
    }
    if (outChans != NULL) {
	ckfree((char*)outChans);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Dp_NetInfoCmd --
 *
 *	This procedure is invoked to process the "dp_netinfo" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
int
Dp_NetInfoCmd(dummy, interp, argc, argv)
    ClientData dummy;			/* Not used. */
    Tcl_Interp *interp;			/* Current interpreter. */
    int argc;				/* Number of arguments. */
    CONST84 char **argv;			/* Argument strings. */
{
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
	    " option arg\"", NULL);
	return TCL_ERROR;
    }

    if (strlen(argv[1]) > 1) {
	char c = argv[1][1];
	switch (c) {
	case 's': {
	    /*
	     * Get the service entry for service name or port number
	     */
	    if (strcmp(argv[1], "-service") == 0) {
		struct servent *serviceEntry = NULL;
		char port[10];
		int portNum;
		/*
		 * First try argv[2] as a name
		 */
		serviceEntry = getservbyname(argv[2], (char *) NULL);
		if (serviceEntry == NULL) {
		    /*
		     * Now try argv[2] as a port number
		     */
		    portNum = atoi(argv[2]);
		    serviceEntry = getservbyport(htons((unsigned short)portNum),
					(char *) NULL);
		    if (serviceEntry == NULL) {
			Tcl_AppendResult(interp, argv[0],
				" -service unknown service/port# \"",
				argv[2], "\"", (char *) NULL);
			return TCL_ERROR;
		    }
		}
		sprintf(port, "%4d", ntohs(serviceEntry->s_port));
		Tcl_AppendResult(interp, serviceEntry->s_name, " ", port, " ",
				(char *) NULL);
		return TCL_OK;
	    }
	}
	break;

	case 'a': {
	    /*
	     * Translate between host name and IP address.
	     */
	    if (strcmp(argv[1], "-address") == 0) {
		char hostName[120];
		char addrStr[16];
		int addr;
		/*
		 * try argv[2] as an IP address first
		 */
		if ((addr = inet_addr(argv[2])) != -1) {
		    if (DpIpAddrToHost(addr, hostName)) {
			Tcl_AppendResult(interp, hostName, (char *) NULL);
			return TCL_OK;
		    } else {
			Tcl_AppendResult(interp, argv[0],
				" -address unknown host \"",
				argv[2], "\"", (char *) NULL);
			return TCL_ERROR;
		    }
		} else {
		    if (DpHostToIpAddr(argv[2], &addr)) {
			sprintf(addrStr, "%d.%d.%d.%d", (addr>>24)&0xFF,
				(addr>>16)&0xFF, (addr>>8)&0xFF, addr&0xFF);
			Tcl_AppendResult(interp, addrStr, (char *) NULL);
			return TCL_OK;
		    } else {
			Tcl_AppendResult(interp, argv[0],
				" -address unknown host \"",
				argv[2], "\"", (char *) NULL);
			return TCL_ERROR;
		    }
		}
	    }
	}
	break;

	}
    }
    Tcl_AppendResult(interp, argv[0], ":  unknown option \"",
	    argv[1], "\"", (char *) NULL);
    return TCL_ERROR;
}

/* ----------------------------------------------------
 *
 *    Dp_SendCmd --
 *
 *	Implements a send-like command for channels.
 *	The Tcl I/O system has serious problems because
 *	it does internal buffering.  One can use this
 *	command to bypass the Tcl I/O subsystem.
 *
 *    Returns
 *
 *	TCL_OK with the amount sent or TCL_ERROR.
 *
 *    Side Effects
 *
 *	The channel is written to.
 *
 * -----------------------------------------------------
 */

int
Dp_SendCmd(dummy, interp, argc, argv)
    ClientData dummy;			/* Not used. */
    Tcl_Interp *interp;			/* Current interpreter. */
    int argc;				/* Number of arguments. */
    CONST84 char **argv;			/* Argument strings. */
{
    Tcl_Channel chan;
    char writ[10];
    int errorCode = 0, toWrite, written = 0, mode;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		argv[0], " channelId string\"", NULL);
	return TCL_ERROR;
    }

    if ((chan = Tcl_GetChannel(interp, argv[1], &mode)) == NULL) {
	return TCL_ERROR;
    }

    toWrite = strlen(argv[2]);

    while (toWrite > written) {
	written += (Tcl_GetChannelType(chan)->outputProc)
		(Tcl_GetChannelInstanceData(chan), argv[2] + written,
		toWrite - written, &errorCode);
	if (errorCode > 0) {
	    break;
	}
    }

    if (errorCode > 0) {
	/*
	 * Translate error code to POSIX
	 */
	DppGetErrno();
        Tcl_AppendResult(interp, "Error sending on channel \"", argv[1], "\":",
		Tcl_PosixError(interp), (char *)NULL);
	return TCL_ERROR;
    }
    sprintf(writ, "%d", written);

    Tcl_AppendResult(interp, writ, (char *)NULL);
    return TCL_OK;
}

/* ----------------------------------------------------
 *
 *    Dp_RecvCmd --
 *
 *	Implements a recv-like command for channels.
 *	The Tcl I/O system has serious problems because
 *	it does internal buffering.  One can use this
 *	command to bypass the Tcl I/O subsystem.
 *
 *    Returns
 *
 *	TCL_OK with the message recv'd or TCL_ERROR.
 *
 *    Side Effects
 *
 *	The channel is read.
 *
 * -----------------------------------------------------
 */

int
Dp_RecvCmd(dummy, interp, argc, argv)
    ClientData dummy;			/* Not used. */
    Tcl_Interp *interp;			/* Current interpreter. */
    int argc;				/* Number of arguments. */
    CONST84 char **argv;			/* Argument strings. */
{
    Tcl_Channel chan;
    Tcl_DString dstr;
    int errorCode = 0, nread = 0, mode;
    char buff[4096];
    int blocking;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		argv[0], " channelId\"", NULL);
	return TCL_ERROR;
    }

    if ((chan = Tcl_GetChannel(interp, argv[1], &mode)) == NULL) {
	return TCL_ERROR;
    }
    memset(buff, 0, sizeof(buff));

    nread = (Tcl_GetChannelType(chan)->inputProc) (Tcl_GetChannelInstanceData(chan),
            (char *)buff, sizeof(buff)-1, &errorCode);

    if (nread == DP_SOCKET_ERROR) {
	Tcl_DStringInit(&dstr);
	Tcl_GetChannelOption(
#if (TCL_MAJOR_VERSION > 7)
				interp,
#endif
				chan, "-blocking", &dstr);
	Tcl_GetBoolean(interp, Tcl_DStringValue(&dstr), &blocking);
        Tcl_DStringFree(&dstr);
	if ((blocking == 0) && (DppGetErrno() == EWOULDBLOCK)) {
	    /*
	     * If the channel is non-blocking and we returned because
	     * there was nothing to read, then there is no error.
	     */
	} else {
	    /*
	     * The error code has already been translated by the above call
	     * to DppGetErrno().
	     */
	    Tcl_AppendResult(interp, "Error receiving on channel \"", argv[1],
		    "\":", Tcl_PosixError(interp), (char *)NULL);
	    return TCL_ERROR;
	}
    }

    Tcl_AppendResult(interp, buff, (char *)NULL);
    return TCL_OK;
}


