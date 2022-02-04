.TH ns 1 "February 1, 1995"
.SH NAME
.B ns
- CMT Name Server
.SH SYNOPSIS
ns
.RI ? options ?
.SH OPTIONS
.SS -primary
Designate this name server as the primary server.
.SS -backup
Tells the primary server to start the backup servers given in the nscap file.
.SH DESCRIPTION
The CMT name server provides fast lookup of the various CM processes running 
on a local area network. It also acts as a general purpose name server for 
other applications. In addition to the service lookup functionality, this name
server is capable of auto-starting services previously registerd with the
name server using either
.I rsh 
or 
.IR inetd .
It also provides primitive mechanisms 
for authenticating running services. Another major feature of the name server
is that is is fault tolerant under the "backup" configuration. The user can 
specify a list of name servers to run on different machines with on designated
as the primary server and the rest as back servers. The primary will guarantee
consistency across the backup servers making crash recovery instantaneous. For
more information, please refer to CMT book.
.SH LOW-LEVEL API
The ns provides a library of primitive interface routines. They are included in the file
.IR nslib.tcl .
Any tcl application that uses the name server has to source this file before
calling the interface routines. The API consists of client interface, service
interface and registration interface.

.I Client Interface

ns_FindServices 
.I srvcList
.RS
.IR SrvcList 
is a list of service names and glob patterns. This routine will 
first expand each pattern into a list of service names, and look up the host
name and port number for each service. The return value is a list of 
{srvcName {host port}}. If the service is not running, the {host port} will be
an empty list. If none of the services exists, an empty list is returned. 
.RE

ns_ListServices 
.I pattern
.RS
This routine returns a list of services matching 
.IR pattern ,
which is a glob pattern. An empty list is returned if no service matches
the pattern.
.RE

ns_LaunchServices
.I srvcList
.RS
This routine asks the name server to auto-start services given in
.IR srvcList .
The services have to be pre-registered as on-demand by the name server
administrator. The name server will only launch a service if it is on-demand
and no running. If a client application detects the demise of a running
service, it should call
.IR ns_ServiceState ,
described next, before calling 
.IR ns_LaunchServices .
.I ns_LaunchServices 
have no return value. The caller have to call 
.I ns_ServiceState 
to determine the status of the service. 
.RE

ns_ServiceState
.I srvcList
.RI ? dead ?
.RS
This routine serves two purposes. The first is to query the state of services
listed in 
.IR srvcList .
If the "dead" flag is not specified, 
.I ns_ServiceState
will return the state of each service in the following form: 
{{srvc state} ...}. The possible states are as follow:

.RS
onDemand - not running but auto-startable
.RE

.RS
launching - in the process of starting up but has not informed the name server
of its port number
.RE

.RS
running - alive and kicking
.RE

If the "dead" flag is specified, the routine is effectively telling the name server that the services specified in
.I srvcList
have ceased to exist. The name server will then verify the information and erase the information regarding the service. The return value is the new state of the service. 
.RE

ns_AuthenticateService
.I srvcName
.RS
This routine asks the name server to verify the authenticity of a service. At
start-up of a service, the name server generates a pair of random tickets and
pass them to the service. One of the ticket is the ticket for authenticating
the service. The other is for the name server to authenticate itself to the 
service. What this routine does is to have the name server check the
authenticity of a service and generate another pair of tickets to be
used between the client and the service. How a client verify the authenticity
of a service at connection time is described later. The return value is
a pair of random numbers or an empty list if the service is not authentic.
.RE

dp_RPC
.I srvcHandle
.I ns_Authenticate
.I ticket
.RS
Once the client gets the tickets by calling
.IR ns_AuthenticateService ,
it can connect to the service and then do a remote procedure call on 
.IR ns_Authenticate . Ticket
is the first of the the ticket pair. The return value should be the
second ticket of the pair. If not, the service is not legitmate.
.RE

.I Service Interface

ns_AdvertiseService
.I srvcName hostname port
.RI ? passwd ?
.RS 
This routine is called by a service to inform the name server of its 
port number. 
.I SrvcName 
can be determined from the command line after the -service flag. 
.I Hostname
is the name of the machine on which the service is running. There are
a couple of auxillary routines for accessing these parameters to be
describe below.
.I Port
is the port number on which the service is running. 
.I Passwd
is the second of the ticket pair mentioned above. The ticket pair
should be available on the stdin. Therefore, it is imperative for a
service to scan in the tickets at the start-up.
.RE

ns_UnadvertiseService
.I srvcName
.RI ? passed ?
.RS
This routine is the opposite of
.IR ns_AdvertiseService.
It should be called by the service when it terminates.
.RE

ns_RecordTicket 
.I tickets
.RS
This routine records the tickets in a table maintained internally. For
a auto-started service, tickets can be read from its stdin. For
example: ns_RecordTicket [gets stdin].
.RE

.I Registration Interface

The following routines are for pre-registering services to the name server.
They are currently used by the Service Monitor (nsi) for registerign and
monitoring services.

ns_Register addService
.I srvcName host path
.RI ? args ?
.RS
This routine registers a service. 
.I Path 
is a the command path name to be executed at auto-start. 
.I Args
are the arguments to the command.
.RE

ns_Register deleteService 
.I srvcName
.RS
This routine unregisters a service from the name server.
.RE

ns_Register modifyService 
.I srvcName newHost newPath newArgs
.RS
This routine is used to replace the registration information of an 
existing service.
.RE

ns_Register aliasService 
.I srvcAlias srvcName
.RS
This routine creates an alias for an existing service.
.RE

ns_Register info
.I srvcName
.RS
This routine returns the registration information of the service:
{srvcName, aliasOf, host, command, args}. 
.I AliasOf 
indicates what service this service is alias of.
.RE

HIGH-LEVEL API
The high-level API is a set of high-level routines built on top of the
low-level API. All high-level routines start with NS_ instead of ns_ as
in the low-level case. Applications are, by no means, restricted to using
this API. It is designed for ease of use. Also, all routines accept only 
one service name instead of a pattern. 

.I Client Interface

NS_SetHostPort
.I host port
.RS
This routine is used to set the default host and port of a service, so
the application does not have to go through the name server.
.RE

NS_UnsetHostPort
.I host port
.RS
This routine unset the default host and port, so the application does
go through the name server. 
.RE

NS_FindService 
.I srvcName
.RS
This routine return {host port} of srvcName. It is used internally by 
.IR NS_GetServiceConn .
.RE

NS_LaunchService
.I srvcName
.RS
This routine requests the name server to launch a service. It is used 
internally by 
.IR NS_GetServiceConn .
.RE

NS_GetServicConn 
.IR srvcName ? authenticate ?
.RS
This routine returns a connection to a service in the form of a file handle. It
will return an error if the service does not exist or connection is timed out.

.RE

NS_BadConnection
.I srvcName
.RS
This routine informs the name server that the connection to the service has
gone bad.
.RE

.I Service Interface

NS_SrvcInit 
.I srvcName host port
.RS
This routine should be called by a service at start-up time to initialize 
itself. 
.I SrvcName is the name of the service. It can be determined by calling
.IR NS_GetSrvcName .
.I Host
is the internet address of the host machine on which the service is running. 
It can be determined by calling
.IR NS_GetHostName .
.I Port 
is the port number of the service and it is the return value of 
.I dp_MakeRPCServer.
.RE

NS_SrvcExit
.RS
This routine should be called by a service before it exits.
.RE

NS_GetSrvcName
.RS
This routine returns the service name of the current process.
.RE

.I NS_GetHostName
.RS
This routine returns the name of the host machine by calling /bin/hostname.
.RE

.SH FILES
ns.tcl -- contains the script for the name server.

nslib.tcl -- contains all the interface routines to the name server.

nsi.tcl -- script for the Service Monitor. To run it, type "nswish -f nsi.tcl &".

nswish&nstcl -- customized name server tcl shell. Nstcl is a shell without
tk support. Also, it will deamonize itself. Nswish is a shell with tk support.
However, tk can be turned off by specifying -notk on the command line.

nscap -- contains name-server specific information. It sets the following variable:
.RS
.I nameServers 
is a list of {host command port stateFile logFile}. Host is the machine
on which the name server should run.
.I Command
is the command for starting up the name server.
.I Port 
is the port number on which the name server should run. 
.RE

.RS
.I StateFile 
is the file path for saving the state of the name server.
.RE

.RS
.I LogFile 
is the file name for saving name server logs.
.RE

nsConfigFile -- holds the path of the name server initialization file. This 
file is not source if the state file already exists.

launchPort -- holds the port number for launching a service through
.IR inetd .
.RE

.SH C INTERFACE
The following is a description of the C programming interface to the name 
server. It is basically a C wrapper to the low-level Tcl interface 
procedures. 

.I NSResult Structure

This structure is used by all C routines for passing the results from
the name server to the caller. It contains the following fields:
.RS
char *srvcName -- name of the service
.RE

.RS
char *host -- internet address of the host machine
.RE

.RS
int port -- port number
.RE

.RS
NSState state -- state of the service. They can be any of the following
enumerated type: ONDEMAND, LAUNCHING, RUNNING, UNKNOWN.
.RE

.I C Routines

void ns_Init(void)
.RS
This routine initializes the name server connection. 
.RE

void ns_SrvcInit(char *srvc, char *host, int port)
.RS
This routine should be called by the service to register ifself to the
name server. 
.I Srvc 
is the name of the service.
.I Host 
is the host address of the service.
.I Port 
is the port number the service is running on.
.RE

void ns_SrvcExit(char *srvc)
.RS
This routine should be called by the service to unregister itself from
the name server.
.RE

void ns_AdvertiseService(char *srvcName, char *host, int port)
void ns_UnadvertiseService(char *srvcName)
.RS
Advertise and unadvertise a service.
.RE

In the following routines, the parameter 
.I argv 
is an array of service names or pattern (/foobar or /*).
.I Argc 
is the number of elements in the argv.
.I ResultPtr 
is a pointer to an array of pointers to NSResult.
The return value is the number of elements in the resultPtr array.

int ns_FindServices (int argc, char *argv[], NSResult ***resultPtr)
.RS
.IR SrvcName , 
.I host
and 
.I port 
fields in the NSResult structure are set.
.RE


NSResult *ns_FindService(char *srvcName)
.RS
Single-name version
.RE

int ns_ListServices(int argc, char *argv[], NSResult ***resultPtr)
.RS
.I SrvcName
field of NSResult is set.
.RE

int ns_ServicesState(int argc, char *argv[], NSResult ***resultPtr)
.RS
.I SrvcName 
and 
.I state
fields are set.
.RE

NSResult *ns_ServiceState(char *srvcName)
.RS
Single-name version
.RE

void ns_LaunchService(char *srvcName)
.RS
Launch the specified service.
.RE

void ns_FreeResults(int count, NSResult **resultPtr)
.RS
Free an array of pointers to NSResult.
.RE

void ns_FreeResult(NSResult *result)
.RS
Free a NSResult.
.RE


.SH KEYWORDS
cm, ns
.SH AUTHORS
Peter Liu (pliu@cs.berkeley.edu)


