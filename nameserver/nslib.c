#include "nslib.h"

Tcl_Interp *interp;

Tcl_AppInit(Tcl_Interp *interp)
{
    if (Tcl_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
    if (Tdp_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
    if (Tns_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

void
ns_Init(void)
{
    char *nslibFile;
    char portString[10];
    
    interp = Tcl_CreateInterp();

    /* Initialize tcl */
    if (Tcl_AppInit(interp) != TCL_OK) {
	fprintf(stderr, "Tcl_AppInit failed: %s\n", interp->result);
	exit (1);
    }

    /*
     * Set the "tcl_interactive" variable.
     */
    Tcl_SetVar(interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);

    /*
     * Set the "isDaemon" variable.
     */
    Tcl_SetVar(interp, "isDaemon", "0", TCL_GLOBAL_ONLY);
}


void ns_SrvcInit(char *srvc, char *host, int port) 
{
    char portString[10];
    
    sprintf(portString, "%d", port);
    if (Tcl_VarEval(interp, "NS_SrvcInit ", srvc, " ",  host, " ",
			portString, NULL) != TCL_OK) {
	fprintf(stderr, "ns_Init failed: %s\n", interp->result);
	exit (1);
    }
}



void ns_SrvcExit(char *srvc) 
{
    if (Tcl_VarEval(interp, "NS_SrvcExit ", srvc, NULL) != TCL_OK) {
	fprintf(stderr, "NS_SrvcExit failed: %s\n", interp->result);
	exit (1);
    }
}


int
ns_FindServices (int argc, char *argv[], NSResult ***resultPtr)
{
    char *script;
    char **argvPtr;
    int argcPtr, i;

    script = Tcl_Merge(argc, argv);
    if (Tcl_VarEval(interp, "ns_FindServices ", script, NULL) == TCL_ERROR) {
	fprintf(stderr, "ns_FindServices failed: %s\n", interp->result);
	return -1;
    }
    free(script);
    
    if (Tcl_SplitList(interp, interp->result, &argcPtr, &argvPtr) != TCL_OK) {
	return -1;
    } else {
	if (argcPtr == 0) return 0;  /* No services found */
	*resultPtr = (NSResult **) malloc(argcPtr * sizeof(NSResult *));
	for (i = 0; i < argcPtr; i++) {
	    char **avPtr;
	    int acPtr;
	    NSResult *rp;

	    Tcl_SplitList(interp, argvPtr[i], &acPtr, &avPtr);
	    rp = (NSResult *) malloc(sizeof(NSResult));
	    (*resultPtr)[i] = rp;
	    rp->srvcName = avPtr[0];
	    if (avPtr[1] == NULL) {
		rp->host = NULL;
		rp->port = -1;
	    } else {
		char **ap;
		int ac;
		Tcl_SplitList(interp, avPtr[1], &ac, &ap);
		rp->host = ap[0];
		rp->port = atoi(ap[1]);
	    }
	}
	return argcPtr;
    }
}


NSResult *
ns_FindService(char *srvcName) 
{
    char *argv[1];
    NSResult **resultPtr;

    argv[1] = srvcName;
    if (ns_FindServices(1, argv, &resultPtr)) {
	return resultPtr[1];
    } else {
	return NULL;
    }

}

int
ns_ListServices (int argc, char *argv[], NSResult ***resultPtr)
{
    char *script;
    char **argvPtr;
    int argcPtr, i;
    NSResult *rp;

    script = Tcl_Merge(argc, argv);
    if (Tcl_VarEval(interp, "ns_ListServices ", script, NULL) == TCL_ERROR) {
	fprintf(stderr, "ns_ListServices failed: %s\n", interp->result);
	return -1;
    }
    free(script);
    
    if (Tcl_SplitList(interp, interp->result, &argcPtr, &argvPtr) != TCL_OK) {
	return -1;
    } else {
	if (argcPtr == 0) return 0;  /* No services found */
	*resultPtr = (NSResult **) malloc(argcPtr * sizeof(NSResult *));
	for (i = 0; i < argcPtr; i++) {
	    rp = (NSResult *) malloc(sizeof(NSResult));
	    (*resultPtr)[i] = rp;
	    rp->srvcName = argvPtr[i];
	}
	return argcPtr;
    }	
}


void
ns_LaunchServices (int argc, char *argv[])
{
    char *script;
    char **argvPtr;
    int argcPtr, i;

    script = Tcl_Merge(argc, argv);
    if (Tcl_VarEval(interp, "ns_LaunchServices ", script, NULL) == TCL_ERROR) {
	fprintf(stderr, "ns_LaunchServices failed: %s\n", interp->result);
	return ;
    }
    free(script);
}

void
ns_LaunchService(char *srvcName) 
{
    char *argv[1];
    
    ns_LaunchServices(1, argv);
}


int
ns_ServicesState (int argc, char *argv[], NSResult ***resultPtr)
{
    char *script;
    char **argvPtr;
    int argcPtr, i;	

    script = Tcl_Merge(argc-1, argv);
    if (Tcl_VarEval(interp, "ns_ServiceState ", 
			    script, argv[argc-1], NULL) == TCL_ERROR) {
	fprintf(stderr, "ns_ServiceState failed: %s\n", interp->result);
	return -1;
    }
    free(script);

    if (Tcl_SplitList(interp, interp->result, &argcPtr, &argvPtr) != TCL_OK) {
	return -1;
    } else {
	if (argcPtr == 0) return 0;  /* No services found */
	*resultPtr = (NSResult **) malloc(argcPtr * sizeof(NSResult *));
	for (i = 0; i < argcPtr; i++) {
	    NSResult *rp;
	    char **avPtr;
	    int acPtr;

	    rp = (NSResult *) malloc(sizeof(NSResult));
	    (*resultPtr)[i] = rp;
	    Tcl_SplitList(interp, argvPtr[i], &acPtr, &avPtr);
	    rp->srvcName = avPtr[0];

	    if (strcmp(avPtr[1], "onDemand") == 0) {
		rp->state = ONDEMAND;
	    } else if (strcmp(avPtr[1], "launcing") == 0) {
		rp->state = LAUNCHING;
	    } else if (strcmp(avPtr[1], "running") == 0) {
		rp->state = RUNNING;
	    } else {
		rp->state = UNKNOWN;
	    }				  
	}
	return argcPtr;
    }
}

NSResult *
ns_ServiceState(char *srvcName)
{
    char *argv[1];
    NSResult **resultPtr;

    argv[1] = srvcName;
    if (ns_ServicesState(1, argv, &resultPtr)) {
	return resultPtr[1];
    } else {
	return NULL;
    }
}

int
ns_AdvertiseService(char *srvcName, char *host, int port)
{
    char *script;
    char **argvPtr;
    int argcPtr, i;	
    char portString[10];

    sprintf(portString, "%d", port);
    if (Tcl_VarEval(interp, "ns_AdvertiseService ", srvcName," ", host, " ", 
				    portString, NULL) != TCL_OK) {
	fprintf(stderr, "ns_ServiceState failed: %s\n", interp->result);
	return -1;
    }
    free(script);	
}


int
ns_UnadvertiseService(char *srvcName)
{
    char *script;
    char **argvPtr;
    int argcPtr, i;	

    if (Tcl_VarEval(interp, "ns_UnadvertiseService ", srvcName, NULL)) {
	fprintf(stderr, "ns_ServiceState failed: %s\n", interp->result);
	return -1;
    }
    free(script);	
}

void
ns_FreeResults(int count, NSResult **resultPtr)
{
    int i;

    for (i = 0 ; i < count; i++) {
	ns_FreeResult(resultPtr[i]);
    }

    free (resultPtr);
}

void
ns_FreeResult(NSResult *result)
{
    free(result->srvcName);
    if (result->host) free(result->host);
    if (result->state) free(result->state);
    free(result);
}

