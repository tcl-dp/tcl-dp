/*
 * generic/dpIdentity.c --
 *
 * This file contains the implementation of the identity filter channels.
 * These are channels that are created by evaluating "dp_connect identity".
 *
 */


#include <string.h>
#include <errno.h>
#include <tcl.h>
#include <generic/dpInt.h>


/*
 * Prototypes for functions referenced only in this file.
 */

static int	CloseIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     Tcl_Interp *interp));

static int      InputIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     char *buf, int bufsize,
                                                     int  *errorCodePtr));

static int      OutputIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                      CONST84 char *buf, int toWrite,
                                                      int *errorCodePtr));

static int      SOPIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     Tcl_Interp *interp,
                                                     CONST char *optionName,
                                                     CONST char *optionValue));

#ifdef _TCL76
static int	GOPIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     char *optionName,
                                                     Tcl_DString *dsPtr));
#else
static int	GOPIdChannel		_ANSI_ARGS_((ClientData instanceData,
													 Tcl_Interp *interp,
													 CONST84 char *optionName,
                                                     Tcl_DString *dsPtr));
#endif

#ifndef _TCL76
static int	GFPIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     int direction,
                                                     FileHandle *handlePtr));
#else
static Tcl_File	GFPIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     int direction));
#endif

static int	CRPIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     int mask));

static void	WCPIdChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     int mask));

/*
 * This structure stores the names of the functions that Tcl calls when certain
 * actions have to be performed on an identity channel. To understand this entry,
 * please refer to the documentation of the Tcl_CreateChannel and its associated
 * functions in the Tcl 7.6 documentation.
 *
 * An identity channel will always be non-blocking.
 * Seek on an identity channel is not allowed.
 */


Tcl_ChannelType idChannelType = {
    "idfilter",
    DP_CHANNEL_VERSION,	/* TCL_CHANNEL_VERSION_1, TCL_CHANNEL_VERSION_2, and so on */
    CloseIdChannel,    /* closeProc        */
    InputIdChannel,    /* inputProc        */
    OutputIdChannel,   /* outputProc       */
    NULL,              /* seekProc         */
    SOPIdChannel,      /* setOptionProc    */
    GOPIdChannel,      /* getOptionProc    */
    WCPIdChannel,      /* watchChannelProc */
#ifdef _TCL76
    CRPIdChannel,      /* channelReadyProc */
#endif
    GFPIdChannel,       /* getFileProc      */
    NULL,			/* Proc to call to close the channel if the device
					 * supports closing the read & write sides */
    NULL,	/* Proc to set blocking mode on socket */
    /* Only valid in TCL_CHANNEL_VERSION_2 channels or later */
    NULL,			/* Proc to call to flush a channel */
    NULL,			/* Proc to call to handle a channel event */
    /* Only valid in TCL_CHANNEL_VERSION_3 channels or later */
    NULL,			/* Proc to call to seek on the channel which can handle 64-bit offsets */
    /* Only valid in TCL_CHANNEL_VERSION_4 channels or later */
    NULL			/* Proc to notify the driver of thread specific activity for a channel */
};


/*
 * Structure that stores the data needed to manage an identity filter.
 */

typedef struct {

    /* Pointer to the subordinated channel. */
    Tcl_Channel channelPtr;

    /* If peek = 0 consume input, otherwise not. */
    int         peek;
}
    IdentityInfo;



/*
 *-----------------------------------------------------------------------------
 *
 * DpCreateIdChannel --
 *
 *	Creates an identity filter channel.
 *
 * Results:
 *
 *	Returns a channel data structure. If an error happens, NULL
 *      is returned.
 *
 * Side effects:
 *
 *	Alocates memory for the instance data that is associated
 *	with the channel.
 *
 * ----------------------------------------------------------------------------
 */

Tcl_Channel
DpCreateIdChannel (interp, argc, argv)
    Tcl_Interp *interp;		/* (in) Pointer to tcl interpreter. */
    int         argc;		/* (in) Number of arguments. */
    CONST84 char **argv;    /* (in) Argument strings. */
{
    static int    openedChannels = 0;
    int           i;
    IdentityInfo *instanceData;
    Tcl_Channel   newChannel;
    char          chanName [20];


    instanceData = (IdentityInfo *)ckalloc(sizeof(IdentityInfo));

    if(instanceData == NULL) {
        Tcl_AppendResult(interp, "unable to allocate memory for identity ",
			 "filter channel", NULL);
        return NULL;
    }

    instanceData->channelPtr = NULL;

    for (i = 0; i < argc; i += 2) {
        int v = i+1;
	size_t len = strlen(argv[i]);

	if (strncmp(argv[i], "-channel", len)==0) {
	    if (v == argc) {
                goto error2;
            }

            instanceData->channelPtr = Tcl_GetChannel(interp, argv[v], NULL);

            if(instanceData->channelPtr == NULL) {
                goto error1;
            }
	} else {
    	    Tcl_AppendResult(interp, "unknown option \"",
                             argv[i], "\", must be -channel", NULL);
	    goto error1;
	}
    }

    if(instanceData->channelPtr == NULL) {
        Tcl_AppendResult(interp, "-channel must be defined for an identity ",
			 "channel", NULL);
        goto error1;
    }

    /* No peek by default. */
    instanceData->peek = 0;


    /* Identity filters are both readable and writable. */
    sprintf(chanName, "idfilter%d", openedChannels++);
    newChannel = Tcl_CreateChannel(&idChannelType, chanName,
    		(ClientData)instanceData, TCL_READABLE | TCL_WRITABLE);
    if(newChannel == NULL) {
        Tcl_AppendResult(interp, "tcl unable to create identity channel", NULL);
        goto error1;
    }
    Tcl_RegisterChannel(interp, newChannel);

    return newChannel;


error2:
    Tcl_AppendResult(interp, "option value missing for -channel", NULL);
    /* continues with error1 */

error1:
    ckfree((char *)instanceData);
    return NULL;
}



/*
 *-----------------------------------------------------------------------------
 *
 * CloseIdChannel --
 *
 *	Closes the given identity filter channel.
 *
 * Results:
 *
 *	If everything goes well, returns 0. If any error happens,
 *      it returns a POSIX error code.
 *
 * Side effects:
 *
 *	It frees the instance data associated with the channel.
 *
 * ----------------------------------------------------------------------------
 */

static int
CloseIdChannel (instanceData, interp)
    ClientData  instanceData;  /* (in) Pointer to IdentityInfo struct. */
    Tcl_Interp *interp;        /* Pointer to the tcl interpreter. */
{
    ckfree((char *)instanceData);
    return 0;
}



/*
 *-----------------------------------------------------------------------------
 *
 * InputIdChannel --
 *
 *	Reads min(requested data, available data) from the subordinated channel.
 *
 * Results:
 *
 *	Number of bytes of data read from the subordinated filter. If an error
 *	happened, it returns -1.
 *
 * Side effects:
 *
 *	1. Calls the read procedure of the subordinated channel.
 *	2. Stores a POSIX code at errorBuffer if an error occurs.
 *
 * ----------------------------------------------------------------------------
 */

static int
InputIdChannel (instanceData, buf, bufsize, errorCodePtr)
    ClientData	instanceData;	   /* (in) Pointer to IdentityInfo struct. */
    char       *buf;		   /* (in/out) Buffer to fill. */
    int	        bufsize;	   /* (in) Size of buffer. */
    int	       *errorCodePtr;	   /* (out) POSIX error code (if any). */
{
    int tmp;

    Tcl_Channel channelPtr = ((IdentityInfo *)instanceData)->channelPtr;

    tmp = Tcl_Read(channelPtr, buf, bufsize);

    if(tmp == -1) {
        *errorCodePtr = Tcl_GetErrno();
    }

    return tmp;

}



/*
 *-----------------------------------------------------------------------------
 *
 * OutputIdChannel --
 *
 *	Writes the data to the subordinated channel.
 *
 * Results:
 *
 *	Number of writes written, or -1 if an error is signalled from the
 *	subordinated filter.
 *
 * Side effects:
 *
 *	1. Calls the write procedure of the subordinated channel.
 *	2. Stores a POSIX code at errorBuffer if an error occurs.
 *
 * ----------------------------------------------------------------------------
 */

static int
OutputIdChannel	(instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;	/* channel to send the message to */
    CONST84 char *buf;			/* output buffer */
    int   toWrite;		/* number of characters to write */
    int  *errorCodePtr;		/* place to store the POSIX error code */
{
    int tmp;

    Tcl_Channel channelPtr = ((IdentityInfo *)instanceData)->channelPtr;

    tmp = Tcl_Write(channelPtr, buf, toWrite);

    if(tmp == -1) {
        *errorCodePtr = Tcl_GetErrno();
    }

    return tmp;

}



/*
 *-----------------------------------------------------------------------------
 *
 * GFPIdChannel --
 *
 *	"Get file" function for identity channels. Since there are no files
 *	associated with filters, it always returns NULL.
 *
 * Results:
 *
 *	Always NULL.
 *
 * Side effects:
 *
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
#ifndef _TCL76
static int
GFPIdChannel (instanceData, direction, handlePtr)
    ClientData	instanceData;
    int		direction;
    FileHandle *handlePtr;
{
    *handlePtr = NULL;
    return TCL_OK;
}
#else
static Tcl_File
GFPIdChannel (instanceData, direction)
    ClientData	instanceData;
    int		direction;
{
    return NULL;
}
#endif



/*
 *-----------------------------------------------------------------------------
 *
 * SOPIdChannel --
 *
 *	"Set option" procedure for identity channels.
 *
 * Results:
 *
 *	Standard Tcl result.
 *
 * Side effects:
 *
 *	Sets the value of the specified option.
 *
 * ----------------------------------------------------------------------------
 */

static int
SOPIdChannel (instanceData, interp, optionName, optionValue)
    ClientData	 instanceData;	 /* (in) Pointer to IdentityInfo struct. */
    Tcl_Interp	*interp;	 /* (in) Pointer to tcl interpreter. */
    CONST char	*optionName;
    CONST char	*optionValue;
{
    int option;
    int value;

    IdentityInfo data;
    memcpy((void *)&data, (void *)instanceData, sizeof(IdentityInfo));

    /*
     * Set the option specified by optionName
     */

    if (optionName[0] == '-') {
        option = DpTranslateOption(optionName+1);
    } else {
        option = -1;
    }

    switch(option) {

    case DP_PEEK:

        if (Tcl_GetBoolean(interp, optionValue, &value) != TCL_OK) {
            return TCL_ERROR;
        }
        if (value == 0) {
            data.peek = 0;
	    if (Tcl_SetChannelOption(interp, data.channelPtr,
		    "-peek", "no") == TCL_ERROR) {
                Tcl_AppendResult(interp, ": subordinated channel error in ",
                                 Tcl_GetChannelName(data.channelPtr), NULL);
                return TCL_ERROR;
            }
        } else {
	    data.peek = 1;
	    if (Tcl_SetChannelOption(interp, data.channelPtr,
		    "-peek", "yes") == TCL_ERROR) {
                Tcl_AppendResult(interp, ": subordinated channel error in ",
                                 Tcl_GetChannelName(data.channelPtr), NULL);
                return TCL_ERROR;
            }
        }
        break;

    case DP_CHANNEL:

	Tcl_AppendResult(interp, "can't set channel after identity channel ",
			 "is opened", NULL);
	return TCL_ERROR;

    default:
        Tcl_AppendResult (interp, "illegal option \"", optionName, "\" -- ",
			  "must be peek, or a standard fconfigure option", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}



/*
 *--------------------------------------------------------------------------
 *
 * GOPIdChannel --
 *
 *	"Get option" function for identity filters.
 *
 * Results:
 *
 *	Standard Tcl result.
 *
 * Side effects:
 *
 *	Returns the value of a non-standard option. If no option is specified,
 *	a list of all options, together with their values, is returned.
 *
 * -------------------------------------------------------------------------
 */

static int
GOPIdChannel (instanceData,
#if (TCL_MAJOR_VERSION > 7)
	    interp,
#endif
	    optionName, dsPtr)
    ClientData instanceData;
#if (TCL_MAJOR_VERSION > 7)
    Tcl_Interp *interp;
#endif
    CONST84 char	*optionName;
    Tcl_DString *dsPtr;		/* (out) String to store the result in. */
{
    int option;
    int size;

    /*
     * If optionName is NULL, then store an alternating list of
     * all supported options and their current values in dsPtr.
     */

#if (TCL_MAJOR_VERSION > 7)
#define IGO(a, b, c) GOPIdChannel(a, interp, b, c)
#else
#define IGO(a, b, c) GOPIdChannel(a, b, c)
#endif
    if (optionName == NULL) {
     	Tcl_DStringAppend (dsPtr, " -channel ", -1);
	IGO (instanceData, "-channel", dsPtr);
     	Tcl_DStringAppend (dsPtr, " -peek ", -1);
	IGO (instanceData, "-peek", dsPtr);
        return TCL_OK;
    }
#undef IGO

    /*
     * Retrieve the value of the option specified by optionName
     */

    if (optionName[0] == '-') {
        option = DpTranslateOption(optionName+1);
    } else {
        option = -1;
    }

    switch (option) {

    case DP_PEEK:

        if (((IdentityInfo *)instanceData)->peek) {
            Tcl_DStringAppend (dsPtr, "1", -1);
        } else {
            Tcl_DStringAppend (dsPtr, "0", -1);
        }
        break;

    case DP_CHANNEL:

        Tcl_DStringAppend (dsPtr,
	    Tcl_GetChannelName(((IdentityInfo *)instanceData)->channelPtr), -1);
        break;

    default:
#ifndef _TCL76
	Tcl_AppendResult(interp,
		"bad option \"", optionName,"\": must be -blocking,",
		" -buffering, -buffersize, -eofchar, -translation,",
		" or a channel type specific option", NULL);
#else
	{
	    char errStr[128];
	    sprintf(errStr, "bad option \"%s\": must be -blocking,"
		    "-buffering, -buffersize, -eofchar, -translation,"
		    " or a channel type specific option", optionName);
	    Tcl_DStringAppend(dsPtr, errStr, -1);
	}
#endif
        Tcl_SetErrno (EINVAL);
        return TCL_ERROR;
    }

    return TCL_OK;
}



/*
 *-----------------------------------------------------------------------------
 *
 * WCPIdChannel --
 *
 *	This is the "watch channel" procedure for identity filters. It is
 *	assumed that no events are generated internally in the filter channel,
 *	so the procedure only calls the corresponding procedure of the
 *	subordinated channel.
 *
 * Results:
 *
 *	None.
 *
 * Side effects:
 *
 *	Calls the "watch channel" procedure of the subordinated channel.
 *
 * ----------------------------------------------------------------------------
 */

static void
WCPIdChannel (instanceData, mask)
    ClientData  instanceData;	/* (in) Pointer to PlugFInfo struct. */
    int         mask;		/* (in) ORed combination of TCL_READABLE,
                                 * TCL_WRITABLE and TCL_EXCEPTION. It designates
                                 * the event categories that have to be watched.
                                 */
{
    Tcl_Channel channelPtr = ((IdentityInfo *)instanceData)->channelPtr;

#ifdef _TCL76
    (Tcl_GetChannelType(channelPtr)->watchChannelProc)
                         (Tcl_GetChannelInstanceData(channelPtr), mask);
#endif

    return;
}



/*
 *-----------------------------------------------------------------------------
 *
 * CRPIdChannel --
 *
 *	This is the "channel ready" procedure for identity filters. It is
 *	assumed that no events are generated internally in the filter channel,
 *	so the procedure only calls the corresponding procedure of the
 *	subordinated channel.
 *
 * Results:
 *
 *	The value returned by the "channel ready" procedure of the subordinated
 *	channel.
 *
 * Side effects:
 *
 *	Calls the "channel ready" procedure of the subordinated channel.
 *
 * ----------------------------------------------------------------------------
 */

static int
CRPIdChannel (instanceData, mask)
    ClientData  instanceData;	/* (in) Pointer to IdentityInfo struct. */
    int         mask;		/* (in) ORed combination of TCL_READABLE,
                                 * TCL_WRITABLE and TCL_EXCEPTION. It designates
                                 * the event categories whose occurence has to
                                 * be signalled.
                                 */
{
    Tcl_Channel channelPtr = ((IdentityInfo *)instanceData)->channelPtr;

#ifdef _TCL76
    return (Tcl_GetChannelType(channelPtr)->channelReadyProc)
                         (Tcl_GetChannelInstanceData(channelPtr), mask);
#else
    return TCL_OK;
#endif
}


