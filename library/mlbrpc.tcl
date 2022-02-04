#
# Multicast LbRPC library file
#
# Author: Jon Knight <J.P.Knight@lut.ac.uk>
#
# mlbrpc.tcl,v
# Revision 1.1  1995/04/04  03:16:31  bsmith
# Stuff from Brian
#
#

#
# Create a multicast server on the specified group and port.  Returns the
# socket file descriptor used by the server.
#
proc MLbRPC_ServerCreate {group port} {
	# create multicast port and join group 
	set mfp [lindex [dp_connect -mudp $group $port 255] 0]
	# run the above procedure whenever there is data on the multicast port
	dp_filehandler $mfp r MLbRPC_ServerHandler
	# return the file descriptor for the multicast group
	return $mfp 
}

#
# Delete an existing Mulitcast LbRPC server on the specified socket
# descriptor.
#
proc MLbRPC_ServerDelete {fp} {
	# remove the background file handler
	dp_filehandler $fp
	# close the supplied socket descriptor
	close $fp
	return
}

#
# Initialise list of Transaction IDs processed so far
#
set MLbRPC_TidList(null) {}

#
# procedure to handle the execution of exported code.
#
proc MLbRPC_ServerHandler {mode fp} {
	global MLbRPC_TidList
	set incoming [dp_receiveFrom $fp]
	puts stderr $incoming
	set data [lindex $incoming 1]
	set host [lindex $data 0]
	set port [lindex $data 1]
	set tid [lindex $data 2]
	if [info exists MLbRPC_TidList($tid)] {
		set output $MLbRPC_TidList($tid)
	} else {
		set command [lindex $data 3]
		set output [eval $command]
		set MLbRPC_TidList($tid) $output
	}
	set reply_addr [dp_address create $host $port]
	set tfp [lindex [dp_connect -udp 0] 0]
	dp_sendTo $tfp $output $reply_addr
	close $tfp
}

#
# Execute supplied TCL code `code' by remote hosts in multicast IP group
# `group' on port number `port' with multicast TTL `ttl', waiting `timeout'
# seconds, retrying `retry' times and expecting `replies' replies.
#
proc MLbRPC_ClientExec {code group port ttl timeout retry replies} {
	# create multicast socket pointing at that group
	set mfp [lindex [dp_connect -mudp $group $port $ttl] 0]
	# add multicast <group,port> to address list
	set target_addr [dp_address create $group $port]
	# find out who we are!
	set ourhostname [MLbRPC_GetLocalFQDN]
	# create a TID for this transaction
	set tfp [open "|date" r]
	gets $tfp date
	close $tfp
	regsub -all {[: ]} $date {} date
	set tid "$ourhostname-$date-[pid]"
	# create a unicast UDP socket to listen for the results on
	set rfp [dp_connect -udp 0]
	set rport [lindex $rfp 1]
	set rfp [lindex $rfp 0]
#	dp_filehandler $rfp r MLbRPCClientListener
	set answers 0
	set attempts 0
	set result ""
	# Send the request to the group
	dp_sendTo $mfp "$ourhostname $rport $tid \{$code\}" $target_addr
	while {($attempts < $retry) && ($answers < $replies)} {
		# Wait for replies
		lappend result [dp_receiveFrom $rfp]
		incr answers
	}
	close $rfp
	close $mfp
	return $result
}

#
# Sneaky little procedure to get the Fully Qualified Domain Name of this
# host.
#
proc MLbRPC_GetLocalFQDN {} {
	set tfp [open "|hostname" r]
	gets $tfp hostname
	close $tfp
	set tfp [open "|nslookup $hostname" r]
	gets $tfp junk
	gets $tfp junk
	gets $tfp junk
	gets $tfp hostname
	close $tfp
	regsub {Name:[ ]*} $hostname {} hostname
	return "$hostname"
}
