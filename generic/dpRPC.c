/*
 *  generic/dpRPC.c
 *
 * Todo:
 *	Make activation records dynamic structure.
 *	Change DpRPC/DpRDO to use diff parameters.  Parsing should be done
 *	  in dpCmds.c
 *
 *	This file contains code for sending and receiving Tcl-DP
 *	RPCs and RDOs.  All of this code is OS and, to some extent,
 *	transport layer independent.
 *
 *	An RPC/RDO is a message formatted as follows:
 *
 *	---------------------------------------------------------------------
 *      |  len:6  | space:1 | tok:1 | space:1 | id:6 | space:1 | msg:len-16 |
 *	---------------------------------------------------------------------
 *
 *	Length is a 6 byte character string that gives the length
 *	of the rest of the message (including the header)
 *	as a decimal integer.  Whitespace is prepended to pad it to
 *	6 bytes if the message length is less than 100,000 bytes.
 *
 *	Tok is a character that tells what this packet
 *	represents in RPC/RDO terms.  It can be one of four
 *	values, one each for RPC, RDO, error, or return value
 *	messages.  See the #defines for TOK_* below for details.
 *
 *	A space follows the token, followed by a 6 digit numeric
 *	identifier, formatted as a decimal string with leading
 *  	whitespace padding.  The id field uniquely identifies the
 *  	RPC call, and is returned to the sender with the RPC's
 *  	return value or error.  The id field is ignored for RDO calls.
 *
 *	A space is tacked on after that and then the message
 *	follows.  For RPC and RDO messages, the message is a
 *	Tcl command string to evaluate.  For return values,
 *	message contains a Tcl return value.  For errors, message
 *	contains a Tcl list with the errorCode and errorInfo variables
 *	set.
 *
 *	When a message comes in, ReadRPCChannel is called.  Its
 *	job is to read the available messages from the channel and
 *	process each message.  It first reads all available input
 *	from the channel.  There are 3 cases:
 *	o if the message is short, the input is buffered, and
 *	  ReadRPCChannel returns.
 *	o if the input is badly formatted (invalid length field, or
 *	  bad token), or some other error occurs, the channel is closed.
 *	o otherwise, each complete message is processed using
 *	  ProcessRPCMessage.  Any remaining incomplete data is
 *	  buffered.
 *
 *	ProcessRPCMessage processes an incoming RPC, RDO, error, or
 *	return value message.  There are 4 cases:
 *	o if the message is an RPC message, the global variable dp_rpcFile
 *	  is set to the channel identifier and the string is evaluated.
 *	  Security checks, if any, are performed at this point.
 *	  If an error code is generated, a return message is sent on
 *	  the channel consisting of the errorCode and errorInfo variable,
 *	  formatted as a Tcl list.  If no error is generated, the return
 *	  value is sent on the channel.
 *	o if the message is an RDO message, processing proceeds as with
 *	  RPC, but no return value is sent in any case (if a return value
 *	  is requested in an RDO, the evaluated string will contain a
 *	  call to dp_RDO).
 *	o if the message is a return value or error message, the message
 *	  is stored in the activeRPCs global variable, which stores the
 *	  values that dp_RPC should return in interp->result and the
 *	  errorCode (typically, TCL_OK or TCL_ERROR).
 *
 *	When an RPC is sent, the message is sent (along with a unique id),
 *	and the process typically enters an event loop, waiting for the
 *	return message or error.  The id is used as an index in the
 *	activeRPCs global variable.  When the return value (or error)
 *	comes back, the waiting field for the corresponding RPC is cleared.
 *	If a timeout occurs, the timedOut field will be set, in which case
 *	the RPC will return (with an appropriate message).
 *
 *	Timeouts cause a garbage collection problem in the activeRPCs
 *	structure.  Since each RPC corresponds to a unique outstanding
 *	RPC message, only 1000 RPCs can be active at any time.  If
 *	RPCs time out, then that slot in the activeRPCs structure can't
 *	be reused until the RPC returns.  If the RPC never returns, we
 *	still need to eventually free them.  We know we can free a slot
 *	if a return value message comes back (with the appropriate id) or
 *	when the associated channel closes.
 *
 *	To solve this problem, we store the time the RPC was sent in the
 *	slot.  If we ever run out of slots, the oldest timed out slots
 *	will be reclaimed.
 */

#include <string.h>
#include "generic/dpInt.h"

/*
 * If RPCDEBUG is defined, you will get A LOT of information on stdout
 * corresponding to what is happening in the RPC subsystem.
 */
#ifdef RPCDEBUG
#	define DBG(x) x
#else
#	define DBG(x)
#endif

/*
 *  The following tokens appear as the first characters
 *  in the RPC messages which are transmitted between processes.
 */
#define TOK_RPC     		'e'
#define TOK_RDO     		'd'
#define TOK_RET     		'r'
#define TOK_ERR     		'x'
#define NO_TOKEN    		(Tcl_TimerToken)(-1)

#define RPC_BUFFER_SIZE		8192

/*
 *   The maximum number of active RPC's.  Used in MakeActivationRecord()
 */
#define RPC_MAX_ACTIVE_RPCS	1000000

/*
 * One of the following structures is maintained for each Tcl channel
 * that is receiving/sending RPCs.
 */
typedef struct RPCChannel {
    char *name;		/* Name of channel in Tcl interpreter */
    Tcl_Interp *interp;	/* Associated interpreter */
    Tcl_Channel chan;	/* Associated channel */
    char *buffer;	/* Any buffered input */
    int error; 		/* Has an error occured on this channel? */
    int bufLen; 	/* Length of any buffered input */
    int maxLen; 	/* Number of bytes allocated to buffer */
    char *checkCmd;	/* Tcl command to run to check RPCs */
    struct RPCChannel *next;
    int flags;		/* Channel status */
#define CHAN_BUSY	1
#define CHAN_FREE	2
} RPCChannel;
static RPCChannel *registeredChannels = NULL;

/*
 * One of the following structures exists for each active RPC.  The
 * structures are held in a hash table.
 */
typedef struct ActiveRPC {
    int flags;			/* Flags (see below) */
    int id;			/* ID for the RPC */
    unsigned long time;		/* Time RPC was sent (for garbage collection) */
    Tcl_TimerToken token;	/* Token for timeout */
    RPCChannel *chanPtr;	/* Associated channel */
    int returnValue;		/* Value to return from dp_RPC */
    char *result;		/* Value to set in interp->result */
} ActiveRPC;
static Tcl_HashTable activeRPCs;
static int activeId;		/* Next free id */

/*
 * Values for ActiveRPC->flags
 */
#define RPC_WAITING	(1<<0)	/* Waiting for a reply */
#define RPC_TIMEDOUT	(1<<1)	/* Timed out, no reply yet.  When reply
				 * comes, clear it */
#define RPC_CANCELLED	(1<<2)	/* Cancelled by user -- same semantics
				 * for clearing as timeout */

/*
 *  Forward Declarations
 */

static void DpReadRPCChannel 		_ANSI_ARGS_((RPCChannel *rpcChanPtr));
static void DpReadRPCChannelCallback 	_ANSI_ARGS_((ClientData clientData,
						int mask));
static void DpProcessRPCMessage 		_ANSI_ARGS_((Tcl_Interp *interp,
						RPCChannel *chan,
						int id, int token,
						char *message));
static int DpCheckRPC			_ANSI_ARGS_((Tcl_Interp *interp,
						RPCChannel *rcPtr,
						char *rpcStr));
static void DpTimeoutHandler 		_ANSI_ARGS_((ClientData clientData));
static int DpParseEventList 		_ANSI_ARGS_((Tcl_Interp *interp,
						CONST char *eventList, int *maskPtr));
static ActiveRPC *MakeActivationRecord  _ANSI_ARGS_((RPCChannel *rpcChanPtr,
						int setTimeout, int timeout));
static int DpRegisterRPCChannel 	_ANSI_ARGS_((Tcl_Interp *interp,
						CONST char *chanName,
						CONST char *checkCmd));
static int DpDeleteRPCChannel 		_ANSI_ARGS_((Tcl_Interp *interp,
						RPCChannel *rpcChanPtr));
int Dp_RPCCmd 				_ANSI_ARGS_((ClientData clientData,
						Tcl_Interp *interp, int argc,
						CONST84 char **argv));
int Dp_RDOCmd 				_ANSI_ARGS_((ClientData clientData,
						Tcl_Interp *interp, int argc,
						CONST84 char **argv));
int Dp_AdminCmd 			_ANSI_ARGS_((ClientData clientData,
						Tcl_Interp *interp, int argc,
						CONST84 char **argv));
int Dp_CancelRPCCmd			_ANSI_ARGS_((ClientData unused,
						Tcl_Interp *interp, int argc,
						CONST84 char **argv));
int Dp_RPCInit 				_ANSI_ARGS_((Tcl_Interp *interp));
static int DpSendRPCMessage 		_ANSI_ARGS_((Tcl_Channel chan,
						int token, int id,
						char *mesgStr));

/*
 * The following strings are used to provide callback and/or error
 * catching for RDOs. c = callback, e = onerror, ce = both
 */
/*
 * sprintf template when both callback and onerror are specified.
 * Params are cmd, onerror, callback
 */
static char *ceCmdTemplate =
"if [catch {%s} dp_rv] {\
    dp_RDO $dp_rpcFile set errorInfo \"$errorInfo\n    while remotely executing\n%s\"; \
    dp_RDO $dp_rpcFile eval \"%s \\{$dp_rv\\}\"\
} else {\
    dp_RDO $dp_rpcFile eval \"%s \\{$dp_rv\\}\"\
}";

/*
 * sprintf template when just onerror is specified.
 *  Params are cmd, onerror
 */
static char *eCmdTemplate =
"if [catch {%s} dp_rv] {\
    dp_RDO $dp_rpcFile set errorInfo \"$errorInfo\n    while remotely executing\n%s\"; \
    dp_RDO $dp_rpcFile eval \"%s \\{$dp_rv\\}\"\
}";

/*
 * sprintf template when just callback is specified.
 *  Params are cmd, callback
 */
static char *cCmdTemplate =
"set dp_rv [%s]; dp_RDO $dp_rpcFile eval \"%s \\{$dp_rv\\}\"";

/*
 *--------------------------------------------------------------
 *
 * ReadRPCChannel --
 *
 *	This function is called by the event loop (through
 *	ReadRPCChannelCallback) whenever the file is readable
 *	(i.e., there's an inbound message).
 *	It reads all available input on the channel, and calls
 *	ProcessRPCMessage on each valid message.  If the input
 *	is badly formatted, the channel is closed and an error
 *	is returned.  If the message is incomplete, it is buffered.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	May close the channel (in which case a background error is
 *	triggered).
 *
 *--------------------------------------------------------------
 */
static void
DpReadRPCChannel (rpcChanPtr)
    RPCChannel *rpcChanPtr;
{
    char *buffer;
    int bufLen, maxLen;
    char str[100];
    char scratch[100000];
    Tcl_DString dstr;
    int blocking;
    int numRead;
    char *curMsg;
    int id, msgLen;
    char token, saveChar;

    /*
     * Make sure the socket's non-blocking (it was set to be non-blocking
     * when it was registered, and shouldn't have changed.  If it did, then
     * it's an error in the user program).
     */

	memset(scratch, 0, 1000);
    Tcl_DStringInit(&dstr);
    Tcl_GetChannelOption (
#if (TCL_MAJOR_VERSION > 7)
			rpcChanPtr->interp,
#endif
			rpcChanPtr->chan, "-blocking", &dstr);
    Tcl_GetBoolean (rpcChanPtr->interp, Tcl_DStringValue(&dstr), &blocking);
    if (blocking) {
	sprintf (str, "Channel %s must be in non-blocking mode", rpcChanPtr->name);
	Tcl_SetResult (rpcChanPtr->interp, str, TCL_VOLATILE);
	Tcl_BackgroundError (rpcChanPtr->interp);
	DpDeleteRPCChannel (rpcChanPtr->interp, rpcChanPtr);
	return;
    }
    Tcl_DStringFree(&dstr);

    /*
     * If everything's ok, read in up to 1000000 bytes of data (the maximum
     * message length) and append it onto the buffer (expanding if necessary)
     *
     * Note that we only read in 100000 bytes since more causes stack
     * problems in Win32.  There are other systems that limit a thread's
     * stack to 64k also, so this is not a good solution.  This is a
     * gaping wound in the RPC code that needs to be fixed.
     *
     * You need help if you are sending a megabyte RPC anyhow.  :)
     */

    numRead = Tcl_Read (rpcChanPtr->chan, scratch, sizeof(scratch));
    if (numRead < 1) {
	if (Tcl_Eof(rpcChanPtr->chan)) {

	    /*
	     * End of File on the RPC channel.  This will usually happen
	     * if the TCP connection is reset.
	     *
	     * We'll just close the channel quietly.
	     */

	    Tcl_Channel chan = rpcChanPtr->chan;
	    Tcl_Interp *interp = rpcChanPtr->interp;
	    DpDeleteRPCChannel(rpcChanPtr->interp, rpcChanPtr);
	    DBG(printf("Closing EOF'd RPC channel"));
            DpClose(interp, chan);
	    return;
	} else {
	    return;
	}
    }
    buffer = rpcChanPtr->buffer;
    bufLen = rpcChanPtr->bufLen;
    maxLen = rpcChanPtr->maxLen;
    if (bufLen + numRead > maxLen) {
        char *newBuffer;
        maxLen = bufLen + numRead + 1024;
        newBuffer = ckalloc (maxLen);
        memcpy (newBuffer, buffer, bufLen);
        ckfree (buffer);
        buffer = newBuffer;
    }
    memcpy (buffer+bufLen, scratch, numRead);
    bufLen += numRead;

    /*
     * Process all the messages, one by one.  We break out of this loop
     * when there's not a complete message left in the buffer
     */

    curMsg = buffer;
    while (1) {

	/*
	 * Make sure message length field is there.
	 */

	if (bufLen < 6) {
	    break;
	}

	/*
	 * Ok, we have the length field of the message.  Make sure it's
	 * valid.
	 */
	memcpy(str, curMsg, 6);
	str[6] = 0;
	if (Tcl_GetInt(rpcChanPtr->interp, str, &msgLen) != TCL_OK) {
	    goto badFormat;
	}

	/*
	 * Got a valid length field.  Make sure rest of message is there.
	 */
	if (bufLen < msgLen) {
	    break;
	}
	saveChar = curMsg[msgLen];
	curMsg[msgLen] = '\0';
	DBG(printf("\nIncoming RPC: %s on %s\n", curMsg, Tcl_GetChannelName(rpcChanPtr->chan)));

	/*
	 * Ok, we've got enough data.  Extract the fields and call
	 * ProcessRPCMessage.  If we get a bad message, shut down the channel.
	 * Recall that the message format is:
	 *	---------------------------------------------------------------------
	 *      |  len:6  | space:1 | tok:1 | space:1 | id:6 | space:1 | msg:len-16 |
	 *	---------------------------------------------------------------------
	 */

	token = curMsg[7];
	memcpy(str, &curMsg[9], 6);
	str[6] = 0;
	if (Tcl_GetInt(rpcChanPtr->interp, str, &id) != TCL_OK) {
	    goto badFormat;
	}
	DpProcessRPCMessage(rpcChanPtr->interp, rpcChanPtr, id, token, &curMsg[16]);
	curMsg += msgLen;
	*curMsg = saveChar;
	bufLen -= msgLen;
 	if (rpcChanPtr->flags & CHAN_FREE) {
 	    DpDeleteRPCChannel (rpcChanPtr->interp, rpcChanPtr);
 	    return;
 	}
    }
    /*
     * Done!  Shift down any remaining data to the beginning of the buffer
     * and return!
     */
    memcpy (buffer, curMsg, bufLen);
    rpcChanPtr->bufLen = bufLen;
    rpcChanPtr->buffer = buffer;
    rpcChanPtr->maxLen = maxLen;
    return;

badFormat:
    DBG(printf("Bad RPC packet: %s\n", curMsg));
    sprintf(str, "Received badly formatted packet on RPC channel %s",
	    rpcChanPtr->name);
    Tcl_SetResult(rpcChanPtr->interp, str, TCL_VOLATILE);
    Tcl_BackgroundError(rpcChanPtr->interp);
    return;
}


/*
 *--------------------------------------------------------------
 *
 * DpReadRPCChannelCallback --
 *
 *	This function is called by the event handling system
 *	whenever the channel is readable.  Calls ReadRPCChannel
 *	on the channel.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */
static void
DpReadRPCChannelCallback (clientData, mask)
    ClientData clientData;
    int mask;
{
    DpReadRPCChannel((RPCChannel *)clientData);
}

/*
 *--------------------------------------------------------------
 *
 * DpProcessRPCMessage --
 *
 *	Process an incoming RPC, RDO, error, or return value message.
 *	See the documentation at the top of this file for details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Since a Tcl command is evaluated, almost anything can happen.
 *
 *	ProcessRPCMessage processes an incoming RPC, RDO, error, or
 *	return value message.  There are 4 cases:
 *
 *	o if the message is an RPC message, the global variable dp_rpcFile
 *	  is set to the channel identifier and the string is evaluated.
 *	  Security checks, if any, are performed at this point.
 *	  If an error code is generated, a return message is sent on
 *	  the channel consisting of the errorCode and errorInfo variable,
 *	  formatted as a Tcl list.  If no error is generated, the return
 *	  value is sent on the channel.
 *	o if the message is an RDO message, processing proceeds as with
 *	  RPC, but no return value is sent in any case (if a return value
 *	  is requested in an RDO, the evaluated string will contain a
 *	  call to dp_RDO).
 *	o if the message is a return value or error message, the message
 *	  is stored in the activeRPCs global variable, which stores the
 *	  values that dp_RPC should return in interp->result and the
 *	  errorCode (typically, TCL_OK or TCL_ERROR).
 *
 *--------------------------------------------------------------
 */
static void
DpProcessRPCMessage(interp, chan, id, token, message)
    Tcl_Interp *interp;	/* (in) Tcl interpreter for error reporting 	*/
    RPCChannel *chan;	/* (in) Incoming RPC data channel		*/
    int id;		/* (in) Integer id for RPC			*/
    int token;		/* (in) Message token				*/
    char *message;	/* (in) The command to evaluate (zero terminated) */
{
    ActiveRPC *arPtr = NULL;
    RPCChannel *rcPtr;
    Tcl_HashEntry *entryPtr;
    int retCode, len;
    CONST84 char **argv;
    int argc;

    rcPtr = chan;
    rcPtr->flags |= CHAN_BUSY;

    entryPtr = Tcl_FindHashEntry(&activeRPCs, (char *)id);
    if (entryPtr != NULL) {
	arPtr = (ActiveRPC *) Tcl_GetHashValue(entryPtr);
    }

    if (((token == TOK_RET) || (token == TOK_ERR)) && (arPtr == NULL)) {
    	DBG(printf("Received reply to cancelled RPC\n"));
 	rcPtr->flags &= ~CHAN_BUSY;
    	return;
    }

    switch (token) {
    	case TOK_RPC:
	    Tcl_SetVar(interp, "dp_rpcFile", rcPtr->name, TCL_GLOBAL_ONLY);
	    if (DpCheckRPC(interp, rcPtr, message) == TCL_OK) {
		retCode = Tcl_GlobalEval(interp, message);
		if (retCode != TCL_OK) {
		    CONST char *rv[2];
		    char *errMsg;
		    // Tcl_Merge() 8.5 will change the interp result, so save a copy.
		    rv[0] = strdup(Tcl_GetStringResult(interp));
		    rv[1] = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
		    errMsg = Tcl_Merge(2, rv);
		    // Tcl_Merge() wants CONST, but free() doesn't.  Hold your nose as we cast away CONST.
		    free((char*) rv[0]);
		    if (DpSendRPCMessage(rcPtr->chan, TOK_ERR, id, errMsg)
			    != TCL_OK) {
			ckfree(errMsg);
		    	goto error;
		    }
		    ckfree(errMsg);
		} else {
		    if (DpSendRPCMessage(rcPtr->chan, TOK_RET, id,
			     interp->result) != TCL_OK) {
		    	goto error;
		    }
		}
	    } else {
	    	if (DpSendRPCMessage(rcPtr->chan, TOK_ERR, id,
	    		"RPC authorization denied") != TCL_OK) {
		    goto error;
		}
	    }
	    break;
	case TOK_RDO:
	    /*
	     * RDOs are very simple.
	     */
	    Tcl_SetVar(interp, "dp_rpcFile", rcPtr->name, TCL_GLOBAL_ONLY);
	    if (DpCheckRPC(interp, rcPtr, message) == TCL_OK) {
		Tcl_GlobalEval(interp, message);
	    }
	    break;

    	case TOK_ERR:
	    /*
	     * Split the error message into the result and info
	     * and save both.
	     */
	    Tcl_SplitList(interp, message, &argc, &argv);
	    Tcl_AddErrorInfo(rcPtr->interp, argv[1]);
	    len = strlen(argv[0]) + 1;
	    arPtr->result = ckalloc(len);
	    memcpy(arPtr->result, argv[0], len);
	    ckfree((char *)argv);
	    arPtr->flags = 0;
	    arPtr->returnValue = TCL_ERROR;
	    break;

	case TOK_RET:
	    /*
	     * Provide the return result to the interp.
	     */
	    len = strlen(message) + 1;
	    arPtr->result = ckalloc(len);
	    memcpy(arPtr->result, message, len);
	    arPtr->flags = 0;
	    arPtr->returnValue = TCL_OK;
	    break;

    	default:
	    fprintf(stderr, "Invalid token received in incoming RPC.\n");
	    break;
    }
    rcPtr->flags &= ~CHAN_BUSY;
    return;

error:
    Tcl_AppendResult(interp, "Error sending RPC response on \"",
	    Tcl_GetChannelName(rcPtr->chan), "\"", NULL);
    rcPtr->flags &= ~CHAN_BUSY;
    return;
}

/* ----------------------------------------------------
 *
 *    DpCheckRPC --
 *
 *	Checks an incoming RPC with a user-defined function.
 *
 *    Returns
 *
 *	TCL_OK if allowed or TCL_ERROR if not.
 *
 *    Side Effects
 *
 *	The command checking procedure is evaluated.
 *
 * -----------------------------------------------------
 */

static int
DpCheckRPC(interp, rcPtr, rpcStr)
    Tcl_Interp *interp;
    RPCChannel *rcPtr;
    char *rpcStr;
{
    Tcl_DString cmd;
    int resCode;

    if (rcPtr->checkCmd == NULL) {
    	return TCL_OK;
    }

    Tcl_DStringInit(&cmd);
    Tcl_DStringAppend(&cmd, rcPtr->checkCmd, -1);
    Tcl_DStringAppend(&cmd, " {", -1);
    Tcl_DStringAppend(&cmd, rpcStr, -1);
    Tcl_DStringAppend(&cmd, "}", -1);
    resCode = Tcl_GlobalEval(interp, Tcl_DStringValue(&cmd));
    Tcl_DStringFree(&cmd);

    return resCode;
}

/*
 *--------------------------------------------------------------
 *
 * TimeoutHandler --
 *
 *	This function is called by a timer handler when the associated
 *	RPC times out
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The associated RPC will return as soon as possible.
 *
 *--------------------------------------------------------------
 */
static void
DpTimeoutHandler (clientData)
    ClientData clientData;          /* Id of the RPC that timed out */
{
    int id = (int)clientData;
    ActiveRPC *activePtr;
    Tcl_HashEntry *entryPtr;

    /*
     * Cancel the RPC by setting the flag on its active record (the
     * event loop with the waiting RPC will notice this when it's its
     * turn to return).
     */

    entryPtr = Tcl_FindHashEntry(&activeRPCs, (char *)id);
    if (entryPtr != NULL) {
	activePtr = (ActiveRPC *)Tcl_GetHashValue(entryPtr);
	activePtr->flags = RPC_TIMEDOUT;
    }
}

/*
 *--------------------------------------------------------------
 *
 * ParseEventList --
 *
 *	Helper function to parse "-events" option of dp_RPC
 *
 * Results:
 *	A standard tcl result.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */
static int
DpParseEventList (interp, eventList, maskPtr)
    Tcl_Interp *interp;		/* Tcl interpreter for error reporting */
    CONST char *eventList;		/* Event list to parse */
    int *maskPtr;		/* Returned event mask */
{
    int i;
    int eventc;
    CONST84 char **eventv;
    int events = 0;

    if (Tcl_SplitList(interp, eventList, &eventc, &eventv) !=
	     TCL_OK) {
	Tcl_AppendResult(interp, "bad parameter \"", eventList,
			 "\" for -events flag", (char *) NULL);
	return TCL_ERROR;
    }

    for (i = 0; i < eventc; i++) {
	if (strcmp(eventv[i], "x") == 0)
	    *maskPtr |= TCL_WINDOW_EVENTS;
	else if (strcmp(eventv[i], "window") == 0)
	    *maskPtr |= TCL_WINDOW_EVENTS;
	else if (strcmp(eventv[i], "rpc") == 0)
	    *maskPtr |= TCL_FILE_EVENTS;
	else if (strcmp(eventv[i], "file") == 0)
	    *maskPtr |= TCL_FILE_EVENTS;
	else if (strcmp(eventv[i], "timer") == 0)
	    *maskPtr |= TCL_TIMER_EVENTS;
	else if (strcmp(eventv[i], "idle") == 0)
	    *maskPtr |= TCL_IDLE_EVENTS;
	else {
	    Tcl_AppendResult(interp, "unknown event type \"", eventv[i],
		     "\" : should be all, or list of ",
		     "x, window, rpc, file, timer, or idle", (char *) NULL);
	    ckfree((char *) eventv);
	    return TCL_ERROR;
	}
    }

    ckfree((char *) eventv);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * MakeActivationRecord --
 *
 *	Create an activation record for a new, outbound RPC.
 *
 * Results:
 *	Pointer to the new record, or NULL on error.
 *
 * Side effects:
 *	Creates a timer handler that may need to be cleared
 *	later on.  The hash table activeRPCs now contains an
 *	entry for the activation record, so it'll have to be
 *	free'd later on.
 *
 *--------------------------------------------------------------
 */
static ActiveRPC *
MakeActivationRecord (rpcChanPtr, setTimeout, timeout)
    RPCChannel *rpcChanPtr;	/* Channel we're sending RPC on. */
    int setTimeout;		/* Should we set a timeout? */
    int timeout;		/* Timeout (in milliseconds) */
{
    int i, tries;
    ActiveRPC *newRecPtr;
    Tcl_HashEntry *entryPtr;
    int isNew;

    /*
     * Create/initialize new activation record
     */

    newRecPtr = (ActiveRPC *)ckalloc (sizeof(ActiveRPC));
    newRecPtr->flags = RPC_WAITING;
    newRecPtr->time = TclpGetSeconds();
    newRecPtr->chanPtr = rpcChanPtr;
    newRecPtr->id = 0;

    /*
     * Find a free id, and install a new activation record for it
     */
    for (i = activeId, tries = 0; tries++ < RPC_MAX_ACTIVE_RPCS;
		    i = (i+1)%RPC_MAX_ACTIVE_RPCS) {
	entryPtr = Tcl_CreateHashEntry(&activeRPCs, (char *)i, &isNew);
	if (isNew) {
	    DBG(printf("Adding %s:%d to hash table\n",
		    Tcl_GetChannelName(newRecPtr->chanPtr->chan), i));
	    Tcl_SetHashValue(entryPtr, (ClientData)newRecPtr);
	    newRecPtr->id = i;
	    activeId = (i+1)%RPC_MAX_ACTIVE_RPCS;
	    if (setTimeout) {
		newRecPtr->token = Tcl_CreateTimerHandler(timeout,
			DpTimeoutHandler, (ClientData)i);
	    } else {
		newRecPtr->token = NO_TOKEN;
	    }
	    return newRecPtr;
	}
    }

    /*
     * Couldn't find a free one -- return error
     */
    return NULL;
}


/*
 *--------------------------------------------------------------
 *
 * DpRPCCmd --
 *
 *	Send an RPC message and wait for a reply.
 *
 * Results:
 *	A standard Tcl result
 *
 * Side effects:
 *	A message is sent on the channel, and the procedure blocks
 *	until the return value is received or the RPC times out.
 *
 *--------------------------------------------------------------
 */
            /* ARGSUSED */
int
Dp_RPCCmd (clientData, interp, argc, argv)
    ClientData clientData;          /* ignored */
    Tcl_Interp *interp;             /* tcl interpreter */
    int argc;                       /* Number of arguments */
    CONST84 char **argv;     /* Arg list */
{
    RPCChannel *rpcChanPtr;
    ActiveRPC *activePtr, *arPtr;
    int i;
    int rpcArgc;
    CONST84 char * CONST *rpcArgv;
    char *command;
    Tcl_HashEntry *entryPtr;
    int returnValue;
    int rc = TCL_OK;

    /*
     * The default values for the value-option pairs
     */
    int timeout = -1;
    CONST char *timeoutReturn = NULL;
    int events = TCL_FILE_EVENTS;

    /*
     * Flags to indicate that a certain option has been set by the
     * command line
     */
    int setTimeout = 0;
    int setTimeoutReturn = 0;

    if (argc < 3) {
        goto usage;
    }

    for (rpcChanPtr = registeredChannels; rpcChanPtr != NULL;
	    rpcChanPtr = rpcChanPtr->next) {
	if (strcmp(argv[1], rpcChanPtr->name) == 0) {
	    break;
	}
    }
    if (rpcChanPtr == NULL) {
	Tcl_AppendResult(interp, "Attempt to send RPC over unregistered ",
		"channel. Use dp_admin to register channel first.", NULL);
        return TCL_ERROR;
    }

    /*
     * Ok, found the channel and it's valid.  Now, parse the value/option
     * pairs.
     */

    for (i=2; i<argc; i+=2) {
        int v = i+1;
	int len = strlen(argv[i]);

	if (strncmp(argv[i], "-timeout", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (Tcl_GetInt(interp, argv[v], &timeout) != TCL_OK) {
		return TCL_ERROR;
	    }
	    setTimeout = 1;
	} else if (strncmp(argv[i], "-timeoutReturn", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    timeoutReturn = argv[v];
	    setTimeoutReturn = 1;
	} else if (strncmp(argv[i], "-events", len)==0) {
	    if (v==argc) {goto arg_missing;}

	    if (strcmp (argv[v], "all") == 0) {
		events = TCL_ALL_EVENTS;
	    } else {
		DpParseEventList (interp, argv[v], &events);
	    }
	} else {
	    rpcArgc = argc - i;
	    rpcArgv = argv + i;
	    break;
	}
    }

    /*
     * If -timeoutReturn is specified, then -timeout must be
     */

    if (setTimeoutReturn && !setTimeout) {
	Tcl_AppendResult(interp, " Using -timeoutReturn requires -timeout\n",
	     NULL);
	return TCL_ERROR;
    }

    /*
     * Adjust the event mask so it's consistent
     */

    if (setTimeout) {
        events |= TCL_TIMER_EVENTS;
    }

    /*
     * Make an activation record for this RPC,
     * package up the RPC and send it
     */

    activePtr = MakeActivationRecord (rpcChanPtr, setTimeout, timeout);
    if (activePtr == NULL) {
        Tcl_AppendResult (interp, "Error -- ran out of RPC identifiers ",
        	"(over a million active RPCs?)\n", NULL);
	return TCL_ERROR;
    }

    command = Tcl_Merge(rpcArgc, rpcArgv);
    if (DpSendRPCMessage (rpcChanPtr->chan, TOK_RPC, activePtr->id, command)
	    != TCL_OK) {
	Tcl_AppendResult(interp, "Error sending RPC on channel ",
		Tcl_GetChannelName(rpcChanPtr->chan), NULL);
	ckfree((char *) command);
	rc = TCL_ERROR;
	goto cleanup;
    }
    ckfree((char *) command);

    /*
     * Wait for reply in an event loop.
     */
    while (activePtr->flags == RPC_WAITING) {
        Tcl_DoOneEvent (events);
    }

    /*
     * If we got a timeout, call the timeout handler (if there is one)
     * or trigger an error.
     *
     * If the RPC was cancelled, clear the timer event and return an
     * error.
     *
     * If there wasn't a timeout or error, clear the timer event, set
     * the variables according to the reply, and return.
     */

    if (activePtr->flags & RPC_TIMEDOUT) {
        if (timeoutReturn) {
	    /*
	     * Allocate memory for timeout proc and channel name
	     */
	    char *cmd = ckalloc(strlen(timeoutReturn) + 16);

	    sprintf(cmd, "%s %s", timeoutReturn,
		    Tcl_GetChannelName(rpcChanPtr->chan));
            rc = Tcl_GlobalEval(interp, cmd);
            ckfree(cmd);
            goto cleanup;
        } else {
	    Tcl_AppendResult(interp, "RPC timed out on channel ", argv[1],
		    NULL);
	    rc = TCL_ERROR;
            goto cleanup;
        }
    } else {
	if (activePtr->token != NO_TOKEN) {
	    Tcl_DeleteTimerHandler(activePtr->token);
	}
	if (activePtr->flags & RPC_CANCELLED) {
	    Tcl_AppendResult(interp, "RPC cancelled on channel ", argv[1],
		    NULL);
	    rc = TCL_ERROR;
            goto cleanup;
	}
	Tcl_SetResult (interp, activePtr->result, TCL_VOLATILE);
	rc = activePtr->returnValue;
	ckfree((char *)activePtr->result);
	goto cleanup;
    }

cleanup:
    entryPtr = Tcl_FindHashEntry(&activeRPCs, (char *)activePtr->id);
    if (entryPtr != NULL) {
	arPtr = (ActiveRPC *) Tcl_GetHashValue(entryPtr);
	DBG(printf("Deleting %s:%d from hash table\n",
		Tcl_GetChannelName(arPtr->chanPtr->chan), arPtr->id, activePtr->id));
	Tcl_DeleteHashEntry(entryPtr);
    }
    ckfree((char *)activePtr);
    return rc;

usage:
    Tcl_AppendResult(interp, "Usage:\n", "\"", argv[0],
	    " <channel> ?-timeout milliseconds ?-timeoutReturn callback??",
	    " ?-events eventList? command ?args ...?\"\n",
	 NULL);
    return TCL_ERROR;

arg_missing:
    Tcl_AppendResult(interp, "value for \"", argv[argc-1], "\" missing", NULL);
    return TCL_ERROR;
}


/*
 *--------------------------------------------------------------
 *
 * Dp_RDOCmd --
 *
 *	Send an RDO message and return.
 *
 * Results:
 *	A standard Tcl result
 *
 * Side effects:
 *	A message is sent on the channel.  This may cause the receiving
 *	process to send us a reply, which will invoke DpReadRPCChannel.
 *
 *--------------------------------------------------------------
 */

            /* ARGSUSED */
int
Dp_RDOCmd (clientData, interp, argc, argv)
    ClientData clientData;          /* ignored */
    Tcl_Interp *interp;             /* tcl interpreter */
    int argc;                       /* Number of arguments */
    CONST84 char **argv;     /* Arg list */
{
    RPCChannel *rpcChanPtr;
    int rpcArgc, i;
    CONST84 char * CONST *rpcArgv;
    char *command, *cmdStr;
    CONST char *onerror, *callback;
    char *cmd;

    if (argc < 3) {
	Tcl_AppendResult(interp, "Wrong number of args", NULL);
        goto usage;
    }

    for (rpcChanPtr = registeredChannels; rpcChanPtr != NULL;
	    rpcChanPtr = rpcChanPtr->next) {
	if (strcmp(argv[1], rpcChanPtr->name) == 0) {
	    break;
	}
    }

    if (rpcChanPtr == NULL) {
	Tcl_AppendResult(interp, "Attempted to send RDO over unregistered ",
		"channel.\nUse dp_admin to register channel first.", NULL);
        return TCL_ERROR;
    }

    /*
     * Ok, found the channel and it's valid.  Now, parse the value/option
     * pairs.
     */

    onerror = callback = NULL;
    for (i=2; i<argc; i+=2) {
        int v = i+1;
	int len = strlen(argv[i]);

	if (strncmp(argv[i], "-callback", len)==0) {
	    if (v==argc) {goto arg_missing;}
	    callback = argv[v];
	} else if (strncmp(argv[i], "-onerror", len)==0) {
	    if (v==argc) {goto arg_missing;}
	    if (!strcmp(argv[v], "none")) {
		onerror = "tkerror";
	    } else {
		onerror = argv[v];
	    }
	} else {
	    rpcArgc = argc - i;
	    rpcArgv = argv + i;
	    break;
	}
    }

    cmd = Tcl_Merge(rpcArgc, rpcArgv);

    if (onerror != NULL) {
	if (callback != NULL) {
	    /*
	     * Both onerror & callback specified.  Form of command is: if
	     * [catch $cmd dp_rv] { dp_RDO $dp_rpcFile eval {$onerror $dp_rv}
	     * } else { dp_RDO $dp_rpcFile eval {$callback $dp_rv} }
	     */
	    cmdStr = Tcl_Merge(argc, argv);
	    command = (char *) ckalloc(strlen(cmd) +
				       strlen(cmdStr) +
				       strlen(onerror) +
				       strlen(callback) +
				       strlen(ceCmdTemplate));
	    sprintf(command, ceCmdTemplate, cmd, cmdStr, onerror, callback);
	    ckfree(cmdStr);
	} else {
	    /*
	     * Just onerror specified.  Form of command is: if [catch $cmd
	     * dp_rv] { dp_RDO $dp_rpcFile eval {$onerror $dp_rv} }
	     */
	    cmdStr = Tcl_Merge(argc, argv);
	    command = (char *) ckalloc(strlen(cmd) +
				       strlen(onerror) +
				       strlen(cmdStr) +
				       strlen(eCmdTemplate));
	    sprintf(command, eCmdTemplate, cmd, cmdStr, onerror);
	    ckfree(cmdStr);
	}
    } else {
	if (callback != NULL) {
	    /*
	     * Just callback specified.  Form of command is: dp_RDO
	     * $dp_rpcFile $callback [$cmd]
	     */
	    command = (char *) ckalloc(strlen(cmd) +
				       strlen(callback) +
				       strlen(cCmdTemplate));
	    sprintf(command, cCmdTemplate, cmd, callback);
	} else {
	    /*
	     * No callbacks specified.  Form of command is: $cmd
	     */
	    command = (char *) ckalloc(strlen(cmd) + 1);
	    strcpy(command, cmd);
	}
    }
    ckfree((char *) cmd);
    DpSendRPCMessage(rpcChanPtr->chan, TOK_RDO, 0, command);
    ckfree((char *) command);
    return TCL_OK;

usage:
    Tcl_AppendResult(interp, " Usage:\n", "\"", argv[0],
	    " <channel> ?-events eventList? ?-callback script? ?-onerror script? command ?args ...?\"\n",
	 NULL);
    return TCL_ERROR;

arg_missing:
    Tcl_AppendResult(interp, "value for \"", argv[argc-1], "\" missing", NULL);
    return TCL_ERROR;
}


/* ----------------------------------------------------
 *
 *    DpCancelRPC --
 *
 *	Breaks off the current RPC(s?) and forces it
 *	to return an error.
 *
 *    Returns
 *
 *	Nothing.
 *
 *    Side Effects
 *
 *	All waiting RPCs on the given channel are cancelled.
 *
 * -----------------------------------------------------
 */
static void
DpCancelRPC(rpcChanPtr)
    RPCChannel *rpcChanPtr;		/* (in) RPCChannel to cancel */
{
    ActiveRPC *rpcList;
    Tcl_HashEntry *hashEntry;
    Tcl_HashSearch hashStruct;

    for (hashEntry = Tcl_FirstHashEntry(&activeRPCs, &hashStruct);
	    hashEntry != NULL;
	    hashEntry = Tcl_NextHashEntry(&hashStruct)) {
	rpcList = (ActiveRPC *) Tcl_GetHashValue(hashEntry);
	if (rpcList->chanPtr == rpcChanPtr) {
	    /*
	     * Mark it as cancelled so the event
	     * loop will exit.
	     */
	    rpcList->flags = RPC_CANCELLED;
	}
    }
}


/* ----------------------------------------------------
 *
 *    Dp_CancelRPCCmd --
 *
 *	Command parser for the dp_CancelRPC command.
 *
 *    Returns
 *
 *	TCL_OK or TCL_ERROR.
 *
 *    Side Effects
 *
 *	RPCs may be cancelled.
 *
 * -----------------------------------------------------
 */
int
Dp_CancelRPCCmd(unused, interp, argc, argv)
    ClientData unused;
    Tcl_Interp *interp;
    int argc;
    CONST84 char **argv;
{
    RPCChannel *chanPtr;
    Tcl_Channel chan;
    int mode, rc = TCL_OK, i;
    CONST84 char *chanName;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " [?chanID? ?chanID? ... | all]",
			 (char *) NULL);
	return TCL_ERROR;
    }

    if ((argc == 2) && !strcmp(argv[1], "all")) {
    	for (chanPtr = registeredChannels; chanPtr != NULL;
    		chanPtr = chanPtr->next) {
	    DpCancelRPC(chanPtr);
	}
    } else {
    	/*
    	 * Now we need to cycle through all the channel IDs given,
    	 * find the actual channel structure and then find the RPC
    	 * structure cooresponding to that channel.
    	 */
    	for (i = 1; i != argc; i++) {
	    chan = Tcl_GetChannel(interp, argv[i], &mode);
	    if (chan == NULL) {
		return TCL_ERROR;
	    } else {
		chanName = Tcl_GetChannelName(chan);
		for (chanPtr = registeredChannels; chanPtr != NULL;
			chanPtr = chanPtr->next) {
		    if (!strcmp(Tcl_GetChannelName(chanPtr->chan), chanName)) {
		    	DpCancelRPC(chanPtr);
		    	break;
		    }
		}
	    }
	}
    }
    return rc;
}


/*
 *--------------------------------------------------------------
 *
 * DpRegisterRPCChannel --
 *
 *	This function turns a Tcl-DP channel into an RPC channel
 *	by creating an event handler and the appropriate data structure.
 *
 * Results:
 *	A standard Tcl result
 *
 * Side effects:
 *	From now on, whenever the channel is readable, ReadRPCChannel
 *	will be called
 *
 *--------------------------------------------------------------
 */
static int
DpRegisterRPCChannel (interp, chanName, checkCmd)
    Tcl_Interp *interp;
    CONST char *chanName;
    CONST char *checkCmd;
{
    Tcl_Channel chan;
    int mode;
    RPCChannel *prev, *searchPtr;
    RPCChannel *newRpcChannelPtr;

    /*
     * Make sure the channel hasn't been registered already.
     */

    for (prev = NULL, searchPtr = registeredChannels; searchPtr != NULL;
	    prev = searchPtr, searchPtr = searchPtr->next) {
	if (strcmp(chanName, searchPtr->name) == 0) {
	    Tcl_AppendResult(interp, "Channel ", chanName,
		    " is already registered", NULL);
	    return TCL_ERROR;
	}
    }

    /*
     * Check to make sure we can find the channel, and that it's
     * readable/writeable
     */

    chan = Tcl_GetChannel(interp, chanName, &mode);
    if (chan == NULL) {
	return TCL_ERROR;
    }
    if (mode != (TCL_READABLE | TCL_WRITABLE)) {
	Tcl_AppendResult(interp, "Can't use channel ", chanName,
		 " for RPC -- channel is not both readable and writeable",
		  NULL);
	return TCL_ERROR;
    }

    /*
     * Things look ok!  Make the file non-blocking, create a file handler
     * for the channel, and return
     */

    if (Tcl_SetChannelOption (interp, chan, "-blocking", "0") != TCL_OK) {
    	return TCL_ERROR;
    }
    newRpcChannelPtr = (RPCChannel *)ckalloc(sizeof(RPCChannel));
    newRpcChannelPtr->name = ckalloc(strlen(chanName) + 1);
    strcpy(newRpcChannelPtr->name, chanName);
    newRpcChannelPtr->interp = interp;
    newRpcChannelPtr->error = 0;
    newRpcChannelPtr->bufLen = 0;
    newRpcChannelPtr->maxLen = RPC_BUFFER_SIZE;
    newRpcChannelPtr->buffer = ckalloc(RPC_BUFFER_SIZE);
    memset(newRpcChannelPtr->buffer, 0, RPC_BUFFER_SIZE);
    newRpcChannelPtr->chan = chan;
    newRpcChannelPtr->checkCmd = NULL;
    newRpcChannelPtr->flags = 0;
    if (checkCmd) {
	newRpcChannelPtr->checkCmd = ckalloc(strlen(checkCmd) + 1);
	strcpy(newRpcChannelPtr->checkCmd, checkCmd);
    }
    newRpcChannelPtr->next = registeredChannels;
    registeredChannels = newRpcChannelPtr;
    Tcl_CreateChannelHandler(chan, TCL_READABLE, DpReadRPCChannelCallback,
	    (ClientData)newRpcChannelPtr);
    return TCL_OK;
}


/*
 *--------------------------------------------------------------
 *
 * DpDeleteRPCChannel --
 *
 *	Clears the RPC channel handler created by CreateRPCChannel
 *	and frees up the data structures associated with it.
 *
 * Results:
 *	A standard Tcl result
 *
 * Side effects:
 *	rpcChanPtr is free'd and should not be referenced.
 *
 *--------------------------------------------------------------
 */
static int
DpDeleteRPCChannel (interp, rpcChanPtr)
    Tcl_Interp *interp;
    RPCChannel *rpcChanPtr;
{
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch hashSearchRec;
    RPCChannel *searchPtr, *prev;
    ActiveRPC *activePtr;
    Tcl_Channel chan;
    int mode;

    if (rpcChanPtr == NULL) {
	return TCL_ERROR;
    }

    if (rpcChanPtr->flags & CHAN_BUSY) {
	rpcChanPtr->flags |= CHAN_FREE;
	return TCL_OK;
    }

    for (searchPtr = registeredChannels, prev = NULL;
	    searchPtr != NULL;
	    prev = searchPtr, searchPtr = searchPtr->next) {
	if (!strcmp(rpcChanPtr->name, searchPtr->name)) {
	    break;
	}
    }


    /*
     * Clear the file handler, clear all active RPCs,
     * and free its memory.
     */

    if ((chan = Tcl_GetChannel(interp, searchPtr->name, &mode)) != NULL) {
	Tcl_DeleteChannelHandler(chan, DpReadRPCChannelCallback,
	    	(ClientData)searchPtr);
    }

    DpCancelRPC(searchPtr);

    if (prev == NULL) {
	registeredChannels = searchPtr->next;
    } else {
	prev->next = searchPtr->next;
    }
    ckfree((char *) searchPtr->name);
    ckfree((char *) searchPtr->buffer);
    if (searchPtr->checkCmd) {
	ckfree((char *) searchPtr->checkCmd);
    }
    ckfree((char *) searchPtr);
    return TCL_OK;
}


/*
 *--------------------------------------------------------------
 *
 * Dp_AdminCmd --
 *
 *	This function implements the "dp_admin" command.  It
 *	handles administrative functions of the RPC module.
 *	It's usage:
 *
 *		dp_admin register <chan> ?-check checkCmd?
 *		dp_admin delete <chan>
 *
 *	If called with "register", the channel is made into an RPC
 *	channel (i.e., RPCs can be sent/received across it).  If called
 *	with "delete", RPCs will no longer be sent/received across it.
 *
 * Results:
 *	A standard tcl result.
 *
 * Side effects:
 *	Once a channel is registered, all input on the channel will
 *	be received through DpReadRPCChannel(), which will be called
 *	from the event loop.
 *
 *--------------------------------------------------------------
 */
    /* ARGSUSED */
int
Dp_AdminCmd (clientData, interp, argc, argv)
    ClientData clientData;          /* ignored */
    Tcl_Interp *interp;             /* tcl interpreter */
    int argc;                       /* Number of arguments */
    CONST84 char **argv;     /* Arg list */
{
    char c;
    int len;
    Tcl_Channel chan = NULL;
    RPCChannel *searchPtr;
    CONST84 char *checkCmd = NULL;

    if (argc != 3 && argc != 5) {
	Tcl_AppendResult(interp, "Wrong number of args", NULL);
	goto usage;
    }

    c = argv[1][0];
    len = strlen(argv[1]);

    /*
     * Check to see if we got a check procedure.
     */

    if (argc == 5) {
    	if (!strcmp(argv[3], "-check")) {
	    checkCmd = argv[4];
	    if (!strcmp(checkCmd, "none")) {
		checkCmd = NULL;
	    }
	} else {
	    goto usage;
	}
    }

    /* ------------------------ REGISTER ------------------------------ */
    if ((c == 'r') && (strncmp(argv[1], "register", len) == 0)) {
	return DpRegisterRPCChannel (interp, argv[2], checkCmd);

    /* ------------------------ DELETE -------------------------------- */
    } else if ((c == 'd') && (strncmp(argv[1], "delete", len) == 0)) {
	for (searchPtr = registeredChannels; searchPtr != NULL;
		searchPtr = searchPtr->next) {
	    if (!strcmp(argv[2], searchPtr->name)) {
		break;
	    }
	}
	if (searchPtr == NULL) {
	    Tcl_AppendResult(interp, "Channel \"", argv[2],
		    "\" not registered.", NULL);
	    return TCL_ERROR;
	}
	return DpDeleteRPCChannel (interp, searchPtr);

    /* ------------------------ ERROR --------------------------------- */
    } else {
        goto usage;
    }

usage:
    Tcl_AppendResult(interp, " Possible usages:\n",
	 "\"", argv[0], " register <channel> ?-check checkCmd?\"\n",
	 "\"", argv[0], " delete <channel>\"\n",
	 NULL);
    return TCL_ERROR;
}


/*
 *--------------------------------------------------------------
 *
 * DpSendRPCMessage --
 *
 *	Send a formatted RPC packet out on the specified channel.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
static int
DpSendRPCMessage (chan, token, id, mesgStr)
    Tcl_Channel chan;
    int token;				/* in: msg type token */
    int id;				/* in: RPC ID # */
    char *mesgStr;			/* in: actual RPC string */
{
    char *bufStr;
    int result, totalLength;

    totalLength = strlen(mesgStr) + 16;
    bufStr = ckalloc(totalLength + 1);
    sprintf(bufStr, "%6d %c %6d %s", totalLength, (char)token, id, mesgStr);

    DBG(printf("\nSending RPC : %s on %s\n", bufStr, Tcl_GetChannelName(chan)));

    result = Tcl_Write(chan, bufStr, totalLength);
    ckfree(bufStr);
    if (result != totalLength) {
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *--------------------------------------------------------------
 *
 * DpRPCInit --
 *
 *	This function initializes the global data structures in
 *      this module.
 *
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */
int
DpRPCInit (interp)
    Tcl_Interp *interp;
{
    Tcl_InitHashTable(&activeRPCs, TCL_ONE_WORD_KEYS);
    activeId = 0;
    return TCL_OK;
}


