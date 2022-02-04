/*
 * dpChan.c --
 *
 *	Routines for managing install-able channel types.
 *
 * Copyright (c) 1995-1996 Cornell University.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include <tcl.h>
#include <stdlib.h>
#include <string.h>
#include "generic/dpInt.h"


/*
 * This variable holds the list of channel types.
 */

static Dp_ChannelType * chanTypeList = (Dp_ChannelType *) NULL;

/*
 * All the DP built-in channel types.
 */

static Dp_ChannelType builtInTypes[] = {
    {NULL,	"ipm",		DpOpenIpmChannel},
    {NULL,	"tcp",		DpOpenTcpChannel},
#ifndef _WIN32
    {NULL,	"email",	DpCreateEmailChannel},
#endif
    {NULL,      "identity",     DpCreateIdChannel},
    {NULL,      "plugfilter",   DpCreatePlugFChannel},
    {NULL,	"udp",		DpOpenUdpChannel},
    {NULL,      "serial",       DpOpenSerialChannel},
    {NULL,      "packoff",      DpCreatePOChannel},
    /*
     * This array must end with the following element.
     */
    {NULL,	NULL,		NULL}
};


/*
 *--------------------------------------------------------------
 *
 *  Dp_RegisterChannelType --
 *
 *	Registers a new type of channel that can be used in the DP
 *	user-level commands. newTypePtr must points to a Dp_ChannelType
 *	structure in *static memory*, the contents of which must not
 *	be modified after calling this function.
 *
 * Results:
 *	Standard TCL return value. Fails if a channel type with the
 *	same name has already been registered.
 *
 * Side effects:
 *	On success, newTypePtr is inserted to the head of the list
 *	of channel types. Also, the next pointer of newTypePtr is
 *	modified.
 *
 *--------------------------------------------------------------
 */

int
Dp_RegisterChannelType(interp, newTypePtr)
    Tcl_Interp * interp;        /* Interpreter to report errors. */
    Dp_ChannelType * newTypePtr;/* The DP channel type record */
{
    Dp_ChannelType * chanTypePtr;

    for (chanTypePtr = chanTypeList; chanTypePtr;
	    chanTypePtr=chanTypePtr->nextPtr) {

	if (strcmp(chanTypePtr->name, newTypePtr->name)==0) {
	    Tcl_AppendResult(interp, "Channel type \"", newTypePtr->name,
		"\" already exists", NULL);
	    return TCL_ERROR;
	}
    }

    newTypePtr->nextPtr = chanTypeList;
    chanTypeList = newTypePtr;

    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 *  Dp_GetChannelType --
 *
 *	Returns a registered channel type.
 *
 * Results:
 *	Standard TCL return value.
 *
 * Side effects:
 *	None.
 *--------------------------------------------------------------
 */

Dp_ChannelType *
Dp_GetChannelType(interp, name)
    Tcl_Interp * interp;        /* Interpreter to report errors. */
    CONST84 char * name;		/* String name of the channel type */
{
    Dp_ChannelType * chanTypePtr;

    for (chanTypePtr = chanTypeList; chanTypePtr;
	    chanTypePtr=chanTypePtr->nextPtr) {

	if (strcmp(chanTypePtr->name, name)==0) {
	    return chanTypePtr;
	}
    }

    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 *  Dp_ListChannelTypes --
 *
 *	Returns a list of valid channel types.   The string is
 *      dynamically allocated, and must be freed by the caller.
 *
 * Results:
 *	Dynamically allocated string with valid channel types.
 *
 * Side effects:
 *	Return value must be free'd by caller.
 *
 *--------------------------------------------------------------
 */

char *
Dp_ListChannelTypes()
{
    char *str;
    int maxLen, currLen, len;
    Dp_ChannelType *chanTypePtr;

    maxLen = 1024;
    currLen = 0;
    str = ckalloc (maxLen);

    for (chanTypePtr = chanTypeList; chanTypePtr != NULL;
	    chanTypePtr = chanTypePtr->nextPtr) {
        len = strlen(chanTypePtr->name);
        if ((len + currLen + 2) > maxLen) {
	    char *newStr;
            maxLen += max (1024,len+512);
            newStr = ckalloc (maxLen);
            memcpy (newStr, str, currLen);
            ckfree (str);
            str = newStr;
	}
        sprintf (str + currLen, "%s ", chanTypePtr->name);
        currLen += len + 1;
    }
    return str;
}

/*
 *--------------------------------------------------------------
 *
 *  DpClose --
 *
 *	Closes a channel.  We just eval "close" in case the
 *	close command has been overloaded a la dp_atclose.
 *
 * Results:
 *
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *
 *	Closes a channel.  It is no longer available in this
 *	interpreter.
 *.
 *--------------------------------------------------------------
 */

int
DpClose(interp, chan)
    Tcl_Interp *interp;
    Tcl_Channel chan;
{
    char cmd[30];

    sprintf(cmd, "close %s", Tcl_GetChannelName(chan));
    return Tcl_GlobalEval(interp, cmd);
}

/*
 *--------------------------------------------------------------
 *
 *  DpInitChannels --
 *
 *	Registers all the built-in channels supported by DP.
 *
 * Results:
 *	Standard TCL return value.
 *
 * Side effects:
 *	Built-in channels are registered in the channel type list.
 *--------------------------------------------------------------
 */

int
DpInitChannels(interp)
    Tcl_Interp * interp;        /* Interpreter to report errors. */
{
    int i;

    for (i=0; ; i++) {
	if (builtInTypes[i].name == NULL) {
	    break;
	} else {
	    if (Dp_RegisterChannelType(interp, &builtInTypes[i]) != TCL_OK) {
		return TCL_ERROR;
	    }
	}
    }

    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * DpTranslateOption --
 *
 *	This function translates a standard option name (e.g.,
 *	"sendBuffer") into a key (e.g., DP_SEND_BUFFER_SIZE)
 *
 * Results:
 *	value of option (see dpInt.h for valid keys -- search on
 *	DP_SEND_BUFFER_SIZE) if the option was found, -1 if not.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
int
DpTranslateOption (name)
    CONST84 char *name;			/* The name to translate */
{
    char c;
    int len;

    c = name[0];
    len = strlen(name);

    if ((c == 'b') && (strncmp(name, "baudrate", len) == 0)) {
	return DP_BAUDRATE;
    } else if ((c == 'c') && (strncmp(name, "charsize", len) == 0)) {
	return DP_CHARSIZE;
    } else if ((c == 'g') && (strncmp(name, "group", len) == 0)) {
	return DP_GROUP;
    } else if ((c == 'h') && (strncmp(name, "host", len) == 0)) {
	return DP_HOST;
    } else if ((c == 'k') && (strncmp(name, "keepAlive", len) == 0)) {
	return DP_KEEP_ALIVE;
    } else if ((c == 'l') && (strncmp(name, "linger", len) == 0)) {
	return DP_LINGER;
    } else if ((c == 'l') && (strncmp(name, "loopback", len) == 0)) {
	return DP_MULTICAST_LOOP;
    } else if ((c == 'm') && (strncmp(name, "myport", len) == 0)) {
	return DP_MYPORT;
    } else if ((c == 'p') && (strncmp(name, "parity", len) == 0)) {
	return DP_PARITY;
    } else if ((c == 'p') && (strncmp(name, "peek", len) == 0)) {
	return DP_PEEK;
    } else if ((c == 'p') && (strncmp(name, "port", len) == 0)) {
	return DP_PORT;
    } else if ((c == 'r') && (strncmp(name, "recvBuffer", len) == 0)) {
	return DP_RECV_BUFFER_SIZE;
    } else if ((c == 'r') && (strncmp(name, "reuseAddr", len) == 0)) {
	return DP_REUSEADDR;
    } else if ((c == 's') && (strncmp(name, "sendBuffer", len) == 0)) {
	return DP_SEND_BUFFER_SIZE;
    } else if ((c == 's') && (strncmp(name, "stopbits", len) == 0)) {
	return DP_STOPBITS;
    } else if ((c == 'm') && (strncmp(name, "myIpAddr", len) == 0)) {
	return DP_MYIPADDR;
    } else if ((c == 'd') && (strncmp(name, "destIpAddr", len) == 0)) {
	return DP_REMOTEIPADDR;
    } else if ((c == 'd') && (strncmp(name, "destport", len) == 0)) {
	return DP_REMOTEPORT;
    } else if ((c == 'a') && (strncmp(name, "address", len) == 0)) {
	return DP_ADDRESS;
    } else if ((c == 'i') && (strncmp(name, "identifier", len) == 0)) {
	return DP_IDENTIFIER;
    } else if ((c == 's') && (strncmp(name, "sequence", len) == 0)) {
	return DP_SEQUENCE;
    } else if ((c == 'c') && (strncmp(name, "channel", len) == 0)) {
	return DP_CHANNEL;
    } else if ((c == 'i') && (strncmp(name, "infilter", len) == 0)) {
	return DP_INFILTER;
    } else if ((c == 'o') && (strncmp(name, "outfilter", len) == 0)) {
	return DP_OUTFILTER;
    } else if ((c == 'i') && (strncmp(name, "inset", len) == 0)) {
	return DP_INSET;
    } else if ((c == 'o') && (strncmp(name, "outset", len) == 0))
	return DP_OUTSET;

    return -1;
}




/*
 * This variable holds the list of registered plug-in filters.
 */

static Dp_PlugInFilter *plugInList = (Dp_PlugInFilter *) NULL;


/*
 * All the built-in plug-in functions.
 */

static Dp_PlugInFilter builtInPlugs[] = {

    {NULL,     "identity",     Identity},
    {NULL,     "plug1to2",     Plug1to2},
    {NULL,     "plug2to1",     Plug2to1},
    {NULL,     "xor",          Xor},
    {NULL,     "packon",       PackOn},
    {NULL,     "uuencode",     Uuencode},
    {NULL,     "uudecode",     Uudecode},
    {NULL,     "tclfilter",    TclFilter},
    {NULL,     "hexout",    HexOut},
    {NULL,     "hexin",    HexIn},

    /*
     * This array must end with the following element.
     */

    {NULL,	NULL,		NULL}
};



/*
 *-----------------------------------------------------------------------------
 *
 * Dp_RegisterPlugInFilter --
 *
 *	Registers a new type of filter that can be used in the DP
 *	user-level commands. newPlugInPtr must point to a Dp_PlugInFilter
 *	structure in *static memory*, the contents of which must not
 *	be modified after calling this function.
 *
 * Results:
 *
 *	Standard TCL return value. Fails if a filter with the
 *	same name has already been registered.
 *
 * Side effects:
 *	On success,  newPlugInPtr is inserted to the head of the list
 *	of filter. Also, the next pointer of newPlugInPtr is
 *	modified.
 *
 *--------------------------------------------------------------
 */

int
Dp_RegisterPlugInFilter (interp, newPlugInPtr)
    Tcl_Interp      * interp;       /* (in) Interpreter to report errors to. */
    Dp_PlugInFilter * newPlugInPtr; /* (in) Pointer to the filter function. */
{
    Dp_PlugInFilter *plugInPtr;

    for (plugInPtr = plugInList; plugInPtr;
	    plugInPtr = plugInPtr->nextPtr) {

	if (strcmp(plugInPtr->name, newPlugInPtr->name)==0) {
	    Tcl_AppendResult(interp, "Plug-in filter  \"", newPlugInPtr->name,
		"\" already exists", NULL);
	    return TCL_ERROR;
	}
    }

    newPlugInPtr->nextPtr = plugInList;
    plugInList = newPlugInPtr;

    return TCL_OK;
}



/*
 *-----------------------------------------------------------------------------
 *
 * Dp_GetFilterPtr --
 *
 *	Returns a pointer to the filter function whose name was given.
 *
 * Results:
 *
 *	Pointer to the filter function or NULL if the name is not the name of
 *	a registered filter function.
 *
 * Side effects:
 *
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

Dp_PlugInFilterProc *
Dp_GetFilterPtr  (interp, name)
    Tcl_Interp  *interp;        /* (in) Interpreter to report errors to. */
    CONST84 char        *name;		/* (in) Name of the filter function. */
{
    Dp_PlugInFilter *plugInPtr;

    for (plugInPtr = plugInList; plugInPtr;
	    plugInPtr = plugInPtr->nextPtr) {
	if (strcmp(plugInPtr->name, name) == 0) {
            return plugInPtr->plugProc;
        }
    }

    Tcl_AppendResult(interp, "unknown plug-in function \"", name, "\"", NULL);
    return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dp_GetFilterName --
 *
 *	Returns the name of the given filter function.
 *
 * Results:
 *
 *	A pointer to the name of the filter function or NULL if the function
 *	pointer does not appear in the list of registered filter functions.
 *
 * Side effects:
 *
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

char *
Dp_GetFilterName  (filter)
    Dp_PlugInFilterProc *filter;    /* (in) Pointer to the filter function. */
{
    Dp_PlugInFilter *plugInPtr;

    for (plugInPtr = plugInList; plugInPtr;
	    plugInPtr = plugInPtr->nextPtr) {
	if (filter, plugInPtr->plugProc) {
            return plugInPtr->name;
        }
    }

    return NULL;
}



/*
 *-----------------------------------------------------------------------------
 *
 * Dp_DpInitPlugIn --
 *
 *	Registers all the built-in channels supported by DP.
 *
 * Results:
 *
 *	Standard TCL return value.
 *
 * Side effects:
 *
 *	Built-in filter functions are registered in the plug-in filter list.
 *
 *-----------------------------------------------------------------------------
 */

int
DpInitPlugIn(interp)
    Tcl_Interp * interp;        /* (in) Interpreter to report errors to. */
{
    int i;

    for (i=0; ; i++) {
	if (builtInPlugs[i].name == NULL) {
	    break;
	} else {
	    if (Dp_RegisterPlugInFilter(interp, &builtInPlugs[i]) != TCL_OK) {
		return TCL_ERROR;
	    }
	}
    }

    return TCL_OK;
}



