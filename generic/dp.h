/*
 * dp.h --
 *
 *	Declarations for Dp-related things that are visible
 *	outside of the Dp module itself.
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _DP
#define _DP

#include <tcl.h>

#include "generic/dpconfig.h"

/*
 * Procedure types defined by DP:
 */

typedef Tcl_Channel (Dp_ChanCreateProc) _ANSI_ARGS_((Tcl_Interp *interp,
	int argc, CONST84 char ** argv));

/*
 * The following structure is used to register channel types which become
 * available to various DP commands.
 */

typedef struct Dp_ChannelType {
    struct Dp_ChannelType * nextPtr;	/* Links to the next channel type
					 * record in the list of types.
					 * Must be set to NULL by the caller
					 * of Dp_RegisterChannelType() */
    char * name;                        /* Name of this type of channel */
    Dp_ChanCreateProc *createProc;      /* Procedure to create a channel
                                         * of this type */
} Dp_ChannelType;


/*
 * Type for plug-in function.
 */

typedef int (Dp_PlugInFilterProc)  _ANSI_ARGS_((CONST84 char *inBuf, int inLength,
                                                char **outBuf, int *outLength,
						void **data, Tcl_Interp *interp,
						int mode));

/*
 * Any new filter that is registered should provide a (ckallock-ed) pointer to
 * such a structure, whose "name" and "plugProc" fields must be set.
 */

typedef struct _Dp_PlugInFilter {
    struct _Dp_PlugInFilter * nextPtr;
    char                    * name;
    Dp_PlugInFilterProc     * plugProc;
} Dp_PlugInFilter;


/*
 * Modes for the plug-in filter functions.
 */

#define DP_FILTER_NORMAL   0
#define DP_FILTER_FLUSH    1
#define DP_FILTER_CLOSE    3
#define DP_FILTER_SET      4
#define DP_FILTER_GET      5
#define DP_FILTER_EOF      6

/*
 * Exported DP functions.
 */

EXTERN char *           Dp_ListChannelTypes    _ANSI_ARGS_((void));
EXTERN Dp_ChannelType * Dp_GetChannelType _ANSI_ARGS_((
        		    Tcl_Interp * interp, CONST84 char * name));
EXTERN int		Dp_RegisterChannelType _ANSI_ARGS_((
    			    Tcl_Interp * interp, Dp_ChannelType *newTypePtr));

EXTERN int              Dp_RegisterPlugInFilter  _ANSI_ARGS_((
    			    Tcl_Interp * interp, Dp_PlugInFilter *plugInPtr));

EXTERN Dp_PlugInFilterProc *Dp_GetFilterPtr  _ANSI_ARGS_((
                            Tcl_Interp * interp, CONST84 char * name));

EXTERN char            *Dp_GetFilterName  _ANSI_ARGS_((
			    Dp_PlugInFilterProc *filter));


#endif /* _DP */

