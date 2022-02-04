.TH cmt New "August 15,1994"
.SH NAME
.B cmt
- Create a new cmt object.
.SH SYNOPSIS
cmt
.I parentName 
.RI ? options ?
.SH OPTIONS
.SS -rpc_file
A read only option that returns the file descriptor of the
RPC connection to the open cm service or process. If no service
or process is connected to, rpc_file is set to "".
.SS -CID
A read only option that returns the CID (Connection ID) given
to the connection by the open cm service or process. If no
service or process is currently connected to, CID is set to -1.
.SS -subobjects
A read only option that returns a list of objects created remotely
through this cmt.
.SS -nameserver
The hostname of the machine to use as a name server
when starting services.
.SS -nameserver_port
The port number of the nameserver process.
.SS -local_close_command
The command to be called when a connection to a service
or process is closed.
.SS -remote_close_command
The command executed remotely is a connection to a service
or process is closed.
.SS -local_lost_connection_command
The command executed locally if the connection to a service
or process is unexpectedly lost. This command does not get
executed if the connection was properly closed.
.SS -remote_lost_connection_command
The command executed remotely if the connection to a service
or process is unexpectedly lost. This command does not get
executed if the connection was properly closed.
.SH DESCRIPTION
The cmt command creates a new cmt object which can be used to
connect to cm services and processes running on other machines.
Once a connection to a service or process is established, the cmt
object can be used to create and control other cm objects (including
cmt's) in the remote process. Using cmt objects to create and link
togethor other cm objects is the primary method of building applications
with the cm toolkit.
.SH WIDGET COMMANDS
The cmt command creates a new command 
.IR objectName .
.I ObjectName
is generated by concatenating the value of
.I parent
and a unique string which is generated on the fly.
The empty string is a valid value for
.IR parent .
This new command, 
.IR objectName ,
is used to invoke various operations on the widget.

For more information on object names and why they are generated
by the cm process and not set by the user, please read the object
management overview for the cm toolkit.

The command
.I objectName
has the following general form:

.RS
.I
objectName option ?arg arg arg...?
.RE

.I Option
and the
.I args
determine the exact behavior of the command.
The following commands are possible:

.I objectName
configure 
.I option ?value option value ...?
.RS
Query or modify the configuration options described above.
Attempting to modify a read only option will produce an error.
.RE

.I objectName
open -host 
.I hostname port_number
.br
.I objectName
open -service
.I service_name
.RS 
Opens a connection to a cm process or service. The remote
process can be specified in one of two ways. By using the -host
method, the hostname and portnumber of the process is explicitly
specified. The -service method attempts to contact the nameserver
and have it establish the connection to the specified service.
.RE

.I objectName
getCID
.RS
Returns the CID. This is just a short cut command for querying the
read only CID configuration option described above.
.RE

.I objectName
close
.RS
Closes the connection if open. If not open, it does nothing.
.RE

.I objectName
info
.I keyword(s)
.RS
The info command returns information about the process or service to which
currently connected. If not connected to a service or process, this command
will generate an error. The
.I keyword
acts as an index to what information you want. The information resides in
the remote service or process. If
.I keyword 
is not recognized or if no 
information was registered under
.IR keyword , 
the command returns empty
string. The following is the list of valid keywords:

.RS
service
.RS
Returns the name of the service.
.RE

version
.RS
Returns the version number of the service.
.RE

media
.RS
Returns a list of supported media types. An example
of return values might be: {video audio subtext}.
.RE

formats
.I media
.RS
Given the media type
.IR media ,
returns a list of supported formats. For example, if 
.I media
was "video", the return value might be: {mpeg, ucb_mpeg_clip, mjpeg, parrallax_mjpeg}
.RE

objects
.I media format
.RS
Given the media type
.I media
and the format type
.IR format ,
returns a list of all object types in the remote service or process 
that can deal with this type of data.
.RE

object_info 
.I object_type
.RS
Returns any information registered with this object type.
.RE
.RE
.RE

.I objectName
rpc ?-events
.IR events ?
?-timeout 
.IR milliseconds ?
?-timeoutReturn 
.IR callback ?
.I command ?args?
.RS
Sends 
.I command
and 
.I args
to the remote cm service or process for execution. The results are
returned. The -events, -timeout, and -timeoutReturn options work exactly
as documented in the Tcl_DP package.
.RE

.I objectName
rdo ?-callback
.IR callback ?
?-onerror 
.IR callback ?
.I command ?args?
.RS
Sends
.I command
and
.I args
to the remote process for execution but does not wait for a return
value. The -callback and -onerror options work as documented in the
Tcl_DP package.
.RE

.I objectName
create_object
.I object_type
.RS
This command creates a new object in the remote process of type
.IR object_type .
The remote object's name is returned. The remote object's name is also
bound to the procedure of the same name to invoke and and manipulate 
the remote object. This procedure is simply a local wrapper that
passes any arguments to the remote object through the cmt connection.
A few commands and configuration options for the remote object, however,
are dealt with by this local procedure wrapper and not by the remote object
itself. This ensures that all cm objects will have these configuration
options and commands in common. The common configuration options are
are set through the cmt object's "configure_object" command described 
below. The common commands make up a general tagging facility for cm 
objects. These commands are described next. For more information, 
please refer to the
CM Toolkit Object Management Overview. 

.RS
Common Commands

.I remoteObjectName
addtag
.I tag
.RS
Adds the tag, 
.IR tag ,
to
.I remoteObjectName's
tag list.
.RE

.I remoteObjectName
gettags
.RS
Returns
.I remoteObjectName's
tag list.
.RE

.I remoteObjectName
getalltags
.RS
Returns a list of (CID, taglist) pairs associated with
.IR remoteObjectName .
This facility may be useful if the remote object is actually
being shared by more than one application. The CID specifies
which process has associated the taglist with the remote object.
.RE

.I remoteObjectName
deltag
.I tag
.RS
Deletes the tag, 
.IR tag ,
from 
.I remoteObjectName's
taglist.
.RE
.RE
.RE

.I objectName
destroy
.RS
Destroys
.IR objectName .
This command also destroys any remote objects created through 
.I objectName
as well as closing the connection to the cm process or service.
.RE

.I objectName
getobjs
.RI ? tag ?
.RS
If 
.I tag
is specified, returns a list of all remote objects created through
.I objectName
that contain
.I tag
in their taglists. If 
.I tag
is not specified, simply returns a list of all objects created through
.IR objectName .
.RE

.I objectName
getallobjs
.I tag
.RS
Returns a list of pairs (CID, object_list) such that each object in
each object_list has
.I tag
as part of it's taglist. The CID serves to indicate which application
created the objects in the associated object_list. 
.RE

.I objectName
configure_object
.I remoteObjectName option 
.RI ? value 
.I option value ...
?
.RS
Queries and modifies the common configuration options for 
.IR remoteObjectName .
The common configuration options available are:

.RS
-local_lost_connection_command
.RS
The command to execute if the connection to the remote process is unexpectedly
lost.
.RE

-local_destroy_command
.RS
The command to execute locally if the remote object is destroyed.
.RE

-remote_lost_connection_command
.RS
The command to execute remotely if the connection to the remote process
is unexpectedly lost.
.RE

-remote_destroy_command
.RS
The command to execute remotely if the remote object is destroyed.
.RE
.RE
.SH KEYWORDS
cm, cmt, cm toolkit
.SH AUTHORS
Ketan Patel (kpatel@cs.berkeley.edu)
