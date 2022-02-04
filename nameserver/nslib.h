#ifndef NSLIB_H
#define NSLIB_H

#include <stdio.h>
#include <tcl.h>

/* 
   service state:
           ONDEMAND: the service is launchable by the nameserver
           LAUNCHING: the service is currently being launched
           RUNNING: the service is now running
           UNKNOWN: should never get into this state
*/
typedef enum {ONDEMAND, LAUNCHING, RUNNING, UNKNOWN} NSState;

typedef struct nsResult {
	char *srvcName;      /* name of the service */
	char *host;          /* internet address */
	NSState state;       /* state of the service: running, onDemand, or launching */
	int port;            /* port number */
} NSResult;


/* Initialized the nameserver connection */
void ns_Init(void);

/* Called by the service to register itself to the nameserver */
void ns_SrvcInit(char* srvc, char* host, int port);

/* Called by the service to unregister itself from the nameserver */
void ns_SrvcExit(char* srvc);


/*
   Argv is an array of srvc names or patterns (/foobar or /*). 
   Argc is the number of elements in argv.
   ResultPtr is a pointer to an array of pointers to NSResult.
   Return value is the number of elements in the resultPtr array.
*/

/* srvcName, port and host are set */
int ns_FindServices(int argc, char *argv[], NSResult ***resultPtr);

/* single-name version */
NSResult *ns_FindService(char *srvcName);

/* srvcName is set */
int ns_ListServices(int argc, char *argv[], NSResult ***resultPtr);

/* srvcName and state are set */
int ns_ServicesState(int argc, char *argv[], NSResult ***resultPtr);

/* single-name version */
NSResult *ns_ServiceState(char *srvcName);

void ns_LaunchServices(int argc, char *argv[]);

/* single-name version */
void ns_LaunchService(char *srvcName);

int ns_AdvertiseService(char *srvcName, char *host, int port);
int ns_UnadvertiseService(char *srvcName);

void ns_FreeResults(int count, NSResult **resultPtr);
void ns_FreeResult(NSResult *result);

#endif
