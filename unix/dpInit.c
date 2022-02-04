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

    /*
     * (ToDo) Load in the TCL library
     */

    return TCL_OK;
}






