/*
 * This file holds the functions necessary for the generic layer
 * of the DP serial channel driver.  Platform-specific functions
 * in %OS%/dpSerial.c do most of the work since there really is no
 * standard serial port API.
 *
 * DP knows a serial port by two names: the OS specific name (i.e
 * COM1 or /dev/ttya) and DP's own naming scheme that can be used
 * on every platform for portable Tcl code: serialX where X is a
 * number.  Please see %OS%/dpSerial.c for more details.
 *
 * On Win32, serial2 cooresponds to COM2.  On Unix, serial2
 * cooresponds to the second serial port.
 *
 * YOU CAN ONLY USE ONE NAME TO OPEN A CONNECTION WITH DP_CONNECT:
 * 	dp_connect serial -device serialX
 *
 */

#include <string.h>
#include "generic/dpPort.h"
#include "generic/dpInt.h"

static unsigned int serialCount = 0;

static int 	SerialBlock _ANSI_ARGS_((ClientData instanceData,
                                int mode));
static int      SerialInput _ANSI_ARGS_((ClientData instanceData,
                                char *bufPtr, int bufSize, int *errorPtr));
static int      SerialOutput _ANSI_ARGS_((ClientData instanceData,
				CONST84 char *bufPtr, int toWrite, int *errorPtr));
static int      SerialClose _ANSI_ARGS_((ClientData instanceData,
                                Tcl_Interp *interp));
static int      SerialSetOption _ANSI_ARGS_((ClientData instanceData,
                                Tcl_Interp *interp, CONST char *nameStr, CONST char *val));

#ifndef _TCL76
static int	SerialGetOption _ANSI_ARGS_((ClientData instanceData,
			    	Tcl_Interp *interp, CONST84 char *optionName,
			    	Tcl_DString *dsPtr));
#else
static int	SerialGetOption _ANSI_ARGS_((ClientData instanceData,
			    	char *optionName, Tcl_DString *dsPtr));
#endif

#ifndef _TCL76
static int     	SerialGetFile _ANSI_ARGS_((ClientData instanceData,
                                int direction, FileHandle *handlePtr));
#else
static Tcl_File SerialGetFile _ANSI_ARGS_((ClientData instanceData,
                                int direction));
#endif

#ifdef _TCL76
static int      SerialReady _ANSI_ARGS_((ClientData instanceData,
                                int direction));
#endif

static void     SerialWatch _ANSI_ARGS_((ClientData instanceData,
                                int mask));

static Tcl_ChannelType serialChannelType = {
    "serial",			/* Name of channel */
    DP_CHANNEL_VERSION,	/* TCL_CHANNEL_VERSION_1, TCL_CHANNEL_VERSION_2, and so on */
    SerialClose,		/* Proc to close a socket */
    SerialInput,		/* Proc to get input from a socket */
    SerialOutput,		/* Proc to send output to a socket */
    NULL,				/* Can't seek on a socket! */
    SerialSetOption,	/* Proc to set a socket option */
    SerialGetOption,	/* Proc to set a socket option */
    SerialWatch,		/* Proc called to set event loop wait params */
#ifdef _TCL76
    SerialReady,
#endif
    SerialGetFile,	/* Proc to return a handle assoc with socket */
    NULL,			/* Proc to call to close the channel if the device
					 * supports closing the read & write sides */
    SerialBlock,	/* Proc to set blocking mode on socket */
/* Only valid in TCL_CHANNEL_VERSION_2 channels or later */
    NULL,			/* Proc to call to flush a channel */
    NULL,			/* Proc to call to handle a channel event */
/* Only valid in TCL_CHANNEL_VERSION_3 channels or later */
    NULL,			/* Proc to call to seek on the channel which can handle 64-bit offsets */
/* Only valid in TCL_CHANNEL_VERSION_4 channels or later */
    NULL			/* Proc to notify the driver of thread specific activity for a channel */
};


/* --------------------------------------------------
 *
 *    DpOpenSerialChannel --
 *
 *        Generic routine to open a serial channel
 *
 *    Returns
 *
 *        A Tcl_Channel.
 *
 *    Side Effects
 *
 *        Opens the serial port and allocates memory.
 *
 * ---------------------------------------------------
 */

Tcl_Channel
DpOpenSerialChannel(interp, argc, argv)
    Tcl_Interp *interp;
    int argc;
    CONST84 char **argv;
{
    Tcl_Channel chan;
    SerialState *ssPtr;
    char devStr[15];
    char channelNameStr[10];
    int block, ro;
    int i, flags, mode;

    block = 0;
    ro = 0;
    flags = 0;
    strcpy(devStr, "");

    for (i=0; i<argc; i+=2) {
    	int len = strlen(argv[i]);

    	if (strncmp(argv[i], "-device", len) == 0) {
	    if (i+1 == argc) goto arg_missing;
	    strcpy(devStr, argv[i+1]);
	} else if (strncmp(argv[i], "-block", len) == 0) {
	    if (i+1 == argc) goto arg_missing;
	    if (Tcl_GetBoolean(interp, argv[i+1], &block) == TCL_ERROR) {
	    	return NULL;
	    }
	} else if (strncmp(argv[i], "-readonly", len) == 0) {
	    if (i+1 == argc) goto arg_missing;
	    if (Tcl_GetBoolean(interp, argv[i+1], &ro) == TCL_ERROR) {
	    	return NULL;
	    }
	} else {
	    Tcl_AppendResult(interp, "Unknown option \"", argv[i],
		    "\", must be -device, -block, or -readonly", NULL);
	    return NULL;
	}
    }

    /*
     * Combine the two flags into a bitmask
     */
    if (block) {
	flags = 1;
    }

    mode = TCL_READABLE | TCL_WRITABLE;
    if (ro) {
	flags |= 2;
	mode &= ~(TCL_WRITABLE);
    }

    ssPtr = (SerialState *) ckalloc(sizeof(SerialState));
    if (DppOpenSerialChannel(interp, (ClientData)ssPtr, devStr, flags)
	    != TCL_OK) {
	ckfree((char *)ssPtr);
	return NULL;
    }
#ifdef _TCL76
    ssPtr->theFile = Tcl_GetFile((ClientData)ssPtr->fd, SERIAL_HANDLE);
#else
    ssPtr->theFile = (FileHandle) ssPtr->fd;
#endif

    /*
     * Setup the serial channel to flush every line
     */

    sprintf(channelNameStr, "serial%d", serialCount++);
    chan = Tcl_CreateChannel(&serialChannelType, channelNameStr,
	    (ClientData) ssPtr, mode);
	Tcl_RegisterChannel(interp, chan);

    if (Tcl_SetChannelOption(interp, chan, "-buffering", "line")
	    != TCL_OK) {
        DpClose(interp, chan);
	return NULL;
    }

    ssPtr->channel = chan;

    return chan;

arg_missing:
    Tcl_AppendResult(interp, "Value for \"", argv[i],
    	"\" missing", NULL);
    return NULL;
}


/* --------------------------------------------------
 *
 *    SerialBlock --
 *
 *        Generic routine to set I/O to blocking or
 *	  non-blocking.
 *
 *    Returns
 *
 *        TCL_OK or TCL_ERROR.
 *
 *    Side Effects
 *
 *        None.
 *
 * ---------------------------------------------------
 */

static int
SerialBlock(instanceData, mode)
    ClientData instanceData;
    int mode;			/* (in) Block or not */
{
    return DppSerialBlock(instanceData, mode);
}


/* --------------------------------------------------
 *
 *    SerialInput --
 *
 *        Generic read routine for serial ports
 *
 *    Returns
 *
 *        Amount read or -1 with errorcode in errorPtr.
 *
 *    Side Effects
 *
 *        Buffer is updated.
 *
 * ---------------------------------------------------
 */

static int
SerialInput(instanceData, bufPtr, bufSize, errorPtr)
    ClientData instanceData;
    char *bufPtr;		/* (in) Ptr to buffer */
    int bufSize;		/* (in) sizeof buffer */
    int *errorPtr;		/* (out) error code */
{
    return DppSerialInput(instanceData, bufPtr, bufSize, errorPtr);
}


/* --------------------------------------------------
 *
 *    SerialOutput --
 *
 *        Generic write routine for serial ports
 *
 *    Returns
 *
 *        Amount written or -1 with errorcode in errorPtr
 *
 *    Side Effects
 *
 *        None.
 *
 * ---------------------------------------------------
 */

static int
SerialOutput(instanceData, bufPtr, toWrite, errorPtr)
    ClientData instanceData;
    CONST84 char *bufPtr;		/* (in) Ptr to buffer */
    int toWrite;		/* (in) amount to write */
    int *errorPtr;		/* (out) error code */
{
    return DppSerialOutput(instanceData, bufPtr, toWrite, errorPtr);
}


/* --------------------------------------------------
 *
 *    SerialClose --
 *
 *        Generic routine to close the serial port
 *
 *    Returns
 *
 *        0 if successful or a POSIX errorcode with
 *        interp updated.
 *
 *    Side Effects
 *
 *        Channel is deleted.
 *
 * ---------------------------------------------------
 */

static int
SerialClose(instanceData, interp)
    ClientData instanceData;
    Tcl_Interp *interp;
{
    SerialState *ssPtr = (SerialState *) instanceData;
    int rc = TCL_OK;

    rc = DppSerialClose(instanceData);
    if ((rc != 0) && (interp != NULL)) {
    	Tcl_SetErrno(rc);
    	Tcl_SetResult(interp, (char *) Tcl_PosixError(interp), TCL_VOLATILE);
    }
    ckfree((char *)ssPtr);
    return rc;
}


/* --------------------------------------------------
 *
 *    SerialSetOptions --
 *
 *        Sets "name" to "val".  Possible
 *        options with valid arguments are:
 *
 *            -parity [odd|even|none]
 *            -charsize [7|8]
 *            -stopbits [1|2]
 *	      -baudrate [rate]
 *            -sendBuffer [size]
 *            -recvBuffer [size]
 *
 *    Returns
 *
 *        TCL_OK or TCL_ERROR with interp->result updated
 *
 *    Side Effects
 *
 *        Changes parameters for this channel
 *
 * ---------------------------------------------------
 */

static int
SerialSetOption(instanceData, interp, nameStr, valStr)
    ClientData instanceData;
    Tcl_Interp *interp;
    CONST char *nameStr;		/* (in) Name of option */
    CONST char *valStr;		/* (in) New value of option */
{
    SerialState *ssPtr = (SerialState *) instanceData;
    int optVal, option;
    char errorStr[80];
    int optBool;

    if (nameStr[0] != '-') {
        option = -1;
    } else {
        option = DpTranslateOption(nameStr+1);
    }

    switch (option) {
        case DP_PARITY:
            if (!strcmp(valStr, "none")) {
                optVal = PARITY_NONE;
            } else if (!strcmp(valStr, "even")) {
                optVal = PARITY_EVEN;
            } else if (!strcmp(valStr, "odd")) {
                optVal = PARITY_ODD;
            } else {
                sprintf(errorStr, "Parity must be \"even\", \"odd\" or \"none\"");
                goto argError;
            }
            return DppSerialSetOption(ssPtr, DP_PARITY, optVal);
        case DP_CHARSIZE:
            if (Tcl_GetInt(interp, valStr, &optVal) == TCL_ERROR) {
		return TCL_ERROR;
            }
            if (optVal != 7 && optVal != 8) {
                sprintf(errorStr, "Charsize must be 7 or 8");
                goto argError;
            }
            return DppSerialSetOption(ssPtr, DP_CHARSIZE, optVal);
        case DP_STOPBITS:
            if (Tcl_GetInt(interp, valStr, &optVal) == TCL_ERROR) {
                return TCL_ERROR;
	    }
            if (optVal != 1 && optVal != 2) {
                sprintf(errorStr, "Stopbits must be 1 or 2");
                goto argError;
            }
            return DppSerialSetOption(ssPtr, DP_STOPBITS, optVal);
        case DP_BAUDRATE:
            if (Tcl_GetInt(interp, valStr, &optVal) == TCL_ERROR) {
                return TCL_ERROR;
            }
            return DppSerialSetOption(ssPtr, DP_BAUDRATE, optVal);
	case DP_BLOCK:
	    if (Tcl_GetBoolean(interp, valStr, &optBool) == TCL_ERROR) {
	    	return TCL_ERROR;
	    }
	    return DppSerialSetOption(ssPtr, DP_BLOCK, optBool);
        default:
            Tcl_AppendResult (interp, "Illegal option \"", nameStr,
                    "\" -- must be charsize, stopbits, parity, baudrate, \
                     or a standard channel option", NULL);
            return TCL_ERROR;
    }

argError:
    Tcl_AppendResult(interp, errorStr, (char *) NULL);
    return TCL_ERROR;
}


/* ----------------------------------------------------
 *
 *    SerialGetOption --
 *
 *	Queries serial channel for the current value of
 *      the given option.
 *
 *    Returns
 *
 *	TCL_OK and dsPtr updated with the value or
 *	TCL_ERROR.
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

static int
SerialGetOption(instanceData,
#if (TCL_MAJOR_VERSION > 7)
		interp,
#endif
		optionName, dsPtr)
    ClientData instanceData;
#if (TCL_MAJOR_VERSION > 7)
    Tcl_Interp *interp;
#endif
    CONST84 char *optionName;		/* (in) Name of option to retrieve */
    Tcl_DString *dsPtr;		/* (in) String to place value */
{
    SerialState *ssPtr = (SerialState *) instanceData;
    int option;

    if (optionName != NULL) {
	if (optionName[0] != '-') {
	    option = -1;
	} else {
	    option = DpTranslateOption(optionName+1);
	}
    } else {
	Tcl_DStringAppend(dsPtr, " -charsize ", -1);
	DppSerialGetOption(ssPtr, DP_CHARSIZE, dsPtr);
	Tcl_DStringAppend(dsPtr, " -stopbits ", -1);
	DppSerialGetOption(ssPtr, DP_STOPBITS, dsPtr);
	Tcl_DStringAppend(dsPtr, " -baudrate ", -1);
	DppSerialGetOption(ssPtr, DP_BAUDRATE, dsPtr);
	Tcl_DStringAppend(dsPtr, " -parity ", -1);
	DppSerialGetOption(ssPtr, DP_PARITY, dsPtr);
	Tcl_DStringAppend(dsPtr, " -device ", -1);
	DppSerialGetOption(ssPtr, DP_DEVICENAME, dsPtr);
        return TCL_OK;
    }

    if (option == -1)
    {
#ifndef _TCL76
	Tcl_AppendResult(interp,
		"bad option \"", optionName,"\": must be -blocking,",
		" -buffering, -buffersize, -eofchar, -translation,",
		" or a channel type specific option", NULL);
#else
	char errStr[128];
	sprintf(errStr, "bad option \"%s\": must be -blocking,"
		"-buffering, -buffersize, -eofchar, -translation,"
		" or a channel type specific option", optionName);
	Tcl_DStringAppend(dsPtr, errStr, -1);
#endif
	Tcl_SetErrno(EINVAL);
	return TCL_ERROR;
    }

    return DppSerialGetOption(ssPtr, option, optionName, dsPtr);
}

/* ----------------------------------------------------
 *
 *    SerialGetFile --
 *
 *	See below
 *
 *    Returns
 *
 *	TCL_OK
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

#ifndef _TCL76
static int
SerialGetFile(instanceData, direction, handlePtr)
    ClientData instanceData;
    int direction;
    FileHandle *handlePtr;
{
    SerialState *statePtr = (SerialState *)instanceData;

    *handlePtr = statePtr->theFile;
    return TCL_OK;
}

#else

static Tcl_File
SerialGetFile(instanceData, direction)
    ClientData instanceData;
    int direction;
{
    SerialState *statePtr = (SerialState *)instanceData;

    return statePtr->theFile;
}
#endif


/* ----------------------------------------------------
 *
 *    SerialReady --
 *
 *	Determines whether serial port has data to be
 *	read or is OK for writing.
 *
 *    Returns
 *
 *	A bitmask of the events that were found (i.e.
 *	TCL_FILE_READABLE | TCL_FILE_WRITABLE).
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

#ifdef _TCL76
static int
SerialReady(instanceData, direction)
    ClientData instanceData;
    int direction;
{
    return DppSerialFileReady(instanceData, direction);
}
#endif


/* ----------------------------------------------------
 *
 *    SerialWatch --
 *
 *	Sets up event handling on a serial port Tcl_Channel
 *
 *    Returns
 *
 *	Nothing
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

static void
SerialWatch(instanceData, mask)
    ClientData instanceData;
    int mask;
{
    DppSerialWatchFile(instanceData, mask);
}


