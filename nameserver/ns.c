/* tk */
#include <tcl.h>
#include <tk.h>
#include <stdio.h>
#ifdef NO_STDLIB_H
#	include "compat/stdlib.h"
#else
#	include <stdlib.h>
#endif
#include <signal.h>
#include <sys/param.h>
#include <sys/types.h>
#ifndef NO_SYS_TIME_H
#   include <sys/time.h>
#else
#   include <time.h>
#endif
#ifdef NO_ERRNO_H
#       include "compat/errno.h"
#else
#       include <errno.h>
#endif
extern int errno;

#ifdef SIGTSTP			/* True if BSD system */
# include <sys/file.h>
# include <sys/ioctl.h>
#endif

/*----------------------------------------------------------------------
 * Function: Tns_dupCmd
 * Purpose:
 *     Implements the dup Tcl command. It returns TCL_ERROR on a failed
 *     dup, TCL_OK on a correct dup.
 *----------------------------------------------------------------------*/
int
Tns_dupCmd(ClientData clientData, Tcl_Interp *interp, int argc, char **argv)
                          	/* Main window associated with
				 * interpreter. */
                       		/* Current interpreter. */
             			/* Number of arguments. */
                		/* Argument strings. */
{
    int i;
    int fd[2] = {0, 0};		/* Initial value needed only to stop compiler
				 * warnings. */

    if (argc != 3) {
	Tcl_AppendResult (interp, "wrong # args: should be \"",
			  argv[0], "fileid filed",
			  (char *) NULL);
	return TCL_ERROR;
    }

    for (i = 1; i <= 2; i++) {
	if ((argv[i][0] == 'f') && (argv[i][1] == 'i') && (argv[i][2] == 'l')
	    & (argv[i][3] == 'e')) {
	    char *end;

	    fd[i-1] = strtoul(argv[i]+4, &end, 10);
	    if ((end == argv[i]+4) || (*end != 0)) {
		goto badId;
	    }
	} else if ((argv[i][0] == 's') && (argv[i][1] == 't')
		   && (argv[i][2] == 'd')) {
	    if (strcmp(argv[i]+3, "in") == 0) {
		fd[i-1] = 0;
	    } else if (strcmp(argv[i]+3, "out") == 0) {
		fd[i-1] = 1;
	    } else if (strcmp(argv[i]+3, "err") == 0) {
		fd[i-1] = 2;
	    } else {
		goto badId;
	    }
	} else {
	badId:
	    Tcl_AppendResult(interp, "bad file identifier \"", argv[i],
			     "\"", (char *) NULL);
	    return TCL_ERROR;
	}
    }

    if (dup2(fd[0], fd[1]) == -1) {
	Tcl_AppendResult(interp, "couldn't dup \"", argv[1], " onto ", argv[2],
			 "\": ", Tcl_PosixError(interp), (char *) NULL);
	return TCL_ERROR;
    }

    return TCL_OK;
}

/*----------------------------------------------------------------------
 * Function: Tns_forkCmd
 * Purpose:
 *     Implements the fork Tcl command. It returns TCL_ERROR on a failed
 *     fork, TCL_OK on a correct fork.  For the return value in TCL, 0
 *     is returned if we are in the child, the process # if we are in
 *     the parent.
 * Tcl Command Args:
 *     arg1: process group name
 *----------------------------------------------------------------------*/
int
Tns_forkCmd(ClientData clientData, Tcl_Interp *interp, int argc, char **argv)
                          	/* Main window associated with
				 * interpreter. */
                       		/* Current interpreter. */
             			/* Number of arguments. */
                		/* Argument strings. */
{
    int pid;

    if (argc != 1) {
	Tcl_AppendResult (interp, "wrong # args: should be \"",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    }

    pid = fork();
    if (pid == -1) {
	Tcl_AppendResult (interp, argv[0], ": Unable to fork process",
			  (char *) NULL);
	return TCL_ERROR;
    }

#if 0
    if (pid == 0) {
	/* Close the stdin */
	close(1);
    }
#endif

    Tcl_ResetResult(interp);
    sprintf(interp->result, "%d", pid);
    return TCL_OK;
}

/*----------------------------------------------------------------------
 * Function: Tns_daemonizeCmd
 * Purpose:
 *     Implements the daemonize Tcl command.  This detaches a daemon
 *     process from login session context.  It forks
 *----------------------------------------------------------------------*/
int
Tns_daemonizeCmd(ClientData clientData, Tcl_Interp *interp,
		 int argc, char **argv)
                          	/* Main window associated with
				 * interpreter. */
                       		/* Current interpreter. */
             			/* Number of arguments. */
                		/* Argument strings. */
{
    register int childpid, fd;
    register int i;

    if (argc != 1) {
	Tcl_AppendResult (interp, "wrong # args: should be \"",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    }

    if (getppid() == 1) {
	fprintf(stderr, "I think my parent is inetd");
	goto out;
    }

    /*
     * Ignore the terminal stop signals (BSD)
     */
#ifdef SIGTTOU
    signal (SIGTTOU, SIG_IGN);
#endif
#ifdef SIGTTIN
    signal (SIGTTIN, SIG_IGN);
#endif
#ifdef SIGTSTP
    signal (SIGTSTP, SIG_IGN);
#endif

    /*
     * If we were not started in the background, fork and let the parent
     * exit.  This also guarantees the first child is not a process
     * group leader
     */

    if ( (childpid = fork()) < 0) {
	Tcl_AppendResult (interp, "%s: Unable to fork process",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    } else if (childpid > 0) {
	exit (0);		/* Parent process exits */
    }

    /* First child process.
     *
     * Disassociate from controlling terminal and process group.
     * Ensure the process can't reacquire a new controlling terminal.
     */

#ifdef SIGTSTP			/* BSD */

/*  setsid(); */

    if (setpgrp(0, getpid()) == -1) {
	Tcl_AppendResult (interp, "%s: Can't change process group",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    }

    if ( (fd = open("/dev/tty", O_RDWR)) >= 0) {
	ioctl(fd, TIOCNOTTY, (char *) NULL); /* Lose controlling tty */
	close(fd);
    }

#else  /* System V */
    if (setpgrp() == -1) {
	Tcl_AppendResult (interp, "%s: Can't change process group",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    }

    signal (SIGHUP, SIG_IGN);
    if ( (childpid = fork()) < 0) {
	Tcl_AppendResult (interp, "%s: Unable to fork process",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    } else if (childpid > 0) {
	exit (0);		/* Exit first child */
    }
#endif

 out:

    /* This is where all file descriptors are normally closed.  We will
     * leave them open except for 0,1,2 */
    
    for (i = 0; i < 1024; i++) {
	close (i);
    }
    errno = 0;

    umask(0);

    /*
     * Attach the file descriptors 0, 1, and 2 to /dev/null.
     * Applications expect them to at least be open.
     */
    if ((fd = open("/dev/null", O_RDWR)) != 0) {
	Tcl_AppendResult (interp, "%s: Unable to open /dev/null",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    }
    if ((fd = dup(0)) != 1) {
	Tcl_AppendResult (interp, "%s: Unable to dup() onto stdout",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    }
    if ((fd = dup(0)) != 2) {
	Tcl_AppendResult (interp, "%s: Unable to dup() onto stderr",
			  argv[0], 
			  (char *) NULL);
	return TCL_ERROR;
    }

    /* Clear the inherited file creation mask */
    return TCL_OK;
}


int Tns_SystimeCmd (clientData, interp, argc, argv)
    ClientData clientData;         /* Often ignored */
    Tcl_Interp *interp;             /* tcl interpreter */
    int argc;                       /* Number of arguments */
    char **argv;                   /* Arg list */
{
    struct timeval tv;

    if (argc != 1) {
        Tcl_AppendResult(interp, argv[0], " doesn't take any args", NULL);
        return TCL_ERROR;
    }

    (void) gettimeofday(&tv, NULL);
    sprintf (interp->result, "%d.%06d", tv.tv_sec, tv.tv_usec);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Tns_Init --
 *
 *	Initialize the full Tcl-ISIS package.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	ISIS related commands are bound to the interpreter.
 *
 *--------------------------------------------------------------
 */
int
Tns_Init (Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "fork", Tns_forkCmd,
		      (ClientData) NULL, (void (*)()) NULL);
    Tcl_CreateCommand(interp, "daemonize", Tns_daemonizeCmd,
		      (ClientData) NULL, (void (*)()) NULL);
    Tcl_CreateCommand(interp, "dup", Tns_dupCmd,
		      (ClientData) NULL, (void (*)()) NULL);
    Tcl_CreateCommand(interp, "systime", Tns_SystimeCmd,
		      (ClientData) NULL, (void (*)()) NULL);
    return TCL_OK;
}

