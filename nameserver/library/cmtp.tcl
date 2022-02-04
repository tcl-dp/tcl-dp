#
# This file contains the generic cmtp translate comand that cm processes
# can use to unparse the URL into the triple {host port filename}
#
# Also contains other misc. networking/URL related functions.

#---------------------------------------------------------------
# ignores optional arguments

proc translate {url args} {
    Gen_ListSet {protocol machine port file} [Net_SplitURL $url]
    puts "result - <$protocol $machine $port $file>"

    if {$protocol != "cmtp:"} {
	error "Unhandled protocol in URL: $url"
    }

    if {$machine == {} || $port == {}} {
	error "Badly specified machine:port in URL: $url"
    }

    if {[string index $file 0] != "~"} {
	set file "/$file"
    }
    if {![file exists $file]} {
	error "URL's file does not exist: $url"
    }

    return [list $machine $port $file]
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

#---------------------------------------------------------------
# returns 1 iff url appears to be an URL, else 0.


proc Net_IsURL {url} {
    return [regexp -nocase -- {^[a-z]+:} $url]
}

