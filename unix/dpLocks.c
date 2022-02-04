#include <stdio.h>
#include <errno.h>

/*
 * If the lock is set by another process, PutLock will make an attempt to remove
 * it every SLEEP_TIME seconds.
 */

#define SLEEP_TIME  1
                   


/*
 * PutLock --
 *
 *	Creates a symbolic link on the file given in lockFilePath. The name of
 *	the file the link points is a string representing the process number of
 *	the filter process. If the link already exists, the program checks if
 *	the process whose number is given in the symbolic link still exists. If
 *	yes, the program waits for one second and then checks again for the
 *	status of the lock. If no, the existing lock is removed and the new one
 *	is put in its place.
 *
 * Results:
 *
 *	The return value is 2 if the operation failed, otherwise it is 0.
 *
 * Side effects:
 *
 *	Puts a lock (symbolic link) on the given file.
 *
 */


int 
PutLock (lockFilePath)
    char *lockFilePath; /* File to put lock on. */
{
    char  pid    [15];

    sprintf(pid, "%ld", (long)getpid());

    /*
     * Waiting for lock to be removed or for timeout.
     * If other error than "already existing link" occurs, exit.
     */

    while(symlink(pid, lockFilePath) == -1) {
      sleep(1);
    }
   
    return 0;
}




/*
 * RemoveLock --
 *
 *	Remove the symbolic link from the file given in lockFilePath. The link
 *	is removed only if it was put by this process (this is checked by
 *	comparing the value of the symbolink link with the ptrocess id). 
 *
 * Results:
 *
 *	The return value is 2 if the operation failed, otherwise it is 0.
 *
 * Side effects:
 *
 *	None.
 */

int
RemoveLock (lockFilePath)
    char *lockFilePath; /* File to remove lock from. */
{
    if(unlink(lockFilePath) != 0) {
        FILE *fp = fopen("ERROR", "a");
        fprintf(fp,     "ERROR: Failed to remove lock file '%s' (errno = %d).\n", lockFilePath, errno);
        fprintf(stderr, "ERROR: Failed to remove lock file '%s' (errno = %d).\n", lockFilePath, errno);
        fclose(fp);
        return 2;
    }

    return 0;
}





