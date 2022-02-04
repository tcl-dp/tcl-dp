/*
 * generic/dpPlugF.c --
 *
 * This file contains the implementation of the plug-in filter (PIF)
 * channel. These are channels that are created by evaluating
 * "dp_connect filter".
 */

/*
 * Major unsolved problems:
 *
 * 1. Should the PIF channel set the nonblocking option for the subordinated
 * channel?
 */

#include <string.h>
#include "generic/dpInt.h"

#define DP_ARBITRARY_LIMIT 500

typedef struct {
    char *outBuf;
    int   outLength;
    int   outUsed;
    int   eof;
} FiltBuffer;

typedef struct {
    Tcl_Channel channelPtr;
    int         peek;
    FiltBuffer  i;
    Tcl_Interp *interp;
    Dp_PlugInFilterProc *inFilter;
    Dp_PlugInFilterProc *outFilter;
    void       *inData;
    void       *outData;
} PlugFInfo;


/*
 * Prototypes for functions referenced only in this file.
 */

static int	ClosePlugFChannel	_ANSI_ARGS_((ClientData instanceData,
                                                     Tcl_Interp *interp));

static int      InputPlugFChannel	_ANSI_ARGS_((ClientData instanceData,
                                                     char *buf, int bufsize,
                                                     int  *errorCodePtr));

static int      OutputPlugFChannel	_ANSI_ARGS_((ClientData instanceData,
                                                      CONST84 char *buf, int toWrite,
                                                      int *errorCodePtr));

static int      SOPPlugFChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     Tcl_Interp *interp,
                                                     CONST char *optionName,
                                                     CONST char *optionValue));

#ifdef _TCL76
static int	GOPPlugFChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     char *optionName,
                                                     Tcl_DString *dsPtr));
#else
static int	GOPPlugFChannel		_ANSI_ARGS_((ClientData instanceData,
						     Tcl_Interp *interp,
                                                     CONST84 char *optionName,
                                                     Tcl_DString *dsPtr));
#endif

#ifndef _TCL76
static int	GFPPlugFChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     int direction,
                                                     FileHandle *handlePtr));
#else
static Tcl_File	GFPPlugFChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     int direction));
#endif

static int	CRPPlugFChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     int mask));

static void	WCPPlugFChannel		_ANSI_ARGS_((ClientData instanceData,
                                                     int mask));


/*
 * This structure stores the names of the functions that Tcl calls when certain
 * actions have to be performed on a PIF channel. To understand this entry,
 * please refer to the documentation of the Tcl_CreateChannel and its associated
 * functions in the Tcl 7.6 documentation.
 *
 * A PIF channel will always be non-blocking.
 * Seek on a PIF channel is not allowed.
 */

Tcl_ChannelType plugFChannelType = {
    "plugfilter",
    DP_CHANNEL_VERSION,	/* TCL_CHANNEL_VERSION_1, TCL_CHANNEL_VERSION_2, and so on */
    ClosePlugFChannel,    /* closeProc        */
    InputPlugFChannel,    /* inputProc        */
    OutputPlugFChannel,   /* outputProc       */
    NULL,                 /* seekProc         */
    SOPPlugFChannel,      /* setOptionProc    */
    GOPPlugFChannel,      /* getOptionProc    */
    WCPPlugFChannel,      /* watchChannelProc */
    GFPPlugFChannel,       /* getFileProc      */
    NULL,			/* Proc to call to close the channel if the device
					 * supports closing the read & write sides */
    NULL,			/* Proc to set blocking mode on socket */
    /* Only valid in TCL_CHANNEL_VERSION_2 channels or later */
    NULL,			/* Proc to call to flush a channel */
    NULL,			/* Proc to call to handle a channel event */
    /* Only valid in TCL_CHANNEL_VERSION_3 channels or later */
    NULL,			/* Proc to call to seek on the channel which can handle 64-bit offsets */
    /* Only valid in TCL_CHANNEL_VERSION_4 channels or later */
    NULL			/* Proc to notify the driver of thread specific activity for a channel */
};


/*
 *-----------------------------------------------------------------------------
 *
 * DpCreatePlugFChannel --
 *
 *	Creates a PIF channel.
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
DpCreatePlugFChannel (interp, argc, argv)
    Tcl_Interp *interp;		/* (in) Pointer to tcl interpreter. */
    int         argc;		/* (in) Number of arguments. */
    CONST84 char      **argv;           /* (in) Argument strings. */
{
    static int    openedChannels = 0;
    int           i;
    PlugFInfo    *instanceData;
    Tcl_Channel   newChannel;
    char          chanName [20];


    instanceData = (PlugFInfo *)ckalloc(sizeof(PlugFInfo));

    if (instanceData == NULL) {
        Tcl_SetErrno(ENOMEM);
        Tcl_AppendResult(interp, "unable to allocate memory for plug-in filter ",
		"channel", NULL);
        return NULL;
    }

    /* Install the default identity filters. */

    instanceData->channelPtr = NULL;
    instanceData->inFilter   = Dp_GetFilterPtr (interp, "identity");

    if (instanceData->inFilter == NULL) {
        Tcl_AppendResult(interp, "unable to find identity plug-in filter",
		 NULL);
        return NULL;
    }

    instanceData->outFilter = Dp_GetFilterPtr (interp, "identity");

    if (instanceData->outFilter == NULL) {
        Tcl_AppendResult(interp, "unable to find identity plug-in filter",
		 NULL);
        return NULL;
    }

    /* Identify the given options and take appropriate actions. */

    for (i = 0; i < argc; i += 2) {
        int v = i+1;
	size_t len = strlen(argv[i]);

	if (strncmp(argv[i], "-channel", len)==0) {
	    if (v == argc) {goto error2;}

            instanceData->channelPtr = Tcl_GetChannel(interp, argv[v], NULL);
            if (instanceData->channelPtr == NULL) {
                goto error1;
            }
        } else if (strncmp(argv[i], "-infilter", len)==0) {
	    if (v == argc) {goto error2;}

            instanceData->inFilter =  Dp_GetFilterPtr (interp, argv[v]);
            if (instanceData->inFilter == NULL) {
                Tcl_AppendResult(interp, "unable to find plug-in filter ",
			argv[v], NULL);
                goto error1;
            }
        } else if (strncmp(argv[i], "-outfilter", len)==0) {
	    if (v == argc) {goto error2;}

            instanceData->outFilter =  Dp_GetFilterPtr (interp, argv[v]);
            if (instanceData->outFilter == NULL) {
                Tcl_AppendResult(interp, "unable to find plug-in filter ",
			argv[v], NULL);
                goto error1;
            }
	} else {
    	    Tcl_AppendResult(interp, "unknown option \"",
		    argv[i], "\", must be -channel", NULL);
	    goto error1;
	}
    }

    if(instanceData->channelPtr == NULL) {
        Tcl_AppendResult(interp, "-channel must be defined for a plug-in",
		" channel", NULL);
        goto error1;
    }

    /* No peek by default. */
    instanceData->peek = 0;

    /*
     * A PIF channel is always both writable and readable. The real behavior
     * depends on the properties of the subordinated channel.
     */

    sprintf(chanName, "plugfilter%d", openedChannels++);
    newChannel = Tcl_CreateChannel(&plugFChannelType, chanName,
    		(ClientData)instanceData, TCL_READABLE | TCL_WRITABLE);
    if (newChannel == NULL) {
        Tcl_AppendResult(interp, "Unable to create plug-in channel", NULL);
        goto error1;
    }
    Tcl_RegisterChannel(interp, newChannel);

    /*
     * Initialize the data related to buffering. Notice the asymmetry
     * between the handling of input and output buffers.
     */

    instanceData->i.outBuf      = NULL;
    instanceData->i.outLength   = 0;
    instanceData->i.outUsed     = 0;
    instanceData->i.eof         = 0;

    instanceData->interp        = interp;

    instanceData->inData        = NULL;
    instanceData->outData       = NULL;

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
 * ClosePlugFChannel --
 *
 *	Closes the given PIF channel.
 *
 * Results:
 *
 *	If everything goes well, returns 0. If any error happens,
 *      it returns a POSIX error code.
 *
 * Side effects:
 *
 *	1. It calls the plug-in filters indicating that there
 *	they should release all the memory they allocated and should
 *	return the data they might still buffer internally.
 *	2. It writes the data that it returned by the output filter on the
 *	subordinated channel.
 *	3. It frees all the internal channel buffers.
 *	4. It frees the instance data associated with the channel.
 *
 *-----------------------------------------------------------------------------
 */

static int
ClosePlugFChannel (instanceData, interp)
    ClientData  instanceData;  /* (in) Pointer to PlugFInfo struct. */
    Tcl_Interp *interp;        /* (in) Pointer to tcl interpreter. */
{
    char *outBuf;
    int   outLength, error, status, tmp;

    PlugFInfo *data = (PlugFInfo *)instanceData;

    status = 0;

    if (data->i.outBuf != NULL) {
        ckfree(data->i.outBuf);
        /*
         * If close fails, and is repeated later, this will prevent
         * freeing the buffer again.
         */
        data->i.outBuf = NULL;
    }

    /*
     * In case the data was incomplete, and the filter was waiting for
     * more data, this will signal that now it is the last chance to
     * write to the subordinated channel. Also, all memory allocated
     * by the filter should be released now.
     */

    error = (data->outFilter) (NULL, 0, &outBuf, &outLength,
                               &(data->outData), data->interp, DP_FILTER_CLOSE);
    Tcl_SetErrno(error);

    if (error != 0) {
        Tcl_SetErrno(error);

        /*
         * Do not free instance data - the user might take some corrective
         * action based on the POSIX error code, could even write to the
         * channel, and then attept to close it again.
         */

        return -1;
    }

    if (outLength > 0) {
        tmp = Tcl_Write(data->channelPtr, outBuf, outLength);

        if (tmp == -1) {
            status = -1;
        } else if (tmp != outLength) {
            /*
             * We could not write everything to the subordinated channel.
             * Try again, if it fails, report the error.
             */
            int tmp1;

            tmp1 = Tcl_Write(data->channelPtr, outBuf + tmp, outLength - tmp);
            if (tmp1 != (outLength - tmp)) {
                Tcl_SetErrno(ENOSPC);
                status = -1;
            }
        }
	ckfree(outBuf);
    }

    /* If the channel is closed, nobody is interested in reading from
     * it anymore. Signall to the filter and ignore the output.
     */

    error = (data->inFilter) (NULL, 0, &outBuf, &outLength,
                               &(data->inData), data->interp, DP_FILTER_CLOSE);
    Tcl_SetErrno(error);

    if (error != 0) {
        Tcl_SetErrno(error);

        /*
         * Do not free instance data - the user might take some corrective
         * action based on the POSIX error code, could even write to the
         * channel, and then attept to close it again.
         */

        return -1;
    }

    if (outLength > 0) {
        ckfree(outBuf);
    }

    if (instanceData != NULL) {
        ckfree((char *)instanceData);
    }

    return status;
}



/*
 *-----------------------------------------------------------------------------
 *
 * InputPlugFChannel --
 *
 *	Reads data from the subordinated channel and feeds it into the input
 *	filter. It continues until the filter outputs at least as much data
 *	as it was requested, or until there is no more data to be read from
 *	the subordinated channel.
 *
 * Results:
 *
 *	Number of bytes output by the filter, which is at most the amount
 *	requested. If the filter returns more bytes that requested, the
 *	difference is buffered internally. If an error happened, the return
 *	is -1.
 *
 * Side effects:
 *
 *	1. Calls the read procedure of the subordinated channel.
 *	2. Modifies the buffers associated with the input filter.
 *	3. Stores a POSIX code at errorBuffer if an error occurs.
 *	4. The data that is returned is stored in buf.
 *
 *-----------------------------------------------------------------------------
 */

static int
InputPlugFChannel (instanceData, buf, bufsize, errorCodePtr)
    ClientData	instanceData;     /* (in) Pointer to PlugFInfo struct. */
    char       *buf;		  /* (in/out) Buffer to fill. */
    int	        bufsize;	  /* (in) Size of buffer. */
    int	       *errorCodePtr;	  /* (out) POSIX error code (if any). */
{
    int transferred, count, inUsed, inBufLength;
    char inBuf [20 * DP_ARBITRARY_LIMIT];
    FiltBuffer *x;

    PlugFInfo *data = (PlugFInfo *)instanceData;

    inBufLength    = sizeof(inBuf);
    x              = &(data->i);
    inUsed         = 0;

    transferred = 0;
    count = 0;

    while (transferred < bufsize) {
        if (x->outLength > 0) {
            if (bufsize - transferred < x->outLength - x->outUsed) {
                memcpy(buf + transferred, x->outBuf + x->outUsed,
		       bufsize - transferred);
                x->outUsed += (bufsize  - transferred);
                transferred += (bufsize  - transferred);
            } else {
                memcpy(buf + transferred, x->outBuf + x->outUsed,
		       x->outLength - x->outUsed);
                transferred += (x->outLength - x->outUsed);
                x->outUsed = x->outLength;
            }

            if (x->outUsed == x->outLength) {
                x->outLength = 0;
                x->outUsed = 0;
                if (x->outBuf != NULL) {
                    ckfree(x->outBuf);
		}
                x->outBuf = NULL;
            }
        } else { /* outLength == 0 */
            /* Try to get some output from the filter. */

            int error;

            if(!(x->eof)) {
                error = (data->inFilter) (inBuf, inUsed, &(x->outBuf),
                                          &(x->outLength), &(data->inData),
                                          data->interp, DP_FILTER_NORMAL);
            } else {
                error = (data->inFilter) (inBuf, inUsed, &(x->outBuf),
                                          &(x->outLength), &(data->inData),
                                          data->interp, DP_FILTER_EOF);
            }

	    inUsed = 0;

            if (error != 0) {
                *errorCodePtr = error;
                return -1;
            }

            if(x->outLength == 0) {
                /*
                 * We got no data from the filter. Try to read something from
                 * the subordinated channel, and pipe it later in the filter.
                 */

                int newData;

                if(!(x->eof)) {
                    newData = Tcl_Read(data->channelPtr, inBuf, inBufLength);
                } else {
                    newData = 0;
                }

                if (newData == -1) {
                    *errorCodePtr = Tcl_GetErrno();
                    return -1;
                } else if (newData == 0) {

                    /* No data available in the subordinated channel. */

                    /* Did the underlying channel reach eof? */
                    if(!(x->eof)) {
                        if(Tcl_Eof(data->channelPtr)) {
                            x->eof = 1;
                        }
                    }

                    count++;
                    if ((count == 2) || (x->eof == 1)) {
                        return transferred;
                    }
                } else {
                    count = 0;
                }
		inUsed = newData;
            }
        }
    }

    return transferred;
}



/*
 *-----------------------------------------------------------------------------
 *
 * OutputPlugFChannel --
 *
 *	Feeds the data through the output filter. If the filter produces any
 *	output it writes it to the subordinated channel. If the filter
 *	can not process the data completely, the difference is buffered
 *	internally.
 *
 * Results:
 *
 *	Number of bytes "written" (fed into the filter), or -1 if an error
 *	is signalled. If there is no error, the returned value always coincides
 *	with the request amount.
 *
 * Side effects:
 *
 *	1. Calls the write procedure of the subordinated channel.
 *	2. Modifies the buffers associated with the output filter.
 *	3. Stores a POSIX code at errorBuffer if an error occurs.
 *
 *-----------------------------------------------------------------------------
 */

static int
OutputPlugFChannel	(instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to PlugFInfo struct. */
    CONST84 char *buf;			/* (in) Buffer to write. */
    int   toWrite;		/* (in) Number of bytes to write. */
    int  *errorCodePtr;		/* (out) POSIX error code (if any). */
{
    int   tmp, error, outLength, mode;
    char *outBuf = NULL;
    Tcl_DString option;

    char *cx;

    PlugFInfo *data = (PlugFInfo *)instanceData;

    Tcl_DStringInit(&option);
    Tcl_GetChannelOption(
#if (TCL_MAJOR_VERSION > 7)
			data->interp,
#endif
			data->channelPtr, "-buffering", &option);
    cx = Tcl_DStringValue(&option);

    if (strcmp(cx, "none")) {
        /* Buffering is "line" or "full". */
        mode = DP_FILTER_FLUSH;
    } else {
        /* Buffering is "none". */
        mode = DP_FILTER_NORMAL;
    }
    Tcl_DStringFree(&option);

    error = (data->outFilter) (buf, toWrite, &outBuf, &outLength,
                               &(data->outData), data->interp, mode);

    if (error != 0) {
        *errorCodePtr = error;
        goto error1;
    }

    if (outLength > 0) {
        tmp = Tcl_Write(data->channelPtr, outBuf, outLength);

        if (tmp == -1) {
            *errorCodePtr = Tcl_GetErrno();
            goto error1;
        } else if (tmp != outLength) {
            /*
             * We could not write everything to the subordinated channel.
             * Try again, if it fails, report the error.
             */
            int tmp1;

            tmp1 = Tcl_Write(data->channelPtr, outBuf + tmp, outLength - tmp);

            if (tmp1 != outLength - tmp) {
                *errorCodePtr = ENOSPC;
                goto error1;
            }
        }
    }

    if (outBuf != NULL) {
        ckfree(outBuf);
    }
    return toWrite;

error1:
    if (outBuf != NULL) {
        ckfree(outBuf);
    }
    return -1;
}



/*
 *-----------------------------------------------------------------------------
 *
 * GFPPlugFChannel --
 *
 *	"Get file" function for PIF channels. Since there are no files
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
 *-----------------------------------------------------------------------------
 */
	/* ARGSUSED */
#ifndef _TCL76
static int
GFPPlugFChannel (instanceData, direction, handlePtr)
    ClientData	instanceData;
    int		direction;
    FileHandle *handlePtr;
{
    *handlePtr = NULL;
    return TCL_OK;
}
#else
static Tcl_File
GFPPlugFChannel (instanceData, direction)
    ClientData	instanceData;
    int		direction;
{
    return NULL;
}
#endif



/*
 *-----------------------------------------------------------------------------
 *
 * SOPPlugFChannel --
 *
 *	"Set option" procedure for PIF channels.
 *
 * Results:
 *
 *	Standard Tcl result.
 *
 * Side effects:
 *
 *	Sets the value of the specified option.
 *
 *-----------------------------------------------------------------------------
 */

static int
SOPPlugFChannel (instanceData, interp, optionName, optionValue)
    ClientData	 instanceData;  /* (in) Pointer to PlugFInfo struct. */
    Tcl_Interp	*interp;        /* (in) Pointer to tcl interpreter. */
    CONST char	*optionName;
    CONST char	*optionValue;
{
    int option, value, error;

    PlugFInfo *data = (PlugFInfo *)instanceData;

    /*
     * Set the option specified by optionName.
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
		data->peek = 0;
		if (Tcl_SetChannelOption(interp, data->channelPtr, "-peek",
			"no") == TCL_ERROR) {
		    Tcl_AppendResult(interp,
			    ": subordinated channel error in ",
                            Tcl_GetChannelName(data->channelPtr), NULL);
		    return TCL_ERROR;
		}
	    } else {
		data->peek = 1;
		if (Tcl_SetChannelOption(interp, data->channelPtr, "-peek",
			"yes") == TCL_ERROR) {
		    Tcl_AppendResult(interp,
			    ": subordinated channel error in ",
			    Tcl_GetChannelName(data->channelPtr), NULL);
		    return TCL_ERROR;
		}
	    }
	    break;

	case DP_CHANNEL:
	    Tcl_AppendResult(interp, "can't set channel after plug-in",
		    " channel is opened", NULL);
	    return TCL_ERROR;

	case DP_INFILTER:
	    Tcl_AppendResult(interp, "can't set infilter after plug-in",
		    " channel is opened", NULL);
	    return TCL_ERROR;

	case DP_OUTFILTER:
	    Tcl_AppendResult(interp, "can't set outfilter after plug-in",
		    " channel is opened", NULL);
	    return TCL_ERROR;

	case DP_OUTSET:
	    error = (data->outFilter) (optionValue, strlen(optionValue), NULL,
			NULL, &(data->outData), data->interp, DP_FILTER_SET);

	    if (error != 0) {
		Tcl_AppendResult(interp, "can't set option ", optionValue,
			" for output filter",  NULL);
		return TCL_ERROR;
	    }
	    break;

	case DP_INSET:
	    error = (data->inFilter) (optionValue, strlen(optionValue), NULL,
		       NULL, &(data->inData), data->interp, DP_FILTER_SET);

	    if (error != 0) {
		Tcl_AppendResult(interp, "can't set option ", optionValue,
			" for input filter",  NULL);
		return TCL_ERROR;
	    }
	    break;

	default:
	    Tcl_AppendResult (interp, "bad option \"", optionName,
		    "\": must be peek, infilter, outfilter or a standard",
		    "fconfigure option", NULL);
	    return TCL_ERROR;
    }
    return TCL_OK;
}



/*
 *-----------------------------------------------------------------------------
 *
 * GOPPlugFChannel --
 *
 *	"Get option" function for PIF channels.
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
 *-----------------------------------------------------------------------------
 */

static int
GOPPlugFChannel (instanceData,
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
    char *internal;

    PlugFInfo *data = (PlugFInfo *)instanceData;

    /*
     * If optionName is NULL, then store an alternating list of
     * all supported options and their current values in dsPtr.
     */
#if (TCL_MAJOR_VERSION > 7)
#define IGO(a, b, c) GOPPlugFChannel(a, interp, b, c)
#else
#define IGO(a, b, c) GOPPlugFChannel(a, b, c)
#endif
    if (optionName == NULL) {
     	Tcl_DStringAppend (dsPtr, " -channel ", -1);
	IGO (instanceData, "-channel", dsPtr);
     	Tcl_DStringAppend (dsPtr, " -peek ", -1);
	IGO (instanceData, "-peek", dsPtr);
     	Tcl_DStringAppend (dsPtr, " -inset ", -1);
	IGO (instanceData, "-inset", dsPtr);
     	Tcl_DStringAppend (dsPtr, " -outset ", -1);
	IGO (instanceData, "-outset", dsPtr);
        return TCL_OK;
    }
#undef IGO

    /*
     * Retrieve the value of the option specified by optionName.
     */

    if (optionName[0] == '-') {
        option = DpTranslateOption(optionName+1);
    } else {
        option = -1;
    }

    switch (option) {
	case DP_PEEK:
	    if (data->peek) {
		Tcl_DStringAppend (dsPtr, "1", -1);
	    } else {
		Tcl_DStringAppend (dsPtr, "0", -1);
	    }
	    break;

	case DP_CHANNEL:
	    Tcl_DStringAppend (dsPtr, Tcl_GetChannelName(data->channelPtr), -1);
	    break;

	case DP_INFILTER:
	    Tcl_DStringAppend (dsPtr, Dp_GetFilterName(data->inFilter), -1);
	    break;

	case DP_OUTFILTER:
	    Tcl_DStringAppend (dsPtr, Dp_GetFilterName(data->outFilter), -1);
	    break;

	case DP_INSET:
	    (data->inFilter) (NULL, 0, &internal, NULL,
                              &(data->inData), data->interp, DP_FILTER_GET);
	    Tcl_DStringAppend (dsPtr, internal, -1);
	    break;

	case DP_OUTSET:
	    (data->outFilter) (NULL, 0, &internal, NULL,
                               &(data->outData), data->interp, DP_FILTER_GET);
	    Tcl_DStringAppend (dsPtr, internal, -1);
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
 * WCPPlugFChannel --
 *
 *	This is the "watch channel" procedure for PIF channels. It is assumed
 *	that no events are generated internally in the filter channel, so the
 *	procedure only calls the corresponding procedure of the subordinated
 *	channel.
 *
 * Results:
 *
 *	None.
 *
 * Side effects:
 *
 *	Calls the "watch channel" procedure of the subordinated channel.
 *
 *-----------------------------------------------------------------------------
 */

static void
WCPPlugFChannel (instanceData, mask)
    ClientData  instanceData;	/* (in) Pointer to PlugFInfo struct. */
    int         mask;		/* (in) ORed combination of TCL_READABLE,
                                 * TCL_WRITABLE and TCL_EXCEPTION. It designates
                                 * the event categories that have to be watched.
                                 */
{
    Tcl_Channel channelPtr = ((PlugFInfo *)instanceData)->channelPtr;

#ifdef _TCL76
    (Tcl_GetChannelType(channelPtr)->watchChannelProc)
                         (Tcl_GetChannelInstanceData(channelPtr), mask);
#endif
    return;
}



/*
 *-----------------------------------------------------------------------------
 *
 * CRPPlugFChannel --
 *
 *	This is the "channel ready" procedure for PIF channels. It is assumed
 *	that no events are generated internally in the filter channel, so the
 *	procedure only calls the corresponding procedure of the subordinated
 *	channel.
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
 *-----------------------------------------------------------------------------
 */

static int
CRPPlugFChannel (instanceData, mask)
    ClientData  instanceData;	/* (in) Pointer to PlugFInfo struct. */
    int         mask;		/* (in) ORed combination of TCL_READABLE,
                                 * TCL_WRITABLE and TCL_EXCEPTION. It designates
                                 * the event categories whose occurence has to
                                 * be signalled.
                                 */
{
    Tcl_Channel channelPtr = ((PlugFInfo *)instanceData)->channelPtr;

#ifdef _TCL76
    return (Tcl_GetChannelType(channelPtr)->channelReadyProc)
                         (Tcl_GetChannelInstanceData(channelPtr), mask);
#else
    return 1; 	/* to prevent compilation errors - Tcl 8.0 doesn't use
    		// this function so this should never be executed */
#endif
}





