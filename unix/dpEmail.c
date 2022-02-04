/*
 * dpEmail.c --
 *
 *	This file contains the implementation of the email channel.
 *
 */

/*
 * Important facts about email channels:
 *
 * 1. Email channels are always bidirectional.
 * 2. The subject line of the messages is retrieved and stored in the seek
 * file, but not used.
 * 3. An email channel is always writable; no exceptions are signalled on an
 * email channel.
 * 4. The application will create and/or modify the user's ~/.forward file. If
 * several copies of this program are run in the same account, and if the user
 * did not have a ~/.forward file before the first copy was started, it might
 * happen (depending on the order in which the applications were started/ended),
 * that the user will end all the applications, s/he will have a ~/.forward file
 * containing "\user-name".
 * 5. If an application that opened email channels crashes, the user is advised
 * to check and possibly edit the ~/.forward file, and delete files
 * ~/.emailSpool*, ~/.emailSeek*, ~/.emailChannel*, and ~/.emailLock*.
 * 6. The identifier parameter is required in dp_connect whenever the number of
 * opened email channels is zero. In all other cases it is ignored, but it is
 * for correctness (i.e. if present, it has to be a positive integer).
 */


#include <sys/param.h>
#include <string.h>
#include <fcntl.h>
#include "generic/dpInt.h"

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

/*
 * It is likely that these definitions are only temporary.
 * They should be reviewed before the final release.
 */

#define DP_ARBITRARY_LIMIT           500
#define DP_GARBAGE_LIMIT             100000
#define DP_MAX_FILE_NAME_LENGTH      20 /* OS dependent */
#define DP_HOME_DIRECTORY           "/tmp"


#define min(x, y) (((x) < (y)) ? (x) : (y))



typedef struct {

    /*
     * This stores the complete address of the sender/receiver. The address can
     * consist of any non-white characters, except for the first character which
     * can not be '*'.
     */

    char address  [DP_ARBITRARY_LIMIT + 1];

    /*
     * If border is zero, the program will not read across message boundaries.
     * If border is non-zero, then message boundaries will be ignored: whenever
     * a read is requested, the amount of data returned will be min(requested,
     * available). In the current implementation this value is always zero, it
     * can not be set or retrieved with fconfigure.
     */

    int  border;

    /*
     * If this value is non-zero, then the data will be read, but not consumed.
     */

    int peek;

    /*
     * This variable stores a lower bound on the number of bytes that are
     * available for reading in the email channel. It is decreased by read's,
     * and if it reaches zero, it is reset to its real value, based on the data
     * stored in the seek file. It's main use is to speed up the event
     * notification mechanism: if it is positive, then data is waiting to be
     * read, if it is zero, one has to process the seek file to find the answer.
     */

    long available;

    /*
     * This variable stores the value of the "-sequence" parameter. It
     * it is zero, no sequence numbers will be associated with the
     * written messages.
     */

    int sequence;

    /*
     * This variable is the source for the sequence numbers that can
     * be associated with the messages sent out by the email channel.
     */

    long sequenceNo;

} EmailInfo;


/*
 * Prototypes for functions referenced only in this file.
 */



static int	CloseEmailChannel	_ANSI_ARGS_((ClientData instanceData,
                                                     Tcl_Interp *interp));

static int      InputEmailChannel	_ANSI_ARGS_((ClientData instanceData,
                                                     char *buf, int bufsize,
                                                     int  *errorCodePtr));

static int      OutputEmailChannel	_ANSI_ARGS_((ClientData instanceData,
                                                      CONST84 char *buf, int toWrite,
                                                      int *errorCodePtr));

static int      SOPEmailChannel	        _ANSI_ARGS_((ClientData instanceData,
                                                     Tcl_Interp *interp,
                                                     CONST char *optionName,
                                                     CONST char *optionValue));

#ifndef _TCL76
static int	GOPEmailChannel	        _ANSI_ARGS_((ClientData instanceData,
						     Tcl_Interp *interp,
                                                     CONST84 char *optionName,
                                                     Tcl_DString *dsPtr));
#else
static int	GOPEmailChannel	        _ANSI_ARGS_((ClientData instanceData,
                                                     char *optionName,
                                                     Tcl_DString *dsPtr));
#endif


#ifdef _TCL76
static Tcl_File	GFPEmailChannel	        _ANSI_ARGS_((ClientData instanceData,
                                                     int direction));
#else
static int	GFPEmailChannel	        _ANSI_ARGS_((ClientData instanceData,
                                                     int direction,
                                                     FileHandle *fd));
#endif

static int	CRPEmailChannel	        _ANSI_ARGS_((ClientData instanceData,
                                                     int mask));

static void	WCPEmailChannel         _ANSI_ARGS_((ClientData instanceData,
                                                     int mask));

static long	OverwriteRestOfFile	_ANSI_ARGS_((FILE *fp, int delta));

static int	LogicalEraseFromSeek	_ANSI_ARGS_((char *sender));

static int	CleanUpFiles		_ANSI_ARGS_((void));

static long	ReadFromSpool		_ANSI_ARGS_((char *sender, char *where,
                                                     long howMany,  int border,
                                                     int peek));

static int	SetAvailable		_ANSI_ARGS_((ClientData instanceData));


/*
 * This structure stores the names of the functions that Tcl calls when certain
 * actions have to be performed on an email channel. To understand this entry,
 * please refer to the documentation of the Tcl_CreateChannel and its associated
 * functions in the Tcl 7.6 documentation.
 *
 * Notice that an email channel will always be non-blocking. Seek will not be
 * allowed either.
 */

static Tcl_ChannelType emailChannelType = {
    "email",
    DP_CHANNEL_VERSION,	/* TCL_CHANNEL_VERSION_1, TCL_CHANNEL_VERSION_2, and so on */
    CloseEmailChannel,    /* closeProc        */
    InputEmailChannel,    /* inputProc        */
    OutputEmailChannel,   /* outputProc       */
    NULL,                 /* seekProc         */
    SOPEmailChannel,      /* setOptionProc    */
    GOPEmailChannel,      /* getOptionProc    */
    WCPEmailChannel,      /* watchChannelProc */
#ifdef _TCL76
    CRPEmailChannel,      /* channelReadyProc */
#endif
    GFPEmailChannel,       /* getFileProc      */
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
 * Variables that are local to this file:
 */


/*
 * This variable stores the unique identifier that is used to call
 * CreateEmailChannel, and which is used to name the channel, seek, and spool
 * file (see below).
 *
 * NOTE: currently this variable has no real use as a global variable. If the
 * application would want to shut down without intrerupting the incoming
 * communication on the email channels, this information could be used later
 * to retrieve the channel, seek, and spool files, and to revert the
 * modifications of the ~/.forward file when the last email channel will be
 * closed.
 */

static int   gIdentifier = 0;


/*
 * This stores the path of the spool file. Each application has a unique spool
 * file in ~/.emailSpoolXXX, where XXX is the unique identifier asscociated
 * with the running application (see gIdentifier above - it is set in
 * CreateEmailChannel). The spool file contains all the data transmitted
 * in the body (e.g. no address, subject is included) of the email
 * messages. There  is no explicit representation of the message boundaries,
 * these are identified based on the information stored in the seek file
 * (see below).
 */

static char  spoolFile   [MAXHOSTNAMELEN + 1];


/*
 * This stores the path of the channel file. Each application has a unique
 * channel file in ~/.emailChannelXXX, where XXX is the unique identifier
 * associated with the running application (see gIdentifier above and in
 * CreateEmailChannel). The channel file contains the names of all the
 * email addresses that are authorized to send/receive mail. Each address is
 * stored on one separate line.
 */

static char  channelFile [MAXHOSTNAMELEN + 1];


/*
 * This stores the name of the seek file. Each application has a unique seek
 * file in ~/.emailSeekXXX, where XXX is the unique identifier asscociated
 * with the running application (see gIdentifier above and in
 * CreateEmailChannel). The seek file contains one line for each message
 * whose body was appended to the spool file. The format of the entry is the
 * following:
 *
 * <address><space><message lenght><space><used-lenght><space><subject><RET>
 *
 * Fields <message-lenght> and <used-lenght> have exactly 8 digits, while
 * <address> and <subject> are not constraind in lenght. <address> can not
 * contain spaces, nor have '*' as its first character.
 *
 * If a message was completely read, or the channel associated with it was
 * closed, then the first character of the entry is overwritten with "*". These
 * entries will be removed when the first garbage collection is done.
 */

static char  seekFile    [MAXHOSTNAMELEN + 1];



/*
 * This stores the path of the ~/.forward file. When creating the first email
 * channel, this file is modified by adding a filter program to it. Only one
 * filter is added per application. The filter program uses the information
 * in the channel file to select the messages that are relevant; if they are,
 * it adds one entry to the seek file and appends the content of the message
 * body to the channel file. When the last email channel is closed, the filter
 * program is removed from the ~/.forward file.
 *
 * Important: Please refer to the note on the .forward file at the top of this
 * file.
 */

static char  fwdFile     [MAXHOSTNAMELEN + 1];


/*
 * This stores the path of the lock file. Each application uses a single lock
 * file in ~/.emailLockXXX, where XXX is the unique identifier
 * associated with the running application (see gIdentifier above and in
 * CreateEmailChannel). The lock file is used to assure that simultaneous
 * access of data files by the application and the filter program is impossible.
 */

static char  lockFile     [MAXHOSTNAMELEN + 1] = { '\0' };


 /*
 * This is a pointer to a string containing the path to the directory in which
 * the executable filter program is stored. The name of the executable should be
 * dpfilter.
 */

static char *libDirectory;


/*
 * This is a pointer to a string containing the path to the home directory. All
 * temporary files associated with the email channel will be stored here.
 */

static char *homeDirectory;


/*
 * In order to increase efficiency, the spool and seek file are garbage collected
 * only when the amount of garbage is above a given limit. This variable stores
 * the amount of garbage (already read data) in the spool file.
 */

static long  garbageInSpoolFile = 0;


/*
 * This is a counter that stores the number of opened email channels. It is
 * used to detect the first email channel that is opened in order to modify the
 * ~/.forward file, and to create the channel, spool and seek files. It is also
 * used to detect the closing of the last email channel when the content of the
 * ~/.forward file is restored, and the channel, spool and seek files are
 * deleted.
 */

static int openedChannels = 0;


/*
 * If, by the time the first channel was opened, there existed a ~/.forward file
 * the value of this variable will be set to 1. This value is tested when the
 * last channel is closed. If it is zero, the ~/.forward file will be deleted,
 * otherwise the program will only remove the filter program from it.
 */

static int wasThereAForwardFile = 0;



/*
 *-----------------------------------------------------------------------------
 *
 * CreateEmailChannel --
 *
 *	Creates an email channel to exchange information with "address". The
 *      "identifier" is used to uniquely distinguish between different
 *      applications that might run concurrently, and use email channels. In
 *      most of the cases, "identifier" can be the pid of the running program.
 *
 * Results:
 *
 *	Returns a channel data structure. If an error happens, NULL
 *      is returned.
 *
 * Side effects:
 *
 *      When the first email channel is created, the value of identifier is
 *      stored in the global variable gIdentifier. Also, the channel, seek and
 *      spool files are creates. The ~/.forward file is modified by adding to
 *      it a call to the filter program (emailFilter). For each email channel
 *      that is opened a new entry is added to the channel file.
 *
 *-----------------------------------------------------------------------------
 */

Tcl_Channel
DpCreateEmailChannel (interp, argc, argv)
    Tcl_Interp *interp;		/* (in) Pointer to tcl interpreter. */
    int argc;			/* (in) Number of arguments. */
    CONST84 char **argv;                /* (in) Argument strings. */
{
    static long	channelSerialNumber = 0;
    char	fwdFileBuffer	  [DP_ARBITRARY_LIMIT + 1];
    char	channelFileBuffer [DP_ARBITRARY_LIMIT + 1];
    char	chanName [30];
    CONST84 char       *address;
    int		i, identifier;
    FILE       *chanFilePtr, *fwdFilePtr;
    EmailInfo  *infoPtr;
    Tcl_Channel chan;

    address = NULL;
    identifier = 0;

    for (i = 0; i < argc; i += 2) {
        int v = i+1;
	size_t len = strlen(argv[i]);

	if (strncmp(argv[i], "-address", len)==0) {
	    if (v == argc) {
                goto arg_missing;
            }
	    address = argv[v];
	    if((address[0] == '\0') || (address[0] == '*')) {
                Tcl_AppendResult(interp, "the address for an email channel ",
                                 "can not be empty or start with a '*'", NULL);
		return NULL;
	    }
	} else if (strncmp(argv[i], "-identifier", len)==0) {
	    if (v == argc) {
                goto arg_missing;
            }

            if(Tcl_GetInt(interp, argv[v], &identifier) != TCL_OK) {
                return NULL;
            }

            if(identifier <= 0) {
                Tcl_AppendResult(interp, "the identifier for an email channel ",
                                "can not be zero, negative, or a string", NULL);
                return NULL;
	    }
	} else {
    	    Tcl_AppendResult(interp, "unknown option \"",
		    argv[i], "\", must be -address or -identifier", NULL);
	    return NULL;
	}
    }


    if((address == NULL) || ((openedChannels == 0) & (identifier <= 0))) {
        Tcl_AppendResult(interp, "address and/or identifier not defined for ",
                         "email channel", NULL);
	return NULL;
    }

    if(lockFile[0] == '\0') {

        /* Set the paths of the files used by the email channels. */

        if((homeDirectory = getenv("HOME")) == NULL) {
            homeDirectory = DP_HOME_DIRECTORY;
        }

        if((libDirectory = getenv("DP_LIBRARY")) == NULL) {
            libDirectory = DP_LIBRARY_DIRECTORY;
        }

        /* Protect against overflow due to very long paths. */
        if(strlen(homeDirectory) > MAXPATHLEN - DP_MAX_FILE_NAME_LENGTH) {
    	    Tcl_AppendResult(interp, "too long path for home directory ",
                             homeDirectory, NULL);
            return NULL;
        }

        sprintf(spoolFile, "%s/.emailSpool%d", homeDirectory, identifier);
        sprintf(channelFile, "%s/.emailChannel%d", homeDirectory, identifier);
        sprintf(seekFile, "%s/.emailSeek%d", homeDirectory, identifier);
        sprintf(lockFile, "%s/.emailLock%d", homeDirectory, identifier);
        sprintf(fwdFile, "%s/.forward", homeDirectory);

    }

    if(PutLock(lockFile) != 0) {
        return NULL;
    }

    if(openedChannels > 0) {

        if((chanFilePtr = fopen(channelFile, "r")) == NULL) {
           goto error0;
        }

        /* Check if this channel was already opened. */

        if(fgets(channelFileBuffer,sizeof(channelFileBuffer),chanFilePtr)==NULL){
            goto error1;
        }

        while(!feof(chanFilePtr)) {
            strtok(channelFileBuffer, " ");
            if(!strcasecmp(channelFileBuffer, address)) {
                /* Oops, this channel has been opened earlier! */
                goto error1;
            };
            if(fgets(channelFileBuffer, sizeof(channelFileBuffer),
                                                       chanFilePtr) == NULL) {
                if(!feof(chanFilePtr)) {
                    goto error1;
                }
            }
        }
    }

    /*
     * OK, either the channel file does not exist, either this channel
     * has not been opened yet.
     */

    sprintf(chanName, "email%ld", channelSerialNumber++);

    if(openedChannels == 0) {

        garbageInSpoolFile = 0;
        /* Assume there was no .forward file. */
        wasThereAForwardFile = 0;

        if(creat(spoolFile, 0x180) == -1) {
            goto error0;
        }

        if(creat(seekFile, 0x180) == -1) {
            unlink(spoolFile);
            goto error0;
        }
    }

    if((chanFilePtr = fopen(channelFile, "a")) == NULL) {
        goto error0;
    }

    if(fprintf(chanFilePtr, "%s\n", address) == EOF) {
        goto error1;
    }

    fclose(chanFilePtr);

    if(openedChannels == 0) {

        if(access(fwdFile, R_OK | W_OK) == -1) {
            /* .forward file does not exist or inaccessible */
            /* Try to create the file...                    */
            if(creat(fwdFile, 0x180) == -1) {
                /* File could not be created, nothing to do. */
                goto error2;
            }
        } else {
            wasThereAForwardFile = 1;
        }

        if((fwdFilePtr = fopen(fwdFile, "r+")) == NULL) {
              goto error3;
        }

        if(fgets(fwdFileBuffer, DP_ARBITRARY_LIMIT, fwdFilePtr) == NULL) {
            if(!feof(fwdFilePtr)) {
                goto error4;
            }
        }

        if(fseek(fwdFilePtr, 0, 0) == -1) {
            goto error4;
        }

        if(!wasThereAForwardFile) {
            if(fprintf(fwdFilePtr, "\"|%s/dpfilter %s/ .emailLock%d \
.emailChannel%d .emailSeek%d .emailSpool%d\", \\%s", libDirectory, homeDirectory,
                       identifier, identifier, identifier, identifier,
                       getenv("USER")) == EOF) {
                goto error4;
            }
        } else {
            if(fprintf(fwdFilePtr, "\"|%s/dpfilter %s/ .emailLock%d \
.emailChannel%d .emailSeek%d .emailSpool%d\", %s", libDirectory, homeDirectory,
                       identifier, identifier, identifier, identifier,
                       fwdFileBuffer) == EOF) {
                goto error4;
            }
        }

        fclose(fwdFilePtr);

    }

    /*
     * Create the instance data associated with this channel
     * only if everything went ok.
     */

    if((infoPtr = (EmailInfo *)ckalloc(sizeof(EmailInfo))) == NULL) {
        goto error0;
    }

    strcpy(infoPtr->address, address);
    infoPtr->border = 0;    /* Do not read across message boundaries. */
    infoPtr->peek   = 0;    /* Read the data, don't peek. */
    infoPtr->available = 0; /* No data available in the email channel. */
    infoPtr->sequence = 0;  /* No sequence numbers in messages. */
    infoPtr->sequenceNo = 0;/* Start sequences from 0. */

    if(openedChannels == 0) {
      	gIdentifier = identifier;
    }
    openedChannels++;
    RemoveLock(lockFile);

    chan = Tcl_CreateChannel(&emailChannelType, chanName, infoPtr,
	    TCL_READABLE | TCL_WRITABLE);
    Tcl_RegisterChannel(interp, chan);
    return chan;

error1:

    fclose(chanFilePtr);
    /* Continues with error0. */

error0:

    RemoveLock(lockFile);
    return NULL;

error4:

    fclose(fwdFilePtr);
    /* Continues with error3. */

error3:

    if(!wasThereAForwardFile)
        unlink(fwdFile);
    /* Continues with error2. */

error2:

    unlink(spoolFile);
    unlink(seekFile);

    RemoveLock(lockFile);

    return NULL;

arg_missing:
    Tcl_AppendResult(interp, "value for \"", argv[argc-1], "\" missing", NULL);
    return NULL;

}



/*
 *-----------------------------------------------------------------------------
 *
 * CloseEmailChannel --
 *
 *	Closes the given email channel.
 *
 * Results:
 *
 *	If everything goes well, it returns 0. If any error happens,
 *      it returns a POSIX error code.
 *
 * Side effects:
 *
 *      If the last email channel is closed, the channel, seek, and spool
 *      files are removed. Also, the filter program is removed from the
 *      ~/.forward file.
 *      If the email channel that is closed is not the last one, the
 *      corresponding entry is removed from the channel file. All the entries
 *      associated with this channel in the seek file will be modified to have
 *      a "*" character in the first position. These entries, together with
 *      their corresponding data in the spool file will be removed when
 *      the garbage collection procedure is run.
 *
 *-----------------------------------------------------------------------------
 */

static int
CloseEmailChannel (instanceData, interp)
    ClientData  instanceData;  /* (in) Pointer to EmailInfo struct. */
    Tcl_Interp *interp;        /* (in) Pointer to tcl interpreter. */
{

    FILE *fwdFilePtr, *chanFilePtr;
    int   delta;
    long  logicalEOF;
    char  buffer [DP_ARBITRARY_LIMIT + 1];


    if(PutLock(lockFile) != 0) {
        return ENOLCK;
    }

    if(openedChannels == 1) {

        /* We are closing the last open channel. */

        char  tmpBuffer [20 * DP_ARBITRARY_LIMIT + 1];
        char *tmp;
        char *found;
        int   error = 0;

        error += unlink(spoolFile);
        error += unlink(channelFile);
        error += unlink(seekFile);

        if(error) {
            Tcl_AppendResult(interp, "unable to delete spool file ", spoolFile,
                             " and/or channel file ", channelFile,
                             "and/or seekfile ", seekFile, NULL);

            /*
             * Errno is not reset if a successful call follows an erroneous one.
             */

            goto error4;
        }


        /* Remove the entry from the ~/.forward file. */
        if((fwdFilePtr = fopen(fwdFile, "r")) == NULL) {
            Tcl_AppendResult(interp,
                             "unable to open ~/.forward file for reading",
                             NULL);
            goto error3;
        }

        if(fgets(buffer, DP_ARBITRARY_LIMIT, fwdFilePtr) == NULL) {
            goto error1;
        }

        /* Find the name of the channel file in the .forward file */
        sprintf(tmpBuffer, "\"|%s/dpfilter %s/ .emailLock%d .emailChannel%d \
.emailSeek%d .emailSpool%d\", ", libDirectory, homeDirectory, gIdentifier,
                gIdentifier,  gIdentifier, gIdentifier);

        if((found = strstr(buffer, tmpBuffer)) == NULL) {
            /* Oops, it's not there! */
            Tcl_AppendResult(interp, "filter program not found in ~/.forward ",
			"file, did you remove it?", NULL);
            goto error1;
        }

        for(tmp = found + strlen(tmpBuffer); *tmp != '\0'; *found++ = *tmp++) {
            /* empty body */
        }

        *found = '\0';

        /* Was there a ~/.forward file when we opened the first channel? */
        if(!wasThereAForwardFile) {
            /* If no, did the somebody modify it since we created it? */
	    char *format = "\\%.*s";
	    char tmp [DP_ARBITRARY_LIMIT + 1];

            sprintf(tmp, format, sizeof(tmp) - strlen(format), getenv("USER"));
            if(!strcmp(buffer, tmp)) {
                /* No, the file was (probably) not modified; delete it. */

                openedChannels--;

                if(unlink(fwdFile) == -1) {
                    /* An error message would not be very relevant here. */
                    goto error4;
                }

                goto error0;
            }

            /*
             * Yes, the file was modified, rewrite it to disk as if it had been
             * there before this program strated to run.
             */
        }

        /* Write the modified content back to the ~/.forward file */

        if((fwdFilePtr = freopen(fwdFile, "w", fwdFilePtr)) == NULL) {
            Tcl_AppendResult(interp, "unable to open ~/.forward file for \
writing", NULL);
            goto error3;
        }

        if(fputs(buffer, fwdFilePtr) == EOF) {
            Tcl_AppendResult(interp, "can not write  ~/.forward file", NULL);
            goto error1;
        }

        fclose(fwdFilePtr);

        openedChannels--;

        goto error0;

    }

    /*
     * Logically eliminate unread messages associated with this
     * channel from the seek file by overwriting the first character of their
     * corresponding entries in the seek file with "*".
     */

    if(LogicalEraseFromSeek(((EmailInfo *)instanceData)->address) == -1) {
        Tcl_AppendResult(interp, "unable to eliminate garbage entries from ",
			 "the spoolfile", NULL);

	goto error3;
    }

    /*
     * Eliminate the entry in the channel file that is associated with
     * the channel we are closing. First, find the address in the channel
     * file.
     */

    if((chanFilePtr = fopen(channelFile, "r+")) == NULL) {
        Tcl_AppendResult(interp, "unable to open channel file ",
                         channelFile, NULL);
        goto error3;
    }

    if(fgets(buffer, DP_ARBITRARY_LIMIT, chanFilePtr) == NULL) {
        if(!feof(chanFilePtr)) {
            goto error2;
        }
    }

    while(!feof(chanFilePtr)) {
        strtok(buffer, " \n");
        if(!strcasecmp(buffer, ((EmailInfo *)instanceData) -> address)) {
            /* We found the entry we are looking for. */
            break;
        }
        if(fgets(buffer, DP_ARBITRARY_LIMIT, chanFilePtr) == NULL) {
            if(!feof(chanFilePtr)) {
                goto error2;
            }
        }
    }

    if(feof(chanFilePtr)) {
        /* Too bad, there is no channel to close. */
        Tcl_AppendResult(interp, "unable to close email channel ",
                         ((EmailInfo *)instanceData) -> address,
                         ". Not opened?", NULL);
        goto error2;
    }

    /* Eliminate the entry by overwriting it. */

    buffer[strlen(buffer)] = ' '; /* Undo the effect of strtok(). */
    delta = strlen(buffer);

    if((logicalEOF = OverwriteRestOfFile(chanFilePtr, delta)) == -1) {
        Tcl_AppendResult(interp, "unable to rewrite the channel file ",
                         channelFile, NULL);
        goto error2;
    }

    fclose(chanFilePtr);

    if(truncate(channelFile, logicalEOF) == -1) {
        Tcl_AppendResult(interp, "unable to rewrite the channel file ",
                         channelFile, NULL);
        goto error3;
    }

    openedChannels--;
    /* Continues with error0. */


error0:

    RemoveLock(lockFile);
    return 0;

error1:

    fclose(fwdFilePtr);
    RemoveLock(lockFile);

    return EINVAL;

error2:

    fclose(chanFilePtr);
    /* Continues with error3. */

error3:

    RemoveLock(lockFile);

    return EINVAL;

error4:

    RemoveLock(lockFile);

    return Tcl_GetErrno();

}




/*
 *-----------------------------------------------------------------------------
 *
 * OverwriteRestOfFile --
 *
 *	Starting from the current position in a file, it will eliminate chunk
 *      of size delta characters, by overwriting it with the data (if any) that
 *      follows in the file after the block to be deleted.
 *
 * Results:
 *
 *      A positive long integer specifying the logical end of the file.
 *      Normally, the file will be truncated to this size by the caller
 *      function. If an error happens, -1 will be returned. If this occurs, it
 *      is likely that the file content will be corrupted.
 *
 * Side effects:
 *
 *      None.
 *
 *-----------------------------------------------------------------------------
 */


static long
OverwriteRestOfFile (filePtr, delta)
    FILE *filePtr;	/* (in) File to work on. */
    int   delta;	/* (in) Number of bytes to eliminate. */
{
    char bigBuffer [ 20 * DP_ARBITRARY_LIMIT];
    long  temp, whereToWriteTo, whereToReadFrom;


    /*  O is a valid return value for fread's in this function */
    temp = fread(bigBuffer, sizeof(char), sizeof(bigBuffer), filePtr);

    whereToReadFrom = ftell(filePtr);
    whereToWriteTo  = whereToReadFrom - temp - delta;

    while(temp > 0) {

        if(fseek(filePtr, whereToWriteTo, 0) == -1) {
            return -1;
        }

        if(fwrite(bigBuffer, sizeof(char), temp, filePtr) != temp) {
            return -1;
        }

        if(fseek(filePtr, whereToReadFrom, 0) == -1) {
            return -1;
        }

        whereToWriteTo  += temp;
        temp = fread(bigBuffer, sizeof(char), sizeof(bigBuffer), filePtr);
        whereToReadFrom += temp;
    }

    return whereToWriteTo;
}



/*
 *-----------------------------------------------------------------------------
 *
 * LogicalEraseFromSeek --
 *
 *	Eliminates the entries associated with "sender" in the seek file.
 *	The elimination is done by overwriting the first character of each
 *      entry referring to a message received from "sender" with "*". The
 *      physical elimination of these entries, together with the corresponding
 *      data in the spool file, will be done when the garbage collection
 *      algorithm is run.
 *
 * Results:
 *
 *      0 if no error is detected, -1 in the opposite case. If an error
 *      occured, the content of the seek file might be damaged.
 *
 * Side effects:
 *
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static int
LogicalEraseFromSeek (sender)
    char *sender;	/* (in) Email address of sender. */
{
    FILE *seekFilePtr;
    char  buffer [DP_ARBITRARY_LIMIT];
    long  whereToReadFrom, whereToWriteTo;

    if((seekFilePtr = fopen(seekFile, "r+")) == NULL) {
        return -1;
    }

    whereToWriteTo = 0;

    if(fgets(buffer, DP_ARBITRARY_LIMIT, seekFilePtr) == NULL) {
        if(!feof(seekFilePtr)) {
            goto error1;
        }
    }

    whereToReadFrom = ftell(seekFilePtr);
    strtok(buffer, " ");
    while(!feof(seekFilePtr)) {
        if(!strcasecmp(sender, buffer)) {
            garbageInSpoolFile += atol(strtok(NULL, " "));

            /*
             * The repetition is not error: it undoes the effect of
             * the two calls to strtok() that were made above.
             */

            buffer[strlen(buffer)] = ' ';
            buffer[strlen(buffer)] = ' ';
            buffer[0] = '*';
            if(fseek(seekFilePtr, whereToWriteTo, 0) == -1) {
                goto error1;
            }
            if(fputs(buffer, seekFilePtr) == EOF) {
                goto error1;
            }
            if(fseek(seekFilePtr, whereToReadFrom, 0) == -1) {
                goto error1;
            }
        }
        if(fgets(buffer, DP_ARBITRARY_LIMIT, seekFilePtr) == NULL) {
            if(!feof(seekFilePtr)) {
                goto error1;
            }
        }
        whereToWriteTo = whereToReadFrom;
        whereToReadFrom += strlen(buffer);
        strtok(buffer, " ");
    }

    fclose(seekFilePtr);

    return 0;

error1:

    fclose(seekFilePtr);
    return -1;

}




/*
 *-----------------------------------------------------------------------------
 *
 * CleanUpFiles --
 *
 *	Clean up the garbage accumulated in the seek and spool files by
 *	removing the entries marked with a "*" from the seek file, as well as
 *	the corresponding data from the spool file. Also, it there exists a
 *	partially read entry in the seek file (one for which the content of the
 *	<used-length> field is not zero, but the number it contains is smaller
 *	than the one in <message-lenght>), its content is adjusted to show a
 *	message of lenght <message-lenght> - <used-length>, and <used-length> =
 *	0. The spool file is adjusted accordingly.
 *
 * Results:
 *
 *      0 if no error is detected, -1 in the opoosite case. If an error
 *	occured, the content of the seek and spool files might be damaged.
 *
 * Side effects:
 *
 *      Sets variable garbageInSpoolFile (local to this file) to 0.
 *
 *-----------------------------------------------------------------------------
 */

static int
CleanUpFiles (/*void*/)
{
    FILE *fpSeek, *fpSpool;
    char buffer [DP_ARBITRARY_LIMIT];

    /* wtrf = where to read from */
    /* wtwt = where to write to */
    long wtrfSeek, wtwtSeek;
    long wtrfSpool, wtwtSpool;

    if((fpSeek = fopen(seekFile, "r+")) == NULL) {
        return -1;
    }

    if((fpSpool = fopen(spoolFile, "r+")) == NULL) {
        fclose(fpSeek);
        return -1;
    }

    wtrfSeek = 0;
    wtwtSeek = 0;
    wtrfSpool = 0;
    wtwtSpool = 0;

    if(fgets(buffer, DP_ARBITRARY_LIMIT, fpSeek) == NULL) {
        if(!feof(fpSeek)) {
            goto error1;
        }
    }

    wtrfSeek = strlen(buffer);

    /*
     * Treat each entry in the seek file according to the three possible cases:
     *
     * (a) garbage entry (first character is "*"): eliminate it from the seek
     * file and delete the corresponding data from the spool file.
     *
     * (b) partially used entry (<used-length> non-zero and less than
     * <message-lenght>): rewrite the adjusted entry to disk and eliminate the
     * used part of the message from the spool file.
     *
     * (c) normal entries: rewrite the unmodified entry to the disk (possible to
     * a different position because of the entries eliminated before this one),
     * and copy the corresponding data in the spool file (this woould also go in
     * general to a different position because of the already deleted entries).
     */

    while(!feof(fpSeek)) {

        char  bigBuffer [20 * DP_ARBITRARY_LIMIT];
        long  total, used, toBeTransferred, transferred;
        long temp, temp2;
        char *ctemp, *ctemp2;


        /*
         * Ignored garbage entries,ant the messages originating from other
         * senders; don't move the "write to" pointers in the seek and spool
         * file.
         */

        while((!feof(fpSeek)) && (buffer[0] == '*')) {
            strtok(buffer, " ");
            wtrfSpool += atol(strtok(NULL, " "));

            if(fseek(fpSeek, wtrfSeek, 0) == -1) {
                goto error1;
            }

            if(fgets(buffer, DP_ARBITRARY_LIMIT, fpSeek) == NULL) {
                if(!feof(fpSeek)) {
                    goto error1;
                }
            }

            wtrfSeek += strlen(buffer);
        }

        if(feof(fpSeek))
            continue;

        /*
         * The entry is of type (b) or (c). Treat them in the same way, by
         * transferring <message lenght> - <used lenght> (for entries of type
         * (c) <used lenght> = 0) bytes in the spool file.
         */

        strtok(buffer, " ");
        ctemp = strtok(NULL, " ");
        total = atol(ctemp);
        ctemp2 = strtok(NULL, " ");
        used  = atol(ctemp2);


        /*
         * Skip the already used part from the spool file.
         */

        wtrfSpool += used;

        toBeTransferred = total - used;
        transferred = 0;

        while(toBeTransferred > 0) {

            if(fseek(fpSpool, wtrfSpool, 0) == -1) {
                goto error1;
            }

            temp = fread(bigBuffer, sizeof(char),
                         min(toBeTransferred, sizeof(bigBuffer)), fpSpool);

            wtrfSpool += temp;

            if(fseek(fpSpool, wtwtSpool, 0) == -1) {
                goto error1;
            }

            temp2 = fwrite(bigBuffer, sizeof(char), temp, fpSpool);

            wtwtSpool += temp2;

            if((temp == 0) || (temp2 == 0) || (temp != temp2)) {
                goto error1;
            }

            toBeTransferred -= temp;

        }

        /*
         * Create the entry that will be written to the seek file. If the old
         * entry is of type (c), we just recreate it. If it is of type (b), we
         * create an equivalent entry of type (b) for it.
         *
         * Notice how we deal with the problem of '\0' characters introduced by
         * succesive calls of strtok().
         */

        sprintf(ctemp - 1, " %.8ld", total - used);
        sprintf(ctemp2 - 1, " 00000000");
        ctemp2[strlen(ctemp2)] = ' ';

        if(fseek(fpSeek, wtwtSeek, 0) == -1) {
            goto error1;
        }

        if(fputs(buffer, fpSeek) == EOF) {
            goto error1;
        }

        wtwtSeek += strlen(buffer);

        if(fseek(fpSeek, wtrfSeek, 0) == -1) {
            goto error1;
        }

        if(fgets(buffer, DP_ARBITRARY_LIMIT, fpSeek) == NULL) {
            if(!feof(fpSeek)) {
                goto error1;
            }
        }

        wtrfSeek += strlen(buffer);
    }

    fclose(fpSeek);
    fclose(fpSpool);

    truncate(seekFile, wtwtSeek);
    truncate(spoolFile, wtwtSpool);

    if(truncate(seekFile, wtwtSeek) + truncate(spoolFile, wtwtSpool) != 0) {
        return -1;
    }

    garbageInSpoolFile = 0;

    return 0;

error1:

    fclose(fpSeek);
    fclose(fpSpool);
    return -1;

}


/*
 *-----------------------------------------------------------------------------
 *
 * ReadFromSpool --
 *
 *	Reads at most howMany bytes from the first available message sent by
 *	sender. If border is 0, the reading will stop message boundaries,
 *	otherwise it will ignore them.
 *
 * Results:
 *
 *	Return the number of bytes read or -1 if an error is detected.
 *
 * Side effects:
 *
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

static long
ReadFromSpool (sender, where, howMany, border, peek)
    char *sender;	/* where do we want the read message to be from */
    char *where;	/* address of buffer to store the result        */
    long  howMany;	/* maximal number of bytes to be read           */
    int   border;       /* if 0, keep message boundaries, otherwise not */
    int   peek;		/* if 0, really read the data, otherwise not    */
{
    FILE *fpSeek, *fpSpool;

    char buffer [DP_ARBITRARY_LIMIT];

    /* wtrf = where to read from */
    /* wtwt = where to write to */
    long wtrfSeek, wtwtSeek, wtrfSpool, wtwtSpool;
    long transferred, toBeTransferred;

    if((fpSeek = fopen(seekFile, "r+")) == NULL) {
        return -1;
    }

    if((fpSpool = fopen(spoolFile, "r")) == NULL) {
        fclose(fpSeek);
        return -1;
    }

    wtrfSeek = 0;
    wtwtSeek = 0;
    wtrfSpool = 0;

    transferred = 0;
    toBeTransferred = howMany;

    if(fgets(buffer, DP_ARBITRARY_LIMIT, fpSeek) == NULL) {
        if(!feof(fpSeek)) {
            goto error1;
        }
    }

    wtrfSeek = strlen(buffer);
    strtok(buffer, " ");


    /*
     * Treat each entry in the seek file according to the three possible cases:
     *
     * (a) garbage entry (first character is "*"): ignore it.
     *
     * (b) partially used entry (<used-length> non-zero and less than
     * <message-lenght>): read min(howMany, <message-lenght> - <used-lenght>)
     * bytes from the spool file, adjust the entry and rewrite the adjusted
     * entry to disk.
     *
     *
     * (c) normal entries: same as for (b).
     *
     * If the amount of bytes that must be read is greater than that available
     * in the first acceptable seek file entry, and border != 0, process the next
     * acceptable seek file entry, if any.
     */

    while((!feof(fpSeek)) && (toBeTransferred > 0)) {

        char *ctemp, *ctemp2;
        long total, used, temp, transferredNow;

        /*
         * Ignored garbage entries,ant the messages originating from other
         * senders; don't move the "write to" pointers in the seek and spool
         * file.
         */

        while((!feof(fpSeek)) && (strcasecmp(sender, buffer))) {
            wtrfSpool += atol(strtok(NULL, " "));

            if(fgets(buffer, DP_ARBITRARY_LIMIT, fpSeek) == NULL) {
                if(!feof(fpSeek)) {
                    goto error1;
                }
            }

            wtrfSeek += strlen(buffer);
            wtwtSeek += strlen(buffer);
            strtok(buffer, " ");
        }

        if(feof(fpSeek)) {
            continue;
        }

        ctemp = strtok(NULL, " ");
        total = atol(ctemp);
        ctemp2 = strtok(NULL, " ");
        used = atol(ctemp2);
        wtrfSpool += used;
        transferredNow = 0;

        while(transferredNow < min(howMany, min(total-used, toBeTransferred))) {
            if(fseek(fpSpool, wtrfSpool, 0) == -1) {
                goto error1;
            }

            temp = fread(where, sizeof(char),
                     min(howMany, min(total - used, toBeTransferred)), fpSpool);
            where += temp;
            wtrfSpool += temp;

            if(temp < min(howMany, min(total - used, toBeTransferred))) {
                goto error1;
            }
            transferredNow += temp;
        }

        /*
         * Restore buffer. i.e. undo the effects of successive calls to strtok().
         */

        *(ctemp - 1) = ' ';
        *(ctemp + 8) = ' ';
        *(ctemp + 17) = ' ';

        transferred += transferredNow;

        /*
         * Create the entry of type (a) or (d) that will be rewritten to the
         * seek file.
         */


        if(peek == 0) {
            if(transferredNow == total - used) {
                buffer[0] = '*';
            }
            else {
                sprintf(ctemp + 9, "%.8ld", used + transferredNow);
                *(ctemp + 17) = ' ';
            }
        }

        if(fseek(fpSeek, wtwtSeek, 0) == -1) {
            goto error1;
        }

        if(fputs(buffer, fpSeek) == EOF) {
            goto error1;
        }

        wtwtSeek += strlen(buffer);

        if((transferred < howMany) && (border == 0)) {

            /* Do not cross message boundaries. */

            garbageInSpoolFile += transferred;

            fclose(fpSeek);
            fclose(fpSpool);

            return transferred;
        }

        /* Ignore message boundaries. */
        toBeTransferred -= transferredNow;

        if(fseek(fpSeek, wtrfSeek, 0) == -1) {
            goto error1;
        }

        if(fgets(buffer, DP_ARBITRARY_LIMIT, fpSeek) == NULL) {
            if(!feof(fpSeek)) {
                goto error1;
            }
        }

        wtrfSeek += strlen(buffer);
        strtok(buffer, " ");
    }

    if(peek == 0) {
        garbageInSpoolFile += transferred;
    }

    fclose(fpSeek);
    fclose(fpSpool);

    return transferred;

error1:

    fclose(fpSeek);
    fclose(fpSpool);
    return -1;
}



/*
 *-----------------------------------------------------------------------------
 *
 * GFPEmailChannel --
 *
 *	Returns the tcl file descriptor that is associated with the given
 *	channel. In the case of email channels, this descriptor will always be
 *	the NULL pointer, signalling that there is no such file.
 *	For a more complete description of this function please refer
 *	to the description of Tcl_CreateChannel and its associated functions
 *	in the Tcl 7.6 documentation. This function is described under the
 *	entry GETFILEPROC.
 *
 * Results:
 *
 *	Always return NULL.
 *
 * Side effects:
 *
 *	None.
 *
 *-----------------------------------------------------------------------------
 */
	/* ARGSUSED */
#ifdef _TCL76
static Tcl_File
GFPEmailChannel (instanceData, direction)
    ClientData	instanceData;
    int		direction;
{
    return (Tcl_File)NULL;
}
#else
static int
GFPEmailChannel (instanceData, direction, fd)
    ClientData	instanceData;
    int		direction;
    FileHandle	*fd;
{
    *fd = NULL;
    return 0;
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * CRPEmailChannel --
 *
 *	Mask is an ORed combination of TCL_READABLE, TCL_WRITABLE and
 *	TCL_EXCEPTION. If any of the events in the mask happened, the
 *	corresponding bit is maintained, otherwise it is cleared. TCL_EXCEPTION
 *	is not treated for the moment.  An email channel is always writable. If
 *	more than one message was received since the last call to
 *	WCPEmailChannel, they will be collectively signaled as a single
 *	event.
 *	For a more complete description of this function please refer
 *	to the description of Tcl_CreateChannel and its associated functions
 *	in the Tcl 7.6 documentation. This function is described under the entry
 *	CHANNELREADYPROC.
 *
 * Results:
 *
 *	Return the number of bytes read or -1 if an error is detected.
 *
 * Side effects:
 *
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

static int
CRPEmailChannel (instanceData, mask)
    ClientData  instanceData;	/* (in) Pointer to EmailInfo struct. */
    int         mask;		/* (in) ORed combination of TCL_READABLE,
                                 * TCL_WRITABLE and TCL_EXCEPTION. It designates
                                 * the event categories whose occurence has to
                                 * be signalled.
                                 */
{
    if(mask & TCL_EXCEPTION) {
        mask &= ~TCL_EXCEPTION; /* ignored */
    }

    if(mask & TCL_READABLE) {
        if(((EmailInfo *)instanceData)->available <= 0) {
            int tmp;

	    if(PutLock(lockFile) != 0) {
	      return -1;
	    }
            tmp = SetAvailable(instanceData);
            RemoveLock(lockFile);

            if(tmp == -1) {
                /* set errno */
                return -1;
            }
            if(((EmailInfo *)instanceData)->available <= 0) {
                mask &= ~TCL_READABLE;
            }
        }
    }

    return mask;
}




/*
 *-----------------------------------------------------------------------------
 *
 * WCPEmailChannel --
 *
 *	This is the "watch channel" procedure for email channels.
 *
 * Results:
 *
 *	None.
 *
 * Side effects:
 *
 *	Sets the maximum blocking time.
 *
 *-----------------------------------------------------------------------------
 */

	/* ARGSUSED */
static void
WCPEmailChannel (instanceData, mask)
    ClientData instanceData;	/* (in) Pointer to EmailInfo struct. */
    int mask;			/* (in) ORed combination of TCL_READABLE,
                                 * TCL_WRITABLE and TCL_EXCEPTION. It designates
                                 * the event categories that have to be watched.
                                 */
{
    Tcl_Time x;

    x.sec = 0;
    x.usec = 0;
    Tcl_SetMaxBlockTime(&x);
    return;
}



/*
 *-----------------------------------------------------------------------------
 *
 * SetAvailable --
 *
 *	Computes the number of bytes available for a given email channel. It is
 *      used to give a faster answer to calls to the 'channel ready' procedure.
 *
 * Results:
 *
 *	Number of bytes available for reading for a given email channel.
 *
 * Side effects:
 *
 *	Updates the 'available' filed of instance data.
 *
 *-----------------------------------------------------------------------------
 */

static int
SetAvailable (instanceData)
    ClientData instanceData;	/* (in) Pointer to EmailInfo struct. */
{
    FILE *fpSeek;
    char *ctemp, *ctemp2;
    char buffer [DP_ARBITRARY_LIMIT];

    EmailInfo *data = ((EmailInfo *)instanceData);

    if((fpSeek = fopen(seekFile, "r")) == NULL) {
        return -1;
    }

    data->available = 0;

    if(fgets(buffer, DP_ARBITRARY_LIMIT, fpSeek) == NULL) {
        if(!feof(fpSeek)) {
            goto error1;
        }
    }

    while(!feof(fpSeek)) {
        strtok(buffer, " ");
        if(!strcasecmp(buffer, data->address)) {

            char *ctemp, *ctemp2;

            ctemp = strtok(NULL, " ");
            ctemp2 = strtok(NULL, " ");
            data->available += (atol(ctemp) - atol(ctemp2));
        }
        if(fgets(buffer, DP_ARBITRARY_LIMIT, fpSeek) == NULL) {
            if(!feof(fpSeek)) {
                goto error1;
            }
        }
    }

    fclose(fpSeek);

    return 0;

error1:

    fclose(fpSeek);

    return -1;

}



/*
 * OutputEmailChannel --
 *
 *      Sends an email message on the email channel represented by instanceData.
 *	For a more complete description of this function please refer
 *	to the description of Tcl_CreateChannel and its associated functions
 *	in the Tcl 7.6 documentation. This function is described under the entry
 *	OUTPUTPROC.
 *
 * Result:
 *
 *	0 if everything went OK, -1 if an error was detected.
 *
 * Side effects:
 *
 *	None.
 */

static int
OutputEmailChannel (instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;	/* (in) Pointer to EmailInfo struct. */
    CONST84 char *buf;			/* (in) Buffer to write. */
    int   toWrite;		/* (in) Number of bytes to write. */
    int  *errorCodePtr;		/* (out) POSIX error code (if any). */
{
    char *format   = "/usr/lib/sendmail %.*s >/dev/null";
    char *subject  = "Subject: email channel\n";
    char *sequence = "Sequence: %ld\n";
    char *finish   = "\n.\n";

    FILE *pipeFilePtr;
    int   error = 0;
    char  buffer    [DP_ARBITRARY_LIMIT + 1];
    char  seqBuffer [DP_ARBITRARY_LIMIT + 1];

    EmailInfo *data = ((EmailInfo *)instanceData);

    sprintf(buffer, format, sizeof(buffer) - strlen(format), data->address);

    if((pipeFilePtr = popen(buffer, "w")) == NULL) {
        *errorCodePtr = ECHILD;
        return -1;
    }

    error += (fwrite(subject, sizeof(char), strlen(subject), pipeFilePtr)
                                != strlen(subject));

    if(data->sequence != 0) {
        sprintf(seqBuffer, sequence, (data->sequenceNo)++);
        error += (fwrite(sequence, sizeof(char), strlen(sequence), pipeFilePtr)
                                != strlen(sequence));
    }

    error += (fwrite(buf, sizeof(char), toWrite, pipeFilePtr) != toWrite);

    error += (fwrite(finish, sizeof(char), strlen(finish), pipeFilePtr)
                                != strlen(finish));

    error += (pclose(pipeFilePtr) != 0);

    if(error) {
        *errorCodePtr = ECHILD;
        return -1;
    }

    return toWrite;
}



/*
 *-----------------------------------------------------------------------------
 *
 * InputEmailChannel --
 *
 *	This is a wrap function around ReadFromSpool. It provides the interface
 *	required by tcl for reading from the email channel.
 *
 * Results:
 *
 *	Number of bytes of data read from the spool file. If an error
 *	happened, it returns -1.
 *
 * Side effects:
 *
 *	Modifies the files associated with the email channels.
 *
 *-----------------------------------------------------------------------------
 */

static int
InputEmailChannel (instanceData, buf, bufsize, errorCodePtr)
    ClientData	instanceData;	/* (in) Pointer to EmailInfo struct. */
    char       *buf;		/* (in/out) Buffer to fill. */
    int	        bufsize;	/* (in) Size of buffer. */
    int	       *errorCodePtr;	/* (out) POSIX error code (if any). */
{
    int tmp;

    EmailInfo *data = ((EmailInfo *)instanceData);

    if(PutLock(lockFile) != 0) {
          return EAGAIN;
    }

    if(data->available == 0) {
        SetAvailable(instanceData);
    }

    if(data->available == 0) {
        *errorCodePtr = EAGAIN;
	RemoveLock(lockFile);
        return 0;
    }

    tmp = ReadFromSpool(data->address, buf, bufsize, data->border, data->peek);

    if(tmp > 0) {
        data->available -= tmp;
    }

    if(tmp == -1) {
        *errorCodePtr = EINVAL;
    } else if(tmp == 0) {
        *errorCodePtr = EAGAIN;
    }

    if(garbageInSpoolFile > DP_GARBAGE_LIMIT) {
        if(CleanUpFiles() == -1) {
            /*
             * An error happened while trying to eliminate garbage the
             * garbage from the seek and spool files.
             */
            *errorCodePtr = EIO;
            tmp = -1;
        }
    }


    RemoveLock(lockFile);

    return tmp;

}



/*
 *-----------------------------------------------------------------------------
 *
 *  SOPEmailChannel --
 *
 *	This function is called by the Tcl channel driver
 *	whenever Tcl evaluates an fconfigure call to set
 *	some property of the email channel. Currently, only
 *	"-peek" is accepted.
 *
 * Results:
 *	Standard Tcl return value.
 *
 * Side effects:
 *
 *	Depends on the option.
 *
 *-----------------------------------------------------------------------------
 */

static int
SOPEmailChannel (instanceData, interp, optionName, optionValue)
    ClientData	 instanceData;	/* (in) Pointer to EmailInfo struct. */
    Tcl_Interp	*interp;	/* (in) Pointer to tcl interpreter. */
    CONST char	*optionName;
    CONST char	*optionValue;
{
    int option;
    int value;

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
        ((EmailInfo *)instanceData)->peek = value;
        break;

    case DP_SEQUENCE:

        if (Tcl_GetBoolean(interp, optionValue, &value) != TCL_OK) {
            return TCL_ERROR;
        }
        ((EmailInfo *)instanceData)->sequence = value;
        break;

    case DP_ADDRESS:

	Tcl_AppendResult(interp, "can't set address after email channel ",
                         "is opened", NULL);
	return TCL_ERROR;

    case DP_IDENTIFIER:

	Tcl_AppendResult(interp, "can't set identifier after email channel ",
                         "is opened", NULL);
	return TCL_ERROR;

    default:
        Tcl_AppendResult (interp, "illegal option \"", optionName,
                "\" -- must be peek, or a standard fconfigure option", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}



/*
 *-----------------------------------------------------------------------------
 *
 * GOPEmailChannel --
 *
 *	This function is called by the Tcl channel driver to
 *	retrieve parameters that are associated with the email channel.
 *	The only accepted option is "-peek".
 *
 * Results:
 *
 *	A standard Tcl result.
 *
 * Side effects:
 *
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

static int
GOPEmailChannel (instanceData,
#ifndef _TCL76
		interp,
#endif
		optionName, dsPtr)
    ClientData	instanceData;	/* (in) Pointer to EmailInfo struct. */
#ifndef _TCL76
    Tcl_Interp  *interp;
#endif
    CONST84 char	*optionName;
    Tcl_DString *dsPtr;		/* (out) String to store the result in. */
{
    int option;
    int size;
    char str[256];

    /*
     * If optionName is NULL, then store an alternating list of
     * all supported options and their current values in dsPtr.
     */

#if (TCL_MAJOR_VERSION > 7)
#define IGO(a, b, c) GOPEmailChannel(a, interp, b, c)
#else
#define IGO(a, b, c) GOPEmailChannel(a, b, c)
#endif
    if (optionName == NULL) {
     	Tcl_DStringAppend (dsPtr, " -address ", -1);
	IGO (instanceData, "-address", dsPtr);
	Tcl_DStringAppend (dsPtr, " -identifier ", -1);
	IGO (instanceData, "-identifier", dsPtr);
	Tcl_DStringAppend (dsPtr, " -peek ", -1);
	IGO (instanceData, "-peek", dsPtr);
        Tcl_DStringAppend (dsPtr, " -sequence ", -1);
	IGO (instanceData, "-sequence", dsPtr);
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

        if (((EmailInfo *)instanceData)->peek) {
            Tcl_DStringAppend (dsPtr, "1", -1);
        } else {
            Tcl_DStringAppend (dsPtr, "0", -1);
        }
        break;

    case DP_SEQUENCE:

        if (((EmailInfo *)instanceData)->sequence) {
            Tcl_DStringAppend (dsPtr, "1", -1);
        } else {
            Tcl_DStringAppend (dsPtr, "0", -1);
        }
        break;

    case DP_ADDRESS:

        Tcl_DStringAppend (dsPtr, ((EmailInfo *)instanceData)->address, -1);
        break;

    case DP_IDENTIFIER:

        {
            char tmp [DP_ARBITRARY_LIMIT + 1];

            sprintf(tmp, "%d", gIdentifier);

            Tcl_DStringAppend (dsPtr, tmp, -1);
        }
        break;

    default:
        Tcl_SetErrno (EINVAL);
        return TCL_ERROR;
    }

    return TCL_OK;
}






