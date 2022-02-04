

proc CMTP_Translate {url args} {
    set l  [Net_SplitURL $url]
    puts "URL: $l"
    set protocol [lindex $l 0]
    set machine [lindex $l 1]
    set port [lindex $l 2]
    set file [lindex $l 3]

    switch $protocol {
        cmtp: { 
            return [eval [list CMTP_translate $machine $port $file $args]]
        } 
        vodp: {
            return [eval [list VODP_translate $machine $port $file $args]]
        }
        default {
            error "Unhandled protocol in URL:$url"
        }
    }
}

set counter 0
set timerID 0
set expired 0

proc SetOffTimer {id} {
    global timerID expired

    if {$id == $timerID} {
        set expired 1
    }
}

set kCMP_ROOT /n/vid2/CMP_ROOT        
proc CMTP_translate {machine port file args} {
    global counter timerID expired
    global kCMP_ROOT 

    set time 40000
    set timeoutFlag 0
    set retryFlag 0
    set retryArgFlag 0
    set args [lindex $args 0]
    foreach x $args {
        puts $x
        if {$x == "-timeout"} {
            set timeoutFlag 1
            continue
        } elseif {$x == "-retry"} {
            set retryFlag 1
            set retryArgFlag 1
            continue
        } elseif {$timeoutFlag == 1} {
            set time [expr $x*1000]
            set timeoutFlag 0
            continue
        } elseif {$retryArgFlag == 1} {
            puts "Retry $x"
            set l  [Net_SplitURL $x]
            puts "retry URL: $l"
            set machine [lindex $l 1]
            set port [lindex $l 2]
            set retryArgFlag 0
        }
    }
    puts "time = $time"        
    set url "<CMTP $machine $port $file>"
    if {$machine == {}} {
        puts stderr "Badly spec'ed machine: $url"
        error "Badly specified machine:port in URL: $url"
    }
    set machine gumby
    if {[string index $file 0] != "~"} {
        if {![regexp {^([^~:/\.]+)[\./]} $file x machine]} {
            puts stderr "bad directory: $url , $file"
            error "bad directory: $url"
        }
        set srvcList [ns_FindServices /cms/*]
        if {![lsearch -exact {$srvcList} /cms/$machine]} {
            set machine gumby
        }
        set file "$kCMP_ROOT/$file"
    }
    if {![file exists $file]} {
        error "URL's file does not exist: $url"
    }
    set file [glob $file]

    set srvcName /cms/$machine
    if {$retryFlag == 1} {
        ns_ServiceState $srvcName dead
        set hp ""
    } else {
        set srvc [lindex [ns_FindServices $srvcName] 0]
        set hp [lindex $srvc 1]
        set host [lindex $hp 0]
        set port [lindex $hp 1]
    }
    if {$hp == ""} {
        set expired 0
        dp_after $time "SetOffTimer $timerID"
        ns_LaunchServices $srvcName
        while {1} {
            dp_update
            if {$expired == 1} {
                error "CMTP translation timedout"
            }
            set srvc [lindex [ns_FindServices $srvcName] 0]
            set hp [lindex $srvc 1]
            set host [lindex $hp 0]
            set port [lindex $hp 1]
            if {$hp != ""} {
                incr timerID
                return "cmtp://$host:$port$file"
            }
        }               
    } else {
        return "cmtp://$host:$port$file"
    }
}

proc VODP_translate {machine port file args} {
    global timerID expired

    set time 40000
    set timeoutFlag 0
    set retryFlag 0
    foreach x $args {
        if {$x == "-timeout"} {
            set timeoutFlag 1
            continue
        } elseif {$x == "-retry"} {
            set retryFlag 1
            continue
        } elseif {$timeoutFlag == 1} {
            set time [expr $x*1000]
            set timeoutFlag 0
            continue
        }
    }
    puts "time = $time"
    set srvcName /vods/vmgr
    if {$retryFlag == 1} {
        ns_ServiceState $srvcName dead
        set hp ""
    } else {
        set srvcList [ns_FindServices $srvcName]
        set srvc [lindex $srvcList 0]
        puts "Service: $srvc"
        set hp [lindex $srvc 1]
        set host [lindex $hp 0]
        set port [lindex $hp 1]
    }
    if {$hp == ""} {
        set expired 0
        dp_after $time "SetOffTimer $timerID"
        ns_LaunchServices $srvcName
        while {1} {
            dp_update
            if {$expired == 1} {
                error "VODP translation timeout"
            }
            set srvc [lindex [ns_FindServices $srvcName] 0]
            set hp [lindex $srvc 1]
            set host [lindex $hp 0]
            set port [lindex $hp 1]
            if {$hp != ""} {
                incr timerID
                return "vodp://$host:$port/$file"
            }
        }               
    } else {
        return "vodp://$host:$port/$file"
    }

}

#---------------------------------------------------------------
# parses an URL into {protocol machine port file} with {} filling
# unspecifed slots.
# error: iff machine spec is not correct.


proc Net_SplitURL {url} {
    set mach {}
    set port {}
    set file {}
    if {[regexp -nocase -- {^([a-z]+:)(.*)$} $url  zip proto rest]} {
	# puts "protocol+ -- $proto $rest"
	if {[regexp {^(//[^/]+/)(.*)$} $rest  zip mach rest]} {
	    # puts "machine+ -- $mach $rest"
	    if {![regexp {^//([^/:]+)(:([0-9]+))?/$} $mach  \
		  zip mach zip port]} {
	    error "Bad machine spec in URL: $url"
	    }
	}
	set file $rest
	# puts "parse+ -- $mach $port"
    } else {
	set proto "file:"
	set file $url
    }
    return [list $proto $mach $port $file]
}
