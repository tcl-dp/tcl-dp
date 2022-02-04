/* 
 * dpInit.c --
 *
 *	Perform UNIX-specific initialization of DP.
 *
 * Copyright (c) 1995-1996 The Regents of Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "generic/dpPort.h"
#include "generic/dpInt.h"


/*
 *----------------------------------------------------------------------
 *
 * DllEntryPoint --
 *
 *	This wrapper function is used by Windows to invoke the
 *	initialization code for the DLL.  If we are compiling
 *	with Visual C++, this routine will be renamed to DllMain.
 *	routine.
 *
 * Results:
 *	Returns TRUE;
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

BOOL APIENTRY
DllEntryPoint(hInst, reason, reserved)
    HINSTANCE hInst;		/* Library instance handle. */
    DWORD reason;		/* Reason this function is being called. */
    LPVOID reserved;		/* Not used. */
{
    return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * DppInit --
 *
 *	Performs Unix-specific interpreter initialization related to the
 *      dp_library variable.
 *
 * Results:
 *	Returns a standard Tcl result.  Leaves an error message or result
 *	in interp->result.
 *
 * Side effects:
 *	Sets "dp_library" Tcl variable, runs "tk.tcl" script.
 *
 *----------------------------------------------------------------------
 */

int
DppInit(interp)
    Tcl_Interp *interp;
{
    int err; 
    char str[256];

    /*
     * (ToDo) Load in the TCL library
     */

    /*
     * Initialize the Windows Socket library
     */
    err = WSAStartup(MAKEWORD(1, 1), &dpStartUpInfo); 
    switch (err) {
	case 0:
	    /* All is well */
	    break;

	case WSASYSNOTREADY:
	    Tcl_AppendResult (interp, "Error initializing Tcl-DP: ",
		"network subsystem is not ready for network ",
		"communication.", NULL);
	    return TCL_ERROR; 

	case WSAVERNOTSUPPORTED:
	    Tcl_AppendResult (interp, "Error initializing Tcl-DP: ",
		"The version of Windows Sockets support requested ",
		"is not provided by this particular Windows Sockets ",
		"implementation.", NULL);
	    return TCL_ERROR; 
 
	case WSAEINVAL:
	    Tcl_AppendResult (interp, "Error initializing Tcl-DP: ",
		"The Windows Sockets version specified by the ",
		"application is not supported by this DLL.", NULL);
	    return TCL_ERROR; 

	default:
	    sprintf (str, "%d", err);
	    Tcl_AppendResult (interp, "Error initializing Tcl-DP: ",
		"Unknown error from WSAStartup.  Error code is ",
		str, ". Please email this error message to ",
		"tcl-dp@cs.cornell.edu", NULL);
	    return TCL_ERROR; 
    }
 
    /*
     * Confirm that the Windows Sockets DLL supports 1.1.
     * Note that if the DLL supports versions greater 
     * than 1.1 in addition to 1.1, it will still return 
     * 1.1 in wVersion since that is the version we 
     * requested.
     */ 

    if (LOBYTE(dpStartUpInfo.wVersion) != 1 || 
	HIBYTE(dpStartUpInfo.wVersion) != 1 ) {
	WSACleanup(); 
	sprintf (str, "%d.%d", LOBYTE(dpStartUpInfo.wVersion),
		HIBYTE(dpStartUpInfo.wVersion));
	Tcl_AppendResult (interp, "Error initializing Tcl-DP: ",
		"Tcl-DP requires at least winsock version 1.1. ",
		"Installed winsock.dll has version ", str, NULL);
	return TCL_ERROR; 
    } 
    return TCL_OK;
}



