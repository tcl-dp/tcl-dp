##########################################################
# 
# A trivial file transfer layer on top of DP.
#	mperham 3/97
# 
# Assumes a server has been started with "fserver"
# and a client connected to that server via "fconnect"
#
# The user interface:
#
#    fconnect $host {$port} - open an RPC connection to host:port
#			    and a data connection to host:port-1
#			    port is optional
#    fget $filename - copy remote file, $filename, to local machine.
#    fput $filename - copy local file, $filename, to remote machine.
#    fdel $filename - delete a remote file named $filename
#    fdir - remote directory listing
#    fpwd - print current remote directory
#    fcd - change remote directory
#
# Problems:
#
#    Minimal error handling at best.  Need to catch more.
#

package require dp 4.0

#//////////////////////////////////////

proc oops {errMsg} {
    puts stdout "Remote server returned error:"
    puts stdout "$errMsg"
}

#//////////////////////////////////////

proc fget {filename} {
    global f_chan dataPort host 

# Make data channel
# On server side

    if [catch {
        dp_RPC $f_chan eval {set dataServ [dp_connect tcp -server 1 -myport} $dataPort {]}
        dp_RDO $f_chan eval {set dataSC [dp_accept $dataServ]}
    } msg] {
        puts stdout "Error opening dataServer socket (fget): $msg"
        close $f_chan
        return
    }

# And on Client side
    
    if [catch {
        set dataClient [dp_connect tcp -host $host -port $dataPort]
    } msg] {
        puts stdout "Error connecting to dataServer socket: $msg"
        close $f_chan
        return
    }

# Tell server to send file

    dp_RPC $f_chan eval {set srcFile [open} $filename {r]}
    dp_RPC $f_chan eval {dp_send [lindex $dataSC 0] [dp_recv $srcFile]}

# Open output file on client
    set destfile [open $filename {CREAT WRONLY}]
	
	puts stdout "Getting $filename from remote host."

# Put all coming stuff to $newfile and close it

    dp_send $destfile [dp_recv $dataClient]
    close $destfile

# First close the Client, before you close the server !!

    close $dataClient

# Close Server now 
    dp_RPC $f_chan eval {close $srcFile}
    dp_RPC $f_chan eval {close [lindex $dataSC 0]}
    dp_RPC $f_chan eval {close $dataServ}
}

#//////////////////////////////////////

proc fput {filename} {
    global f_chan dataPort host

# Make data channel
# On server side

    if [catch {
        dp_RPC $f_chan eval {set dataServ [dp_connect tcp -server 1 -myport} $dataPort {]}
        dp_RDO $f_chan eval {set dataSC [dp_accept $dataServ]}
    } msg] {
        puts stdout "Error opening dataServer socket (fput): $msg"
        close $f_chan
        return
    }

# And on client side

    if [catch {
        set dataClient [dp_connect tcp -host $host -port $dataPort]
    } msg] {
        puts stdout "Error connecting to dataServer socket: $msg"
        close $f_chan
        return
    }

# Sending on client side (local) 
    set srcfile [open $filename r]
    puts stdout "Putting $filename to remote host."
    dp_send $dataClient [dp_recv $srcfile]

# Tell server to recv file

    dp_RPC $f_chan eval {set destFile [open} $filename {w]}
    dp_RPC $f_chan eval {dp_send $destFile [dp_recv [lindex $dataSC 0]]}

#Close client BEFORE you close Server !!

    close $dataClient
    close $srcfile

# Now close Server
    dp_RPC $f_chan eval {close $destFile}
    dp_RPC $f_chan eval {close [lindex $dataSC 0]}
    dp_RPC $f_chan eval {close $dataServ}

}

#//////////////////////////////////////

proc fpwd {} {
    global f_chan

    dp_RPC $f_chan if {$tcl_platform(platform) == "windows"} {cmd.exe /c cd} else { pwd }
}

#//////////////////////////////////////

proc fdir {} {
    global f_chan

# I think this will be broken for filenames with spaces.
# The foreach will think they are different files.

    foreach a [dp_RPC $f_chan glob \*] {
	puts $a
    }
}

#//////////////////////////////////////

proc fdel {filename} {
    global f_chan

    dp_RPC $f_chan file delete $filename
}

#//////////////////////////////////////

proc fcd {dir} {
    global f_chan

    dp_RDO $f_chan -onerror oops cd $dir
}

#//////////////////////////////////////

proc fquit {} {
    global f_chan

    close $f_chan
}

#//////////////////////////////////////
# We need to open one channel on each side; the
# the data channel - $dataClient here, $dataSC on the server -
# will be created each time we send or recv a file
# since we can't send normal data over an RPC channel.

proc fconnect {host_add {port 19743}} {
    global f_chan dataPort host

	set host $host_add
    set f_chan [dp_MakeRPCClient $host $port]
    puts stdout "Connected to $host..."
    puts stdout "Channel: $f_chan"
    set dataPort [incr port -1]
    
    puts stdout "--------- Directory Listing ----------"
    fdir
    puts stdout "--------- Remote Directory -----------"
    fpwd
}
