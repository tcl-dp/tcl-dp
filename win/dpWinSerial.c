/*
 * win/dpSerial.c
 *
 *	Win32 Tcl_Channel implementation for serial ports
 */

#include "generic/dpPort.h"
#include "generic/dpInt.h"

/*
 * This is a Tcl function that is not exported but
 * is very handy for error reporting so we use it
 * anyhow.
 */

//extern void TclWinConvertError(DWORD errCode);

#define MAX_LENGTH		10
#define NUM_PORTS		4

static char *portNames[NUM_PORTS] = {
    "COM1",
    "COM2",
    "COM3",
    "COM4"
};

static unsigned long serialCount = 0;

int		DppOpenSerialChannel _ANSI_ARGS_((Tcl_Interp *interp,
			    ClientData instanceData, char *devStr, int flags));
int		DppSerialBlock _ANSI_ARGS_((ClientData instanceData,
			    int mode));
int		DppSerialClose _ANSI_ARGS_((ClientData instanceData));
int		DppSerialInput _ANSI_ARGS_((ClientData instanceData,
			    char *bufPtr, int bufSize, int *errorCodePtr));
int		DppSerialOutput _ANSI_ARGS_((ClientData instanceData,
			    char *bufPtr, int toWrite, int *errorCodePtr));
int		DppSerialSetOption _ANSI_ARGS_((ClientData instanceData,
			    int optionName, int val));
int		DppSerialGetOption _ANSI_ARGS_((ClientData instanceData,
			    int opt, char *optionName, 
			    Tcl_DString *dsPtr));
int		DppSerialFileReady _ANSI_ARGS_((ClientData instanceData,
			    int mask));
void		DppSerialWatchFile _ANSI_ARGS_((ClientData instanceData,
			    int mask));
static char *	DppBaudRateConsToStr _ANSI_ARGS_((int rate));
static int	DppBaudRateNumToCons _ANSI_ARGS_((int rate));

static char *	DppCheckDevice _ANSI_ARGS_((char *devStr));



/* ------------------------------------------------
 *
 *  DppOpenSerialChannel -
 *
 *      Creates a DP channel using the serial port specified 
 *      in dev (i.e. "COM1" or "COM2")
 *
 *  	We do not allow nonblocking IO since we are not
 *      multithreaded.
 *  
 *  Returns 
 *      
 *      Tcl_Channel used for I/O.
 *
 *  Side Effects
 *
 *      Memory allocated.
 *
 * ------------------------------------------------  
 */

int
DppOpenSerialChannel(interp, instanceData, devStr, flags)
    Tcl_Interp *interp;
    ClientData instanceData;
    char *devStr;		/* (in) device to use  */
    int flags;			/* Bit 0: block Bit 1: read-only */
{
    SerialState *ssPtr = (SerialState *) instanceData;
    char *openStr;
    HANDLE fd;
    DCB dcb;
    char channelName[10];
    Tcl_Channel chan;
    int mode = GENERIC_WRITE;

    if ((openStr = DppCheckDevice(devStr)) == NULL) {
	Tcl_AppendResult(interp, "Unknown device \"", devStr, "\"", NULL);
	return TCL_ERROR;
    }

    if (flags & 0x2) {
	mode = 0;
    }

    fd = CreateFile(openStr, GENERIC_READ | mode,
		0, NULL, OPEN_EXISTING, 0, NULL);
				    	
    if (fd == INVALID_HANDLE_VALUE) {
       	TclWinConvertError(GetLastError());
        Tcl_AppendResult(interp, "Error opening ", openStr, ": ",
        	Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    
    /*
     * Setup serial port to a default configuration
     */

    GetCommState(fd, &dcb);
    if (!BuildCommDCB("19200,N,8,1", &dcb)) {
    	goto error;
    }
    SetCommState(fd, &dcb);
    
    ssPtr->fd = fd;
    strcpy(ssPtr->deviceName, devStr);

    /*
     * Set blocking mode for port
     */

    if (DppSerialSetOption((ClientData)ssPtr, DP_BLOCK, flags & 0x1)
	    == TCL_ERROR) {
	 goto error;
    }
    return TCL_OK;

error:
    TclWinConvertError(GetLastError());
    Tcl_AppendResult(interp, "Error configuring serial device: ",
	    Tcl_PosixError(interp), NULL);
    return TCL_ERROR;
}

/* --------------------------------------------------
 *
 *    DppSerialBlock --
 *
 *      Sets serial channel to block or not.
 *
 *	Our non-blocking implementation is a bit
 *	strange.  We are not using some sort of
 *	callback/event mechanism but rather
 *	emulating not blocking by simply reading
 *	all we can at that moment and returning it.
 *	The user MUST check to make sure the entire
 *	message was received when reading or else
 *	turning blocking on.
 *
 *    Returns
 *
 *        TCL_OK or TCL_ERROR 
 *   
 *    Side Effects
 *
 *        None.
 *
 * ---------------------------------------------------
 */

int
DppSerialBlock(instanceData, mode)
    ClientData instanceData;
    int mode;
{
    if (mode == TCL_MODE_BLOCKING) {
	return DppSerialSetOption(instanceData, DP_BLOCK, 1);
    } else {
	return DppSerialSetOption(instanceData, DP_BLOCK, 0);
    }
}

/* --------------------------------------------------
 *
 *    SerialClose --
 *
 *        Closes the serial port and frees memory
 *        associated with the port.
 *
 *    Returns
 *
 *        TCL_OK or TCL_ERROR
 *    
 *    Side Effects
 *
 *        Channel is no longer available.
 *
 * ---------------------------------------------------
 */

int 
DppSerialClose(instanceData)
    ClientData instanceData;
{
    SerialState *ssPtr = (SerialState *) instanceData;
    BOOL rc;

    FlushFileBuffers(ssPtr->fd);	
    rc = CloseHandle(ssPtr->fd);
    ckfree((char *)ssPtr);
    if (!rc) {
    	return TCL_ERROR;
    } else {
    	return TCL_OK;
    }
}

/* --------------------------------------------------
 *
 *    SerialInput --
 *
 *        Reads upto bufSize bytes from serial port
 *        into buf.
 *
 *    Returns
 *
 *        Number of bytes read or -1 with Win32 error code
 *        in errorCodePtr.
 *    
 *    Side Effects
 *
 *        buf is modified.
 *
 * -------------------------------------------------
 */

int
DppSerialInput(instanceData, bufPtr, bufSize, errorCodePtr)
    ClientData instanceData;
    char *bufPtr;
    int bufSize;
    int *errorCodePtr;
{
    BOOL rc;
    DWORD amount;
    SerialState *ssPtr = (SerialState *) instanceData;

    rc = ReadFile(ssPtr->fd, bufPtr, bufSize, &amount, NULL);
    if (!rc) {
	TclWinConvertError(GetLastError());
	*errorCodePtr = Tcl_GetErrno();
	return -1;
    }
    	
    if (!amount) {
    	// We read no data.
    	// Check to see if we are in non-blocking mode
    	// and return an EAGAIN error if we are...
    	COMMTIMEOUTS cto;
    	GetCommTimeouts(ssPtr->fd, &cto);
    	if (cto.ReadIntervalTimeout == MAXDWORD) {
	    *errorCodePtr = EAGAIN;
	} else {
	    TclWinConvertError(GetLastError());
	    *errorCodePtr = Tcl_GetErrno();
	}
	return -1;
    }
    return amount;
}

/* --------------------------------------------------
 *
 *    SerialOutput --
 *
 *        Sends toWrite bytes out through the serial
 *        port.
 *
 *    Returns
 *
 *        Number of bytes written or -1 and a POSIX
 *        error in errorCodePtr.
 *    
 *    Side Effects
 *
 *        None.
 *
 * ---------------------------------------------------
 */

int
DppSerialOutput(instanceData, bufPtr, toWrite, errorCodePtr)
    ClientData instanceData;
    char *bufPtr;
    int toWrite;
    int *errorCodePtr;
{
    BOOL rc;
    DWORD amount;
    SerialState *ssPtr = (SerialState *) instanceData;

    rc = WriteFile(ssPtr->fd, bufPtr, toWrite, &amount, NULL);
    if (!rc) {
	TclWinConvertError(GetLastError());
	*errorCodePtr = Tcl_GetErrno();
	return -1;
    }

    if (!amount) {
    	// We wrote no data.
    	// Check to see if we are in non-blocking mode
    	// and return an EAGAIN error if we are...
    	COMMTIMEOUTS cto;
    	GetCommTimeouts(ssPtr->fd, &cto);
    	if (cto.ReadIntervalTimeout == MAXDWORD) {
	    *errorCodePtr = EAGAIN;
	} else {
	    TclWinConvertError(GetLastError());
	    *errorCodePtr = Tcl_GetErrno();
	}
	return -1;
    }
    FlushFileBuffers(ssPtr->fd);
    return amount;
}

/* --------------------------------------------------
 *
 *    Dpp_SetSerialState --
 *
 *        Platform-specific serial option changer.
 *
 *    Returns
 *
 *        TCL_OK or TCL_ERROR
 *    
 *    Side Effects
 *
 *        None.
 *
 * ---------------------------------------------------
 */

int
DppSerialSetOption(instanceData, optionName, val)
    ClientData instanceData;
    int optionName;
    int val;
{
    SerialState *ssPtr = (SerialState *) instanceData;
    DCB settings;
    int rate;
    COMMTIMEOUTS cto;

    if (!GetCommState(ssPtr->fd, &settings)) {
	return TCL_ERROR;
    }
    switch (optionName) {
        case DP_PARITY:
            if (val == PARITY_NONE) {
                settings.fParity = FALSE;
                settings.Parity = NOPARITY;
            } else {
                settings.fParity = TRUE;
                if (val == PARITY_EVEN) {
                    settings.Parity = EVENPARITY;
                } else {
                    settings.Parity = ODDPARITY;
                }
            }
            break;
        case DP_CHARSIZE:
            if (val == 7) {
                settings.ByteSize = 7;
            } else {
                settings.ByteSize = 8;
            }
            break;
        case DP_STOPBITS:
            if (val == 1) {
                settings.StopBits = ONESTOPBIT;
            } else {
                settings.StopBits = TWOSTOPBITS;
            }
            break;
        case DP_BAUDRATE:
	    rate = DppBaudRateNumToCons(val);
	    if (rate == -1) {
	    	char baud[7];
	    	sprintf(baud, "%ld", val);
	    	Tcl_SetErrno(EINVAL);
	    	return TCL_ERROR;
	    }
	    settings.BaudRate = rate;
            break;
        case DP_BLOCK:
	    memset(&cto, 0, sizeof(COMMTIMEOUTS));
	    if (val == 1) {
		/* 
		 * We want to block.
		 * A read has numbytes * 1 s to complete
		 * before the system will return an error.
		 * 
		 * A byte MUST arrive at least every 3 seconds
		 * during the read or we will timeout with
		 * an error.
		 */
		cto.ReadTotalTimeoutMultiplier = 1000;
		cto.ReadIntervalTimeout = 3000;
	    } else {
		/*
		 * This line will set the serial port to:
		 *	READ - Return as much as possible without blocking
		 *	WRITE - Write buf then return
		 */
		cto.ReadIntervalTimeout = MAXDWORD;
	    }
	    SetCommTimeouts(ssPtr->fd, &cto);
	    return TCL_OK;

        default:
            return TCL_ERROR;
    }
    if (!SetCommState(ssPtr->fd, &settings)) {
    	return TCL_ERROR;
    }
    return TCL_OK;
}

/* ----------------------------------------------------
 *
 *    DppBaudRateNumToCons --
 *
 *	Changes a numeric rate into a baudrate constant
 *	understood by the platform.
 *
 *    Returns
 *
 *	The baudrate constant or -1 on error.
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

int
DppBaudRateNumToCons(rate)
    int rate;
{
    switch (rate) {
        case 1200:
            return CBR_1200;
        case 2400:
            return CBR_2400;
        case 4800:
            return CBR_4800;
        case 9600:
            return CBR_9600;
        case 19200:
            return CBR_19200;
        case 38400:
            return CBR_38400;
        case 57600:
            return CBR_57600;
        case 115200:
            return CBR_115200;
        default:
            return -1;
    }
}

/* ----------------------------------------------------
 *
 *    DppSerialGetOption --
 *
 *	Returns the value of the given option in dsPtr.
 *
 *    Returns
 *
 *	TCL_OK or TCL_ERROR
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

int
DppSerialGetOption(instanceData, opt, optionName, dsPtr)
    ClientData instanceData;
    int opt;
    char *optionName;
    Tcl_DString *dsPtr;
{
    DCB commState;
    SerialState *ssPtr = (SerialState *) instanceData;
    char *rate;
    COMMTIMEOUTS cto;

    GetCommState(ssPtr->fd, &commState);
    switch (opt) {
    	case DP_PARITY:
	    if (commState.Parity == EVENPARITY) {
		Tcl_DStringAppend(dsPtr, "even", -1);
	    } else if (commState.Parity == ODDPARITY) {
		Tcl_DStringAppend(dsPtr, "odd", -1);
	    } else if (commState.Parity == NOPARITY) {
	    	Tcl_DStringAppend(dsPtr, "none", -1);
	    } else {
	    	return TCL_ERROR;
	    }
	    return TCL_OK;
    	case DP_BAUDRATE:
	    rate = DppBaudRateConsToStr(commState.BaudRate);
	    Tcl_DStringAppend(dsPtr, rate, -1);
	    return TCL_OK;
    	case DP_CHARSIZE:
	    if (commState.ByteSize == 7) {
	    	Tcl_DStringAppend(dsPtr, "7", -1);
	    } else {
	    	Tcl_DStringAppend(dsPtr, "8", -1);
	    }
	    return TCL_OK;
    	case DP_STOPBITS:
	    if (commState.StopBits == ONESTOPBIT) {
	    	Tcl_DStringAppend(dsPtr, "1", -1);
	    } else {
	    	Tcl_DStringAppend(dsPtr, "2", -1);
	    }
	    return TCL_OK;
    	case DP_BLOCK:
	    GetCommTimeouts(ssPtr->fd, &cto);
	    if (cto.ReadIntervalTimeout < MAXDWORD) {
	    	Tcl_DStringAppend(dsPtr, "true", -1);
	    } else {
	    	Tcl_DStringAppend(dsPtr, "false", -1);
	    }
	    return TCL_OK;
	case DP_DEVICENAME:
	    Tcl_DStringAppend(dsPtr, ssPtr->deviceName, -1);
	    return TCL_OK;
	default:
	    {
	    	char errStr[128];
	    	sprintf(errStr, "bad option \"%s\": must be -blocking,"
	    		"-buffering, -buffersize, -eofchar, -translation,"
	    		" or a channel type specific option", optionName);
	    	Tcl_DStringAppend(dsPtr, errStr, -1);
	    }
	    Tcl_SetErrno(EINVAL);
	    return TCL_ERROR;
    }
}

/* ----------------------------------------------------
 *
 *    DppBaudRateConsToStr --
 *
 *	Translates a Win32 baudrate constant to a
 *	human-readable string.
 *
 *    Returns
 *
 *	Pointer to the string representing the baudrate.
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

char *
DppBaudRateConsToStr(rate)
    int rate;
{
    switch (rate) {
    	case CBR_1200:
	    return "1200";
    	case CBR_2400:
	    return "2400";
    	case CBR_4800:
	    return "4800";
    	case CBR_9600:
	    return "9600";
    	case CBR_19200:
	    return "19200";
    	case CBR_38400:
	    return "38400";
    	case CBR_57600:
	    return "57600";
    	case CBR_115200:
	    return "115200";
	default:
	    return NULL;
    }
}

/* ----------------------------------------------------
 *
 *    DppCheckDevice --
 *
 *	Verifies that "checkStr" is a valid serial
 *	device on the OS or the DP naming method of
 *	"serialx".  In Win32, we assume a device is
 *	any of the form "COMx" where x is a single
 *	digit number.  If the name is given as
 *	"serialx", we translate it into the OS term.
 *
 *    Returns
 *
 *	TCL_OK and updates devStr if checkStr is valid or
 *	TCL_ERROR if checkStr is invalid.
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

char *
DppCheckDevice(devStr)
    char *devStr;
{
    int num;

    if (strlen(devStr) == 7) {
#ifdef __BORLANDC__
		if (strncmpi(devStr,"serial",6) == 0) {
#else
    	if (_strnicmp(devStr, "serial", 6) == 0) {
#endif
	    num = devStr[6] - '1';
	    if ((num < 0) || (num > 3)) {
		return NULL;
 	    }
	    return portNames[num];
	}
    }
    return NULL;
}

/* ----------------------------------------------------
 *
 *    DppSerialWatchFile --
 *
 *	Sets up event handling on the serial channel.
 *	We jsut set the event mask on the given handle.
 *
 *    Returns
 *
 *	Immediately.
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

void
DppSerialWatchFile(instanceData, mask)
    ClientData instanceData;
    int mask;
{
    SerialState *ssPtr = (SerialState *) instanceData;
    DWORD evts = 0;

    if (mask & TCL_READABLE) {
    	evts |= EV_RXCHAR;
    }
    if (mask & TCL_WRITABLE) {
    	evts |= EV_TXEMPTY;
    }
    if (mask & TCL_EXCEPTION) {
    	evts |= EV_ERR;
    }
    SetCommMask(ssPtr->fd, evts);
}


/* ----------------------------------------------------
 *
 *    DppSerialFileReady --
 *
 *	Waits for an event to happen on the serial port.
 *	CAUTION!!!!
 *	This is different than the Tcl specs because
 *	there is no way to see what events have
 *	already happened: we MUST block until a new
 *	event takes place.
 *
 *    Returns
 *
 *	A mask of events.
 *
 *    Side Effects
 *
 *	None.
 *
 * -----------------------------------------------------
 */

int
DppSerialFileReady(instanceData, mask)
    ClientData instanceData;
    int mask;
{
    SerialState *ssPtr = (SerialState *) instanceData;
    OVERLAPPED ovStr;
    DWORD evts;
    DWORD events = 0;

    GetCommMask(ssPtr->fd, &evts);
    WaitCommEvent(ssPtr->fd, &evts, &ovStr);
    if (evts & EV_RXCHAR) {
    	events |= TCL_READABLE;
    }
    if (evts & EV_TXEMPTY) {
    	events |= TCL_WRITABLE;
    }
    if (evts & EV_ERR) {
    	events |= TCL_EXCEPTION;
    }
    return events;
}

