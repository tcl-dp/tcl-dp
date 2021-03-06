# source the nscap file

if {[file exists $dp_library/ns/nscap] == 1} {
    source $dp_library/ns/nscap
} else {
    puts stderr "Can't find $dp_library/ns/nscap"
    exit 1
}

# global variables

set nsHandle 0
set nsID 0
set nsTickets(null) ""
set nsDefault 1
set nsPort ""
set nsHost ""

###################################################################
# Nameserver initialization routine
#
###################################################################

#
# ns_Init -- should no longer be used
#
proc ns_Init {} {
    global nsHandle
    set nsHandle [ns_GetNSConn]
}


#
# ns_GetNSConn -- get connection to the nameserver
#
proc ns_GetNSConn {} {
    global nsDefault 
    global nsPort nsHost

    if {$nsDefault} {
        return [ns_GetDefaultNSConn]
    } else {
        if [catch {dp_MakeRPCClient $nsHost $nsPort} handle] {
            error "Can't establish connection to nameserver at $nsHost:$nsPort"
        } else {
            return $handle
        }
    }
}

#
# ns_GetDefaultNSConn -- get connection to the default nameserver
#
proc ns_GetDefaultNSConn {} {
    global nameServers
    global nsID

    # First go down the list of nameservers and connect to
    # the first available one and then ask it for the 
    # primary server

    set nsID 0
    foreach ns $nameServers {
        set host [lindex $ns 0]
        set port [lindex $ns 1]

        if [catch {dp_MakeRPCClient $host $port} handle] {
            incr nsID 
            continue
        }

        # Ask the nameserver whom the primary is        
        if [catch {dp_RPC $handle ns_WhoIsPrimary} id] {
            dp_CloseRPC $handle
            continue
        }

        # We got the primary server
        if {$nsID == $id} {
            dp_socketOption $handle keepAlive yes
            dp_socketOption $handle autoClose yes
#            dp_atclose $handle append ns_CrashHandler
            return $handle 
        }
        
        dp_CloseRPC $handle
        set handle [ns_EstablishPrimaryNSConn $id]
        if {$handle != ""} {
            return $handle
        }
    }
    error "No servers are currently running"
}


#
# ns_EstablishPrimaryNSConn -- establish connection with the primary.
# If the primary down, call ns_GetNSConn again.
#
proc ns_EstablishPrimaryNSConn {id} {
    global nameServers
    global nsID

    set nsID $id    
    set globalNS [lindex $nameServers $id]
    set host [lindex $globalNS 0]
    set port [lindex $globalNS 2]

    if [catch {dp_MakeRPCClient $host $port} handle] {
        return ""
    }
    dp_socketOption $handle keepAlive yes
#    dp_atclose $handle append ns_CrashHandler
    return $handle
}


#
# ns_EstablishBackupNSConn -- establish connection to the backup
# nameserver
#
proc ns_EstablishBackupNSConn {id} {
    global nameServers
    global nsID

    set nsCount [llength $nameServers]
    
    for {set i [expr ($id+1)%$nsCount]} {$i != $id} {set i [expr ($i+1)%$nsCount]} {
        set backupNS [lindex $nameServers $i]
        set host [lindex $backupNS 0]
        set port [lindex $backupNS 2]

        if [catch {dp_MakeRPCClient $host $port} handle] {
            continue
        }
        
        # We have found a running backup server.
        # Tell it to become the primary server

        set nsID $i
        if {[catch {dp_RDO $handle ns_SwitchToBackup} result]} {

            dp_CloseRPC $handle
            continue
        }
        return $handle
    }
}    

#
# ns_CrashHandler -- called when the primary server goes down
#
proc ns_CrashHandler {} {
    global nsHandle
    
    set nsHandle [ns_GetNSConn]
    dp_socketOption $nsHandle keepAlive yes
#    dp_atclose $nsHandle append ns_CrashHandler
    flush stdout
}


#
# ns_RelNSConn -- release connection to the nameserver
#
proc ns_RelNSConn {} {
    global nsHandle
    
    dp_CloseRPC $nsHandle
}


#######################################################################
# Client Interface routines
#
#######################################################################


#
# ns_FindServices -- return a list of {srvc {host port}} where srvc
# matches the pattern.
# If the service is not running, {host port} will be empty.
#
proc ns_FindServices {srvcList} {
    set result {}
    foreach srvc $srvcList {
        set result [concat $result [ns_RPC ns_FindServices $srvc]]
    }
    return $result
}


#
# ns_ListServices -- return a list of services matching pattern
#
proc ns_ListServices {pattern} {
    return [ns_RPC ns_ListServices $pattern]
}


#
# ns_LaunchServices -- launch services matching pattern
# Ignore the command if the service is in the "running" or 
# "launching" state
#
proc ns_LaunchServices {srvcList} {
    foreach srvc $srvcList {
        ns_RPC ns_LaunchServices $srvc
    }
}


#
# ns_ServiceState -- return the current state of services matching
# pattern. The possible states are: onDemand, launching or dead.
# If flag is set to "dead", the service will be reset.
#
proc ns_ServiceState {srvcList {flag {}}} {
    set result {}
    foreach srvc $srvcList {
        set result [concat $result [ns_RPC ns_ServiceState $srvc $flag]]
    }
    return $result
}


#
# ns_AuthenticateService -- ask the server to authenticate the service.
# Returns a pair of tickets to be used for communication
# between the client and the service.
# If the service is not authentic, it will be marked dead
#
proc ns_AuthenticateService {srvcName} {
    return [ns_RPC ns_AuthenticateService $srvcName]
    
}

#
# ns_KillServices -- ask the name server to kill the services specified
#                    by srvcList
#
proc ns_KillServices {srvcList} {
    foreach srvc $srvcList {
        ns_RPC ns_KillServices $srvc
    }
    return
}

#
# ns_IncrConnection -- increment the connection count of services 
#                      specified by pattern
#
proc ns_IncrConnection {pattern} {
    return [ns_RPC ns_IncrConnection $pattern]
}

#
# ns_DecrConnection -- decrement the connection count of services
#                      specified by pattern
proc ns_DecrConnection {pattern} {
    return [ns_RPC ns_DecrConnection $pattern]
}

###################################################################
# Server Interface routines
#
###################################################################

#
# ns_AdvertiseService -- called by the service to advertise
# its port number to the nameserver. Ticket should be the
# one issued by the nameserver when it launched the service.
# If null, the service will be treated as unregistered.
#
proc ns_AdvertiseService {srvcName host port pid {ticket {}}} {
    return [ns_RPC ns_AdvertiseService $srvcName $host $port $pid $ticket]
}


#
# ns_UnadvertiseService -- the inverse of ns_AdvertiseService
# Unregistered service will be deleted. Registered service
# will have its port number reset
#
proc ns_UnadvertiseService {srvcName {ticket {}}} {

    return [ns_RPC ns_UnadvertiseService $srvcName $ticket]
}


#
# ns_Register -- used to add, delete, modify, alias and info services
#
proc ns_Register {args} {
    global nsHandle

    set tries 0
    while { $tries<2} {
        if { [catch "dp_RPC $nsHandle ns_Register $args" result] } {
            #error in performing RDO.   Errors from bad connections
            #look like: file "file3" isn't open.
            if { [regexp {file \"[a-z0-9]*\" isn't open} $result] } {
            # errmsg "RDO to the nameserver failed.  Will retry"
              ns_CrashHandler
              set result ""
              incr tries
              } else {
            # This is the clients error - pass it on
                error $result
            }
        } else {
            return $result
        }
    }
    # The nameserver is probably dead
    # The chances of getting here should be very small
    error "No nameserver is currently running"
}


#
# ns_Authenticate -- called by the client to authenticate a service
#
proc ns_Authenticate {ticket} {
    global nsTickets

    if {[catch {set nsTickets($ticket)} t]} {
        return ""
    } else {
        return $t
    }
}

#
# ns_RecordTicket -- record tickets generated by the nameserver
#
proc ns_RecordTicket {tickets} {
    global nsTickets

    set t1 [lindex $tickets 0]
    set t2 [lindex $tickets 1]
    set nsTickets($t1) $t2
}


######################################################################
# Utility funcitons
#
######################################################################

#
# ns_RPC -- RPC to the name server
#
proc ns_RPC {args} {
    global nsHandle errorInfo

    set tries 0
    while { $tries<2} {
        if { [catch "dp_RPC $nsHandle $args" result] } {
            # error in performing RPC.   Errors from bad connections
            # look like: file "file3" isn't open.
            if { [regexp {file \"[a-z0-9]*\" isn't open} $result]  ||
                 [regexp {bad file identifier \"[a-z0-9]*\"} $result]} {
                ns_CrashHandler
                set result ""
                incr tries
            } else {
		# This is the clients error - pass it on
                error "errorInfo: $errorInfo; $result"
            }
        } else {
            return $result
        }
    }
    # The nameserver is probably dead
    # The chances of getting here should be very small
    error "No nameserver is currently running"
}



######################################################################
# Higher Level API
#
######################################################################

#
# NS_SetHostPort -- set the default nameserver host and port
#
proc NS_SetHostPort {host port} {
    global nsPort nsHost nsDefault

    set nsDefault 0
    set nsHost $host
    set nsPort $port
    return
}

#
# NS_UnsetHostPort -- unset the default nameserver host and port
#
proc NS_UnsetHostPort {} {
    global nsPort nsHost nsDefault

    set nsDefault 1
    set nsHost ""
    set nsPort ""
}    



########################################################################
# The following routines are used for individual services.
########################################################################

#
# NS_FindService -- return the {srvc {host port}} pair for the service
#                   If srvc is a pattern, the one with minimum connections
#                   will be returned.
#
proc NS_FindService {srvc} {
    if {[catch {ns_FindServices $srvc} result]} {
        error "$result"
    } else {
        if {$result == ""} {
            error "$srvc does not exist"
        } 

        # Find the service with the least number of connections
        set srvcList ""
        set minConn 100000
        foreach x $result {
            set conn [lindex $x 2]
            if {$conn < $minConn} {
                set minConn $conn
                set srvcList $x
            }
        }
        return $srvcList
    }
}


#
# NS_LaunchService -- launch a service and wait for it to enter
# the "running" state
# If the the service doesn't come up in 25 seconds, an error is
# raised.
#
proc NS_LaunchService {srvc} {
    set tries 0

    # service is not currently running
    set start [ns_systime]
    set delay 0
    while {$tries < 250} {
        if {[catch {ns_ServiceState $srvc} result]} {
            error "$result"
        } else {
            set state [lindex [lindex $result 0] 1]
            if {$state == "onDemand"} {
                if {[catch {ns_LaunchServices $srvc} result]} {
		    error "$result"
                } else {
                    dp_after $delay
		    set delay 100
                }
            } elseif {$state == "launching"} {
                incr tries
		dp_after $delay
		set delay 100
            } elseif {$state == "running"} {
                return
            }
        }
    }
    ns_ServiceState $srvc dead
    error "Service connection timed out"
}

#
# NS_GetServiceConn -- connect to a service and return the connection handle.
# If a flag is set to "authenticate", authentication of the service
# will be performed.
# If srvc is a pattern indicating a group of service, the service with the
# minimum number of connections is returned.
#
proc delay {s} {expr [ns_systime]-$s}
proc NS_GetServiceConn {srvc {flag {}}} {

    set tries 0
    set start [ns_systime]
    
    while {$tries < 3} {
        if {[catch {NS_FindService $srvc} result]} {
            error $result
        } else {
            set srvc [lindex $result 0]
            set hp [lindex $result 1]
            if {$hp == ""} {
                if {[catch {NS_LaunchService $srvc} result]} {
                    error "$result"
                } else {
                    if {[catch {NS_FindService $srvc} result]} {
                        error $result
                    } else {
                        set hp [lindex $result 1]
                        if {$hp == ""} continue
                    }
                }
            }

            # Ask the nameserver to authenticate the service
            if {$flag == "authenticate"} {
                if {[catch {ns_AuthenticateService $srvc} result]} {
                    error "$result"
                } else {
                    set tickets  $result
                }
            }
                    
            set host [lindex $hp 0]
            set port [lindex $hp 1]
            if {[catch {dp_MakeRPCClient $host $port} handle]} {
                # something bad happened to the service
                incr tries
                ns_ServiceState $srvc dead
                continue
            } else {
                if {$flag == "authenticate"} {
                    if {[dp_RPC $handle ns_Authenticate [lindex $tickets 0]] ==
                    [lindex $tickets 1]} {
                        ns_IncrConnection $srvc
                        dp_atclose $handle append "ns_DecrConnection $srvc"
                        return $handle
                    } else {
                        error "$srvc is not an authentic service"
                    }
                } else {
                    ns_IncrConnection $srvc
                    dp_atclose $handle append "ns_DecrConnection $srvc"
                    return $handle
                }
            }
        }
    }
    error "Can't connect to $srvc"
}

#
# NS_GetServiceHP -- returns the host and port of the service
# If srvc is a pattern indicating a group of services, the service with the
# minimum number of connections is returned.

#
proc NS_GetServiceHP {srvc} {

    set tries 0
    
    while {$tries < 3} {
        if {[catch {NS_FindService $srvc} result]} {
            error $result
        } else {
            incr tries
            set srvc [lindex $result 0]
            set hp [lindex $result 1]
            if {$hp == ""} {
                if {[catch {NS_LaunchService $srvc} result]} {
                    error "$result"
                } 
            } else {
                return $hp
            }
        }
    }
    error "Can't get {host port} for $srvc"
}



########################################################################
# The following routines are used for multiple services
########################################################################

#
# NS_FindServices -- return a list of {srvc {host port}} for services
#                    specified in srvcList
#
proc NS_FindServices {srvcList} {
    if {[catch {ns_FindServices $srvcList} result]} {
        error "$result"
    } else {
        if {$result == ""} {
            error "$srvc does not exist"
        } 
        return "$result"
    }
}


#
# NS_LaunchServices -- launch a service and wait for it to enter
# the "running" state
# If the the service doesn't come up in 25 seconds, an error is
# raised.
#
proc NS_LaunchServices {srvcList} {
    set tries 0

    # service is not currently running
    while {$tries < 250} {
        if {[catch {ns_ServiceState $srvcList} result]} {
            error "$result"
        } else {
            incr tries
            set dList {}
            set lList {}
            foreach x $result {
                set state [lindex $x 1]
                if {$state == "onDemand"} {
                    lappend dList [lindex $x 0]
                } elseif {$state == "launching"} {
                    lappend lList [lindex $x 0]
                }
            }

            if {$dList != {}} {
                if {[catch {ns_LaunchServices $dList} result]} {
                    error "$result"
                } else {
                    dp_after 100
                    continue
                }
            } elseif {$lList != {}} {
                dp_after 100
                continue
            } else {
                return 
            }
        }
    }
}

#
# NS_GetServicesHP -- returns a list of {srvc {host port} conn} for services
#                     specified by srvcList. Conn is the number of connections
#                     the service is currently maintaining.
#
proc NS_GetServicesHP {srvcList} {

    set tries 0
    
    while {$tries < 3} {
        if {[catch {NS_FindServices $srvcList} result]} {
            error $result
        } else {
            incr tries
            set sList {}
            foreach x $result {
                if {[lindex $x 1] == ""} {
                    lappend sList [lindex $x 0]
                }
            }
            if {$sList != {} } {
                if {[catch {NS_LaunchServices $sList} result]} {
                    error "$result"
                } 
            } else {
                return "$result"
            }
        }
    }
    return "$result"
}


#
# NS_BadConnection -- tell the nameserver about the bad service connection
#
proc NS_BadConnection {srvc} {
    if {[catch {ns_ServiceState $srvc dead} result]} {
        error $result
    } 
}


#
# NS_SrvcInit -- should be called by any service when it starts up
#                Srvc is the name of the service and can be determined by 
#                calling NS_GetSrvcName.
#                Host is the name of the host machine and can be determined by
#                calling NS_GetHostName.
#                Port is the port number of the service and is the return
#                value of dp_MakeRPCServer.
#
proc NS_SrvcInit {srvc host port} {
    global nsMyTicket argv

    # Check if all the args are supplied
    if {$port == ""} {
        error "No port number given!"
    }
    if {$srvc == ""} {
        error "No service name given!"
    }
    if {$host == ""} {
        set host [NS_GetHostName]
    }

    # Get the ticket if it exists
    if {[lindex [dp_isready stdin] 0]} {
        set tickets [gets stdin]
        set nsMyTicket [lindex $tickets 1]
        ns_RecordTicket $tickets
    } else {
        set nsMyTicket {}
    }

    ns_AdvertiseService $srvc $host $port [pid] $nsMyTicket 
}

#
# NS_GetSrvcName -- return the service name given in argv after the -service
#                   flag
#
proc NS_GetSrvcName {} {
    global argv

    set srvc ""
    set doServiceFlag 0

    # get the name of the service from the argv
    foreach i $argv {
        if {$i == "-service"} {
            set doServiceFlag 1;
        } else {
            if {$doServiceFlag} {
                set srvc $i;
                set doServiceFlag 0;
                return $srvc
            }
        }
    }
}

#
# NS_GetHostName -- return the host name by calling /bin/hostname
#
proc NS_GetHostName {} {
    return [exec "/bin/hostname"]
}

#
# NS_SrvcExit -- should be called by the exiting service
#
proc NS_SrvcExit {srvc} {
    global nsMyTicket

    ns_UnadvertiseService $srvc $nsMyTicket
}


