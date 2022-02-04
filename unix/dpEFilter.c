/*  
 * dpEFilter.c --
 *
 * This auxilliary program will be used to filter the incoming
 * email messages. It uses a list of email sources in $HOME/argv[1]
 * to filter incoming mail. If a message has a From entry matching 
 * one given source, the message (with the header removed will be appended
 * to the corresponding file.
 *
 * The exit codes returned by the program are given below:
 *
 * 0 - everything went OK
 * 1 - number of command line parameters incorrect
 * 2 - lock could not be created or the lock created by somebody else
 *     could not be removed
 * 3 - read/open  error in channel file
 * 4 - read/open error in spool file
 * 5 - read/open error in seek file
 * 6 - empty lock file name
 */

/*
 * Major problems
 *
 * Make sure that the lines in the seek file are shorter than ARBITRARY_LIMIT.
 * This is not a problem at this point, because the subject is always
 * "email channel".
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include "generic/dpInt.h"

/*
 * This auxilliary program will be used to filter the incoming
 * email messages. It uses a list of email sources in $HOME/argv[1]
 * to filter incoming mail. If a message has a From entry matching 
 * one given source, the message (with the header removed will be appended
 * to the corresponding file.
 *
 * The exit codes returned by the program are given below:
 *
 * 0 - everything went OK
 * 1 - number of command line parameters incorrect
 * 2 - lock could not be created or the lock created by somebody else
 *     could not be removed
 * 3 - read/open  error in channel file
 * 4 - read/open error in spool file
 * 5 - read/open error in seek file
 */

#define ARBITRARY_LIMIT 500
#define LOCK_TIMEOUT     10



/* Prototypes of local functions */

static void	Demultiplex	_ANSI_ARGS_((char *channelFile, char *spoolFile,
                                 char *seekFile));
static void	Match		_ANSI_ARGS_((char *pattern));
static void     Exit            _ANSI_ARGS_((int));


/*
 * Call:  filter home-directory lockFile channelFile seekFile spoolFile
 */


char lockFilePath    [ARBITRARY_LIMIT];


int main (argc, argv)
    int   argc;    /* Number of arguments, including the program's name. */
    char *argv[];  /* argv[0] = program's path    */
                   /* argv[1] = root directory    */
                   /* argv[2] = lock file name    */
                   /* argv[3] = channel file name */
                   /* argv[4] = seek file name    */
                   /* argv[5] = spool file name   */
{  
    char spoolFilePath   [ARBITRARY_LIMIT];
    char channelFilePath [ARBITRARY_LIMIT];
    char seekFilePath    [ARBITRARY_LIMIT];
    int  returnValue;


    if(argc != 6)
        Exit(1);

    strcpy(lockFilePath, argv[1]);
    strcat(lockFilePath, argv[2]);

    strcpy(spoolFilePath, argv[1]);
    strcat(spoolFilePath, argv[5]);

    strcpy(channelFilePath, argv[1]);
    strcat(channelFilePath, argv[3]);

    strcpy(seekFilePath, argv[1]);
    strcat(seekFilePath, argv[4]);

    if((returnValue = PutLock(lockFilePath)) != 0) {
      exit(0);
      /*      Exit(returnValue); */
    }

    Demultiplex(spoolFilePath, channelFilePath, seekFilePath);

    if((returnValue = RemoveLock(lockFilePath)) != 0) {
      exit(0);
      /*      Exit(returnValue); */
    }
  
    return 0;
}




/*
 * Demultiplex --
 *
 *	This function scans the incoming message. First, it looks for the
 *	sender; if the sender is one of those authorized, it retrieves the
 *	subject and the body of the message (the body starts after the 
 *	first empty line of the message, the empty line is not included).
 *	For each message that is accepted, one line is added to the seek file,
 *	while the body is appended to the spool file.
 *
 * Results:
 *
 *	None.
 *
 * Side effects:
 *
 *	If the message is accepted, one line is added to the seek file, and
 *	the body of the message is appended to the spool file.
 */

static void
Demultiplex (spoolFile, channelFile, seekFile)
    char *channelFile;  /* Name of channel file. */
    char *spoolFile;    /* Name of spool file.   */
    char *seekFile;     /* Name of seek file.    */
{
    char   stdinBuffer  [ARBITRARY_LIMIT];
    char   buffer2      [ARBITRARY_LIMIT];
    char   chanBuffer   [ARBITRARY_LIMIT];
    FILE  *fpChan, *fpSpool, *fpSeek;
    int    i, found;
    long   count;
    char   ctemp, first;
  

    /*
     * It is likely that these two definitions will have to be changed for
     * different systems. One should put here the strings that immediately
     * preceed the sender and the subject in the message. These string
     * comparisons are done ignoring character case.
     */

    char *p1 = "from ";
    char *p2 = "subject: ";

    /* Check for the sender in the incoming message. */
    Match(p1);

    /* Isolate the sender's name from other data on the same line. */
    fgets(stdinBuffer, ARBITRARY_LIMIT, stdin);
    strtok(stdinBuffer, " ");


    /* Check if the sender appears in the channel file. */
    if((fpChan = fopen(channelFile, "r")) == NULL) {
        fclose(fpChan);
        Exit(3);
    }

    fgets(chanBuffer, ARBITRARY_LIMIT, fpChan);
    chanBuffer[strlen(chanBuffer) - 1] = '\0';
    while(!feof(fpChan)) {
        if(!strcasecmp(chanBuffer, stdinBuffer)) {
            break;
        }
        fgets(chanBuffer, ARBITRARY_LIMIT, fpChan);
        chanBuffer[strlen(chanBuffer) - 1] = '\0';
    }

    if(feof(fpChan)) {
        /* Sender not on list. */
        fclose(fpChan);
        Exit(0);
    }

    fclose(fpChan);

    /* OK, authorized sender. Look for the subject now. */

    Match(p2);

    fgets(buffer2, ARBITRARY_LIMIT, stdin);
    buffer2[strlen(buffer2) - 1] = '\0';

    /* 
     * Look for the first empty line (two succesive '\n' characters)
     * in the message.
     */ 

    found = 0;
    if((ctemp = getchar()) != '\n') {
        for(ctemp = getchar(); !feof(stdin); ctemp = getchar()) {
            if(ctemp == '\n') { 
                if(found == 1) {
                    break;
                }
                else {
                    found = 1;
                }
            }
            else {
                found = 0;
            }
        }
    }
 

    /* Transfer the rest of the message to the spool file. */

    if((fpSpool = fopen(spoolFile, "a")) == NULL) {
        Exit(4);
    }

    count = 0;
    first = getchar();
    if(!feof(stdin)) {
        for(ctemp = getchar(); !feof(stdin); first = ctemp, ctemp = getchar(), count++) {
            if(fputc(first, fpSpool) == EOF) {
                fclose(fpSpool);
                Exit(4);
            }
        }

    };

    fclose(fpSpool);

    /* Add one entry to the seek file. */

    if((fpSeek = fopen(seekFile, "a")) == NULL) {
        Exit(5);
    }

    if(fprintf(fpSeek, "%s %.8ld 00000000 %s\n", chanBuffer, count, buffer2) == EOF) {
        fclose(fpSeek);
        Exit(5);
    }

    fclose(fpSeek);

    return;
}



/*
 * Match --
 *
 *	Returns only if the pattern is found in the standard input. 
 *	We expect the pattern to appear at the beginning of a line.
 *	the comparison is case insensitive. After the pattern is found,
 *	no more characters are read.
 *
 * Results:
 *
 *	None.
 *
 * Side effects:
 *
 *	Reads stdin until the pattern or EOF is found.
 */

static void
Match (pattern)
    char *pattern; /* Pattern to match against. */
{
    int i;

    while(1) {
 
        for(i = 0; (pattern[i] != '\0') && (!feof(stdin)); i++) {
            if(pattern[i] != tolower(getchar()))
                break;
        }

        if(pattern[i] == '\0')
            /* the pattern was found in the standard input */
            return;

        while(!feof(stdin)) {
            if(getchar() == '\n')
                break;
        }

        if(feof(stdin))
            /* premature end of file */
            Exit(4);
    }
}


static void Exit (i)
     int i;
{
  RemoveLock(lockFilePath);
  exit(i);
}





