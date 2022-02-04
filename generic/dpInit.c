/*
 * dpInit.c --
 *
 *	Initialize the Tcl-DP extension.
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "generic/dpPort.h"
#include "generic/dpPatch.h"
#include "generic/dpInt.h"

/*
 * The following structure defines all of the commands supported by
 * Dp, and the C procedures that execute them.
 */

typedef struct {
    CONST char *name;		/* Name of command. */
    Tcl_CmdProc *cmdProc;	/* Command procedure. */
} DpCmd;

static DpCmd commands[] = {
    {"dp_accept",	Dp_AcceptCmd},
    {"dp_connect",	Dp_ConnectCmd},
    {"dp_copy",		Dp_CopyCmd},
    {"dp_netinfo",	Dp_NetInfoCmd},
    {"dp_RDO",		Dp_RDOCmd},
    {"dp_RPC",		Dp_RPCCmd},
    {"dp_admin",	Dp_AdminCmd},
    {"dp_CancelRPC",	Dp_CancelRPCCmd},
    {"dp_send",		Dp_SendCmd},
    {"dp_recv",		Dp_RecvCmd},
    {(char *) NULL,	(Tcl_CmdProc *) NULL}
};


/*
 *----------------------------------------------------------------------
 *
 * Dp_Init --
 *
 *	This procedure is invoked to add DP to an interpreter. It
 *	incorporates all of DP's commands into the interpreter.
 *
 * Results:
 *	Returns a standard Tcl completion code and sets interp->result
 *	if there is an error.
 *
 * Side effects:
 *	Depends on various initialization scripts that get invoked.
 *
 *----------------------------------------------------------------------
 */

EXPORT(int,Dp_Init)(interp)
    Tcl_Interp *interp;		/* (in) Interpreter to initialize. */
{
    DpCmd *cmdPtr;

#ifdef USE_TCL_STUBS

/*	Check for a stubs compatible library. This replaces the Tcl_PkgRequire
	call, and makes this extension work with all binary releases after 8.1
	*/

	 if (Tcl_InitStubs(interp,"8.1",0) == NULL)
		return TCL_ERROR;
#else

/* This is the original code. The TclHasSockets function is unresolved
	for Tcl version 8.3.0.
	*/

    if (Tcl_PkgRequire(interp, "Tcl", TCL_VERSION, 1) == NULL) {
	return TCL_ERROR;
    }
    if (TclHasSockets(interp) != TCL_OK) {
	return TCL_ERROR;
    }
#endif

    Tcl_SetVar(interp, "dp_patchLevel", DP_PATCH_LEVEL, TCL_GLOBAL_ONLY);
    Tcl_SetVar(interp, "dp_version", DP_VERSION, TCL_GLOBAL_ONLY);

    if (Tcl_PkgProvide(interp, DP_PACKAGE, DP_VERSION) != TCL_OK) {
	return TCL_ERROR;
    }
    for (cmdPtr = commands; cmdPtr->name != NULL; cmdPtr++) {
	Tcl_CreateCommand(interp, cmdPtr->name, cmdPtr->cmdProc,
		(ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    }

    if (DpInitChannels(interp) != TCL_OK) {
	return TCL_ERROR;
    }
    if (DppInit(interp) != TCL_OK) {
	return TCL_ERROR;
    }
    if (DpRPCInit(interp) != TCL_OK) {
	return TCL_ERROR;
    }
    if (DpInitPlugIn(interp) != TCL_OK) {
	return TCL_ERROR;
    }

    return TCL_OK;
}

/* --- Dp_SafeInit --- Initialization for safe interpreters */

EXPORT(int,Dp_SafeInit)(interp)
Tcl_Interp *interp;

{	return Dp_Init(interp);		}



