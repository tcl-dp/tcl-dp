/*
 * dpPort.h --
 *
 *	This header file handles porting issues that occur because of
 *	differences between systems.  It reads in platform specific
 *	portability files.
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _DPPORT
#define _DPPORT

#include "dp.h"

#if defined(__WIN32__) || defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   undef WIN32_LEAN_AND_MEAN
#   include "win/dpPort.h"
#else
#   define EXPORT(a,b) a b
#   if defined(MAC_TCL)
    /*
     * (ToDo): there is currently no Mac Port of DP.
     */
#	include "mac/dpPort.h"
#   else
#	include "unix/dpPort.h"
#   endif
#endif


#endif /* _DPPORT */





