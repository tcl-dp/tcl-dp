/* Tcl_Channel implementation for serial ports */



#include "generic/dpInt.h"

#include "generic/dpPort.h"

#include <termios.h>



/*

 * These names cannot be longer than MAX_LENGTH - 1

 *

 * Note that a given machine may only HAVE 2 serial ports

 * so serial3 and serial4 may not work even if they are 

 * listed here.

 *

 * A note on naming: because there is no standard UNIX

 * naming scheme, we must do it by OS.  You can add more

 * ports by upping NUM_PORTS and adding the names.

 *

 */



#define NUM_PORTS	4



static char *portNames[NUM_PORTS] = {

#if defined(__LINUX__)

	"/dev/ttyS0",

	"/dev/ttyS1",

	"/dev/ttyS2",

	"/dev/ttyS3"

#elif defined(__HPUX__)

	"/dev/plt_rs232_a",

	"/dev/plt_rs232_b",

	NULL,

	NULL

#elif defined(__SUNOS__) || defined(__SOLARIS__)

	"/dev/ttya",

	"/dev/ttyb",

	"/dev/ttyc",

	"/dev/ttyd"

#elif defined(__sgi)

        "/dev/ttyd1",

        "/dev/ttyd2",

        "/dev/ttyd3",

        "/dev/ttyd4"

#elif defined(__FREEBSD__)

	/*

	 * Why is it so difficult to find out

	 * the names of the damn serial ports in

	 * FreeBSD?

	 */



	  NULL,

	  NULL,

	  NULL,

	  NULL

#elif defined(__osf__)

   /*

	 For OSF 

	 */

	  "/dev/tty00",

	  "/dev/tty01",

	  NULL,

	  NULL

#else

	/* 

	 * We could assume the worst and just not let

	 * DP be compiled.  But most people won't

	 * even use the serial interface (<sniff>) so we'll

	 * just let DP catch it at runtime.

	 */



	  NULL,

	  NULL,

	  NULL,

	  NULL

#endif

};



int		DppOpenSerialChannel _ANSI_ARGS_((Tcl_Interp *interp,

			    	ClientData instanceData, char *devStr, 

			    	int flags));

int		DppSerialBlock _ANSI_ARGS_((ClientData instanceData,

			    	int mode));

int		DppSerialClose _ANSI_ARGS_((ClientData instanceData));

int		DppSerialInput _ANSI_ARGS_((ClientData instanceData,

			    	char *bufPtr, int bufSize, 

			    	int *errorCodePtr));

int		DppSerialOutput _ANSI_ARGS_((ClientData instanceData,

			    	char *bufPtr, int toWrite, 

			    	int *errorCodePtr));

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

 *        Creates a DP channel using the serial port specified 

 *        in dev (i.e. "serial1")

 *  

 *  Returns 

 *      

 *      Tcl_Channel used for I/O.

 *

 *  Side Effects

 *

 *        None.

 *

 * ------------------------------------------------  

 */



int

DppOpenSerialChannel(interp, instanceData, devStr, flags)

    Tcl_Interp *interp;

    ClientData instanceData;

    char *devStr;             	/* /dev to use  */

    int flags;			/* T/F to block */

    				/* Only block is implemented

    				   right now */

{

    SerialState *ssPtr = (SerialState *) instanceData;

    char *openStr;

    int fd, mode = O_RDWR;

    int blockFlag = 0;



    blockFlag |= 0x1;

    if (flags & 0x2) {

	mode = O_RDONLY;

    }

    if ((openStr = DppCheckDevice(devStr)) == NULL) {

	Tcl_AppendResult(interp, "Unknown device \"", devStr, "\"", NULL);

	return TCL_ERROR;

    }	



    fd = open(openStr, mode);

    if (fd == -1) {

        Tcl_AppendResult(interp, "Error opening ", openStr, 

		": ", Tcl_PosixError(interp), NULL);

        return TCL_ERROR;

    }



    ssPtr->fd = fd;

    strcpy(ssPtr->deviceName, devStr);



    /*

     * Setup the port to a default of 19200, 8N1 

     */



    if (DppSerialSetOption(ssPtr, DP_BAUDRATE, 19200) == TCL_ERROR) {

    	goto error;

    }

    if (DppSerialSetOption(ssPtr, DP_CHARSIZE, 8) == TCL_ERROR) {

    	goto error;

    }

    if (DppSerialSetOption(ssPtr, DP_PARITY, PARITY_NONE) == TCL_ERROR) {

    	goto error;

    }

    if (DppSerialSetOption(ssPtr, DP_STOPBITS, 1) == TCL_ERROR) {

    	goto error;

    }

    if (DppSerialSetOption(ssPtr, DP_BLOCK, blockFlag) == TCL_ERROR) {

    	goto error;

    }

    return TCL_OK; 

    

error: 

    Tcl_AppendResult(interp, "Error configuring serial device", NULL);

    return TCL_ERROR;



} 



/* --------------------------------------------------

 *

 *    SerialBlock --

 *

 *        Sets blocking mode of serial port based on

 *        mode.

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

    

    return close(ssPtr->fd);

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

 *        Number of bytes read or -1 with POSIX error code

 *        in errorCodePtr.

 *    

 *    Side Effects

 *

 *        buf is modified.

 *

 * ---------------------------------------------------

 */



int

DppSerialInput(instanceData, bufPtr, bufSize, errorCodePtr)

    ClientData instanceData;

    char *bufPtr;

    int bufSize;

    int *errorCodePtr; 

{

    SerialState *ssPtr = (SerialState *) instanceData;

    int amount;



    amount = read(ssPtr->fd, bufPtr, bufSize);

    if (amount > 0) {

    	/* We are fine.  Return amount read */

        return amount;

    } else if (amount == 0) {

	/* There is no data to be read */

        int flags;

        fcntl(ssPtr->fd, F_GETFL, &flags); 

        if (NONBLOCKING(flags)) {

            *errorCodePtr = EAGAIN;

            return -1;

        } else { 

            return amount;

        }

    } 



    /* Bummer!  Set the error code and return */ 

    *errorCodePtr = errno;

    return -1;

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

    int amount;

    SerialState *ssPtr = (SerialState *) instanceData;



    amount = write(ssPtr->fd, bufPtr, toWrite);

    if (amount > 0) {

        return amount;

    } else if (amount == 0) {

        int flags;

        fcntl(ssPtr->fd, F_GETFL, &flags);

        if (NONBLOCKING(flags)) {

            *errorCodePtr = EAGAIN;

            return -1;

        } else {

            return amount;

        }

    } 



    *errorCodePtr = errno;

    return -1;

}



/* --------------------------------------------------

 *

 *    DppSetSerialState --

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

DppSerialSetOption(instanceData, optionName, optionVal)

    ClientData instanceData;

    int optionName;

    int optionVal;

{

    SerialState *ssPtr = (SerialState *) instanceData;

    struct termios term;

    int rate;

    int flags = 0;



    if (tcgetattr(ssPtr->fd, &term) == -1) {

    	return TCL_ERROR;

    }

    switch (optionName) {

        case DP_PARITY:

            if (optionVal == PARITY_NONE) {

                term.c_cflag &= ~PARENB;

            } else {

                term.c_cflag |= PARENB;

                if (optionVal == PARITY_EVEN) {

                    term.c_cflag &= ~PARODD;

                } else {

                    term.c_cflag |= PARODD;

                }

            }

            break;

        case DP_CHARSIZE:

	    term.c_cflag &= ~(CSIZE);

            if (optionVal == 7) {

                term.c_cflag |= CS7;

            } else {

                term.c_cflag |= CS8;

            }

            break;

        case DP_STOPBITS:

            if (optionVal == 1) {

                term.c_cflag &= (~CSTOPB);

            } else {

                term.c_cflag |= CSTOPB;

            }

            break;

        case DP_BAUDRATE:

	    rate = DppBaudRateNumToCons(optionVal);

	    if (rate == -1) {

	    	char baud[7];

	    	sprintf(baud, "%ld", optionVal);

	    	Tcl_SetErrno(EINVAL);

	    	return TCL_ERROR;

	    }

	    cfsetispeed(&term, rate);

	    cfsetospeed(&term, rate);

            break;

        case DP_BLOCK:

	    fcntl(ssPtr->fd, F_GETFL, &flags);

	    if (optionVal == 1) {

		if (NONBLOCKING(flags)) {

		    flags &= ~NBIO_FLAG;

		    fcntl(ssPtr->fd, F_SETFL, flags);

		}

	    } else {

		if (!NONBLOCKING(flags)) {

		    flags |= NBIO_FLAG; 

		    fcntl(ssPtr->fd, F_SETFL, flags);

		}

	    }

	    break;



        default:

	    Tcl_SetErrno(EINVAL);

            return TCL_ERROR;

    }



    if (tcsetattr(ssPtr->fd, TCSADRAIN, &term) == -1) {

    	return TCL_ERROR;

    }

    return TCL_OK;

}





/* ----------------------------------------------------

 *

 *    Dpp_BaudRateNumToCons --

 *

 *	Translates an integer baudrate into a constant

 *	understood by the system.

 *

 *    Returns

 *

 *	Constant representing the baudrate or 0xFFFFFFFF on error.

 *

 *    Side Effects

 *

 *	None.

 *

 * -----------------------------------------------------

 */



static int 

DppBaudRateNumToCons(rate)

    int rate;

{

    switch (rate) {

        case 1200:

            return B1200;

        case 2400:

            return B2400;

        case 4800:

            return B4800;

        case 9600:

            return B9600;

        case 19200:

            return B19200;

        case 38400:

            return B38400;

#ifdef B57600

        case 57600:

            return B57600;

#endif

#ifdef B115200

        case 115200:

            return B115200;

#endif

        default:

            return -1;

    }

}



/* ----------------------------------------------------

 *

 *    DppSerialGetOption --

 *

 *	Returns the value of the given option or the

 *	value of ALL options if optionName is set to

 *	DP_ALL_OPTIONS.

 *

 *    Returns

 *

 *	TCL_OK or TCL_ERROR.

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

    SerialState *ssPtr = (SerialState *) instanceData;

    struct termios term;

    char *rate;

    unsigned long ospeed;





    tcgetattr(ssPtr->fd, &term);

    switch (opt) {

    	case DP_PARITY:

	    if (term.c_cflag & PARENB) {

	    	if (term.c_cflag & PARODD) {

		    Tcl_DStringAppend(dsPtr, "odd", -1);

	    	} else {

		    Tcl_DStringAppend(dsPtr, "even", -1);

		}

	    } else {

	    	Tcl_DStringAppend(dsPtr, "none", -1);

	    }

	    return TCL_OK;



    	case DP_STOPBITS:

	    if (term.c_cflag & CSTOPB) {

	    	Tcl_DStringAppend(dsPtr, "2", -1);

	    } else {

	    	Tcl_DStringAppend(dsPtr, "1", -1);

	    }

	    return TCL_OK; 



    	case DP_CHARSIZE:

	    if ((term.c_cflag & CS8) == CS8) {

	    	Tcl_DStringAppend(dsPtr, "8", -1);

	    } else {

	    	Tcl_DStringAppend(dsPtr, "7", -1);

	    }

	    return TCL_OK;



    	case DP_BAUDRATE:

#if defined(__SOLARIS__) && defined(CBAUDEXT)

            if (term.c_cflag & CBAUDEXT) {

                ospeed = (term.c_cflag & CBAUD) + CBAUD + 1;

            } else {

                ospeed = term.c_cflag & CBAUD;

            }

#elif defined(__FREEBSD__)||defined(__osf__)

            ospeed = term.c_ospeed; 

#else

            ospeed = term.c_cflag & CBAUD;

#endif



	    rate = DppBaudRateConsToStr(ospeed);

	    if (rate == NULL) {

	    	Tcl_SetErrno(EINVAL);

	    	return TCL_ERROR;

	    }

	    Tcl_DStringAppend(dsPtr, rate, -1);

	    return TCL_OK;



	case DP_DEVICENAME:

	    Tcl_DStringAppend(dsPtr, ssPtr->deviceName, -1);

	    return TCL_OK;



    	default:

	{

	    char bufStr[128];	

	    sprintf(bufStr, "bad option \"%s\": must be -blocking, -buffering, -buffersize, -eofchar, -translation, or a channel type specific option", optionName);

	    Tcl_DStringAppend(dsPtr, bufStr, -1);

	    Tcl_SetErrno (EINVAL);

	    return TCL_ERROR;

	}

    }

}





/* ----------------------------------------------------

 *

 *    Dpp_BaudRateConsToStr --

 *

 *	Translates the native UNIX baudrate constants

 *	to a human-readable string.

 *

 *    Returns

 *

 *	Pointer to the string representing the baudrate

 *

 *    Side Effects

 *

 *	None.

 *

 * -----------------------------------------------------

 */



static char *

DppBaudRateConsToStr(rate)

    int rate;

{

    switch (rate) {

    	case B1200:

	    return "1200";

	case B2400:

	    return "2400";

	case B4800:

	    return "4800";

	case B9600:

	    return "9600";

	case B19200:

	    return "19200";

	case B38400:

	    return "38400";

#ifdef B57600

	case B57600:

	    return "57600";

#endif

#ifdef B115200

	case B115200:

	    return "115200";

#endif

	default:

	    return NULL;

    }

}





/* ----------------------------------------------------

 *

 *    DppCheckDevice --

 *

 *	This function checks to make sure a string

 *	matches the name of a valid serial port on

 *	this OS.  In UNIX, there is no standard for

 *	RS-232 ports, so we must #define them based

 *	on the OS.  We also allow the DP naming

 *	method of "serialx" which we translate into

 *	an OS-specific name.

 *

 *    Returns

 *

 *	TCL_OK if the string is ok with devStr updated

 *	or TCL_ERROR if the string is invalid.

 *

 *    Side Effects

 *

 *	None.

 *

 * -----------------------------------------------------

 */



static char *

DppCheckDevice(devStr)

    char *devStr;

{

    int num;



    if (strlen(devStr) == 7) {

    	if (strncmp(devStr, "serial", 6) == 0) {

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

 *	Sets up event handling on a serial port.

 *	We can use Tcl_WatchFile since serial

 *	ports are file descriptors in UNIX.

 *

 *    Returns

 *

 *	Nothing.

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



#ifdef _TCL76

    Tcl_WatchFile((ClientData)ssPtr->fd, mask);

#else

    if (mask) {

	Tcl_CreateFileHandler(ssPtr->fd, mask,

		(Tcl_FileProc *) Tcl_NotifyChannel,

		(ClientData) ssPtr->channel);

    } else {

	Tcl_DeleteFileHandler(ssPtr->fd);

    }

#endif

}





/* ----------------------------------------------------

 *

 *    DppSerialFileReady --

 *

 *	Returns events of interest on the serial port

 *	based on mask.

 *

 *    Returns

 *

 *	See above.

 *

 *    Side Effects

 *

 *	None.

 *

 * -----------------------------------------------------

 */



#ifdef _TCL76

int

DppSerialFileReady(instanceData, mask)

    ClientData instanceData;

    int mask;

{

    SerialState *ssPtr = (SerialState *) instanceData;



    return Tcl_FileReady(ssPtr->theFile, mask);

}	

#endif





