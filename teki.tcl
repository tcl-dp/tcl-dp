#!/bin/sh
# \
exec wish "$0" ${1+"$@"}

if {$tcl_version < 7.6} {
    puts stderr "requires Tcl 7.6 or later"
    exit
}

# teki.tcl --
#
#   The Tcl Extension Kit/Installer.  This file manages packages for
#   Tcl/Tk

# Notes about the organization of this file:
#   Major sections are delimited by a string of "-" characters -- e.g. "-------"
#   At the beginning of each major section, the purpose and routines in that
#   section are described.
#   teki-specific packages reside in the "tekilib" directory below teki.tcl

#set tk_version 7.6
set home [string trimright [file dirname [info script]] ./]
set home [file join [pwd] $home tekilib]
lappend auto_path $home
set TekiInfo(library) $home
unset home

if [catch {package require http}] {
    set TekiInfo(http) 0
} else {
    set TekiInfo(http) 1
}

package require Debug
package require Undo

# Define some useful fonts

if [info exists tk_version] {
    set TekiInfo(gui) 1
    set majorversion [lindex [split $tk_version .] 0]
    if {$majorversion >= 8} {
        set TekiInfo(varfont) [font create -family Times -weight bold -size 18]
        set TekiInfo(fixedfont) [font create -family Courier -size 10]
    } else {
        set TekiInfo(varfont) *-Times-Bold-R-Normal--*-240-*-*-*-*-*-*
        set TekiInfo(fixedfont) -*-Courier-Medium-R-Normal--*-120-*-*-*-*-*-*
    }
    unset majorversion
} else {
    set TekiInfo(gui) 0
}

if $TekiInfo(gui) {
    package require Progress-Tk
    package require Wise
} else {
    package require Progress-Tcl
}

# -----------------------------------------------------------------------
#
# Error handling, general UI commands.
#
# This section contains code to handle errors and print messages.
# It defines basic functions whose interface can be a GUI (if we're
# running in wish) or a text based interface (if we're running in
# a shell)

# TekiError prints an error message
# TekiWarning is called to ask the user for confirmation or a decision
#    (such as deleting a file or set of files)

if $TekiInfo(gui) {
  proc TekiError {msg} {
    bgerror "$msg"
  }
  proc TekiWarning {msg default options} {
    Wise_Message "Warning" $msg $options 0
  }
} else {
  proc TekiError {msg} {
    puts stderr $msg
  }
  proc TekiWarning {msg default options} {
    global x
    puts stderr "Warning: $msg"
    set len [llength $options]
    set done 0
    while {!$done} {
        for {set i 0} {$i < $len} {incr i} {
            puts stderr "Type $i to [lindex $options $i]"
        }
        set x [string trim [gets stdin]]
        if [string match {[0-9]} "$x"] {
            set done [expr ($x >=0) && ($x < $len)]
        }
    }
    return $x
  }
}

# -----------------------------------------------------------------------
#
# User Interface
#
# This section of code creates and manages the user interface.  The UI
# consists of a main window with two parts (top and bottom), plus a  menu
# bar.  The top contains the readme file of the currently selected package,
# the bottom a list of packages installed on the system.  The
# functions TekiCreateMenus and TekiCreateWindow create this interface.
# The function TekiCreateUI creates the menus and main window (by
# calling TekiCreateMenus and TekiCreateWindow).
#
# The function TekiUpdateReadme redraws the top portion, and should
# be called whenever the current package -- TekiInfo(currPackage) --  is changed.
# The function TekiUpdateBrowser redraws the bottom, and should be
# called whenever tclPkgInfo(installed) changes.  The function
# TekiBrowserCallback is called when the user clicks in the bottom
# area, selecting a new package.  The functions TekiDoFileInstall,
# TekiDoFileUninstall, and TekiDoFileExit, are called when the user
# selects File->Install, File->Uninstall, or File->Exit menu items,
# respectively.  DoFileInstall pops up a file selection dialog,
# then verifies and installs the file.

set TekiInfo(currPackage)        tcl[info tclversion]

#
# TekiCreateMenus
#    Create the menu bar across the top of the window
#
# Arguments:
#    None
#
proc TekiCreateMenus {} {
    global tclPkgInfo TekiInfo
    Debug_Enter TekiCreateMenus
    frame .menu -relief raised -bd 2
    pack .menu -side top -fill x

    # File Menu
    set m .menu.file.m
    menubutton .menu.file -text File -menu $m -underline 0
    pack .menu.file -side left
    menu $m
    $m add command -label "Install" -underline 0 -command TekiDoFileInstall
    $m add command -label "Uninstall" -underline 1 -command TekiDoFileUninstall
    $m add separator
    $m add command -label "Exit" -underline 1 -command TekiDoFileExit

    # WWW Menu
    set m .menu.www.m
    menubutton .menu.www -text WWW -menu $m -underline 0
    pack .menu.www -side left
    menu $m
    $m add command -label "Update" -underline 0 -command TekiDoWWWUpdate
    $m add command -label "Browse" -underline 1 -command TekiDoWWWBrowse

    if {$TekiInfo(http) == 0} {
        .menu.www.m entryconfigure Browse -state disabled
        .menu.www.m entryconfigure Update -state disabled
    }

    Debug_Leave
}

#
# TekiCreateWindow
#    Create the main window -- this window lists the installed packages, and has a
#    README section.
#
# Arguments:
#    none
#
proc TekiCreateWindow {} {
    global tclPkgInfo TekiInfo
    Debug_Enter TekiCreateWindow

    set w .browser
    frame $w
    pack $w -side bottom -fill x -expand no
    scrollbar $w.scroll -command "$w.list yview"
    listbox $w.list -font $TekiInfo(fixedfont) -relief sunken -width 100 \
                 -bd 2 -yscrollcommand "$w.scroll set" -setgrid 1 -height 12 \
                 -selectmode extended
    pack $w.scroll -side right -fill y
    pack $w.list -expand no -fill both
    set TekiInfo(Browser) $w.list
    TekiUpdateBrowser

    # Create the README section of the frame
    set w .readme
    frame $w
    pack $w -side top -fill both -expand yes
    scrollbar $w.scroll -command "$w.text yview"
    text $w.text -font $TekiInfo(fixedfont) -relief sunken -width 100 \
                 -bd 2 -yscrollcommand "$w.scroll set" -setgrid 1 -height 10
    set TekiInfo(Readme) $w.text
    pack $w.scroll -side right -fill y
    pack $w.text -expand yes -fill both
    TekiUpdateReadme

    update
    bind $TekiInfo(Browser) <Button-1> TekiBrowserCallback
    bindtags $TekiInfo(Browser) [list Listbox all $TekiInfo(Browser)]

    Debug_Leave
}

#
# TekiUpdateReadme -
#   Updates the readme window of the Info window.
#   Called whenever the current package changes.
#   The associated widget is a text widget.
#
proc TekiUpdateReadme {} {
    global tclPkgInfo TekiInfo
    Debug_Enter TekiUpdateReadme
    set w $TekiInfo(Readme)
    set currPkg [lindex $TekiInfo(currPackage) 0]
    if {[string length $currPkg] == 0} {
        return
    }
    set filename $tclPkgInfo($currPkg,infoFile)
    set name $tclPkgInfo($currPkg,name)

    $w delete 1.0 end
    $w insert end "Information on the $currPkg package\n\n"

    set m1 .menu.file.m
    set m2 .menu.www.m

    if {($name == "Tcl") || ($name == "Tk")} {
        $m1 entryconfigure Uninstall -state disabled
        $m2 entryconfigure Update -state disabled
    } else {
        $m1 entryconfigure Uninstall -state normal
        if $TekiInfo(http) {
            $m2 entryconfigure Update -state normal
        }
    }

    if {[string length $filename] &&
       [file exists $filename] &&
       [file readable $filename]} {
        set f [open $filename r]
        $w insert end [read $f]
        close $f
    } else {
        $w insert end "No information available"
    }        

    wm title . "TEKI -- $currPkg"
    Debug_Leave
}

#
# TekiUpdateBrowser -
#   Updates the browser window.
#   Called whenever the list of installed packages changes.
#
proc TekiUpdateBrowser {} {
    global tclPkgInfo TekiInfo
    Debug_Enter TekiUpdateBrowser
    set w $TekiInfo(Browser)
    $w delete 0 end
    $w insert end "Packages installed:"
    $w insert end [format "%-11s%-8s     %-15s%-28s" "package" "version" "requires" "description"]
    if {[winfo depth $w] > 1} {
        set hot "-background #808080 -relief raised -borderwidth 1"
        set normal "-background {} -relief flat"
    } else {
        set hot "-foreground white -background black"
        set normal "-foreground {} -background {}"
    }

    set tclPkgInfo(installed) [lsort $tclPkgInfo(installed)]
    foreach pkg $tclPkgInfo(installed) {
        set des $tclPkgInfo($pkg,description)
        set ver $tclPkgInfo($pkg,version)
        set req $tclPkgInfo($pkg,requires)

        # The following code does one of two things, depending
        # on whether the package is part of another package.
        set split [file split $pkg]
        set lsplit [llength $split]

        $w insert end [format "%-15s%-8s%-16s%-28s" $pkg $ver $req $des]
    }
    Debug_Leave
}

#
# TekiBrowserCallback --
#    This procedure is called when the user clicks on a package in the browser window.
#    It is responsible for updating the UI to show details of the selected package
#
# Arguments:
#    index        The index of the character that the user clicked on.
#
proc TekiBrowserCallback {} {
    global tclPkgInfo TekiInfo
    Debug_Enter "TekiBrowserCallback"
    set w $TekiInfo(Browser)
    set packages {}
    foreach i [$w curselection] {
        if {$i < 2} {
            # The first two elements are the labels, so don't allow the
            # user to select these
            $w selection clear $i
        } else {
            set info [$w get $i]
            set pkg [lindex $info 0]
            lappend packages $pkg
        }
    }
    set TekiInfo(currPackage) $packages
    TekiUpdateReadme
    Debug_Leave
}

#
# TekiCreateUI
#    Create the TEKI User Interface in the main window (".")
#        1. Create the menus
#        2. Create & show the browse windows
#
proc TekiCreateUI {} {
    global tclPkgInfo TekiInfo
    Debug_Enter TekiCreateUI
    TekiCreateMenus
    TekiCreateWindow
    set currPkg $TekiInfo(currPackage)
    wm title . "TEKI -- $currPkg"
    wm iconname . "TEKI"
    Debug_Leave
}

#-----------------------------------------------------------------------

# WISE user interface support

# The following procedures control the WISE user interface (defined in the
# Wise package).  The wizards are called by the Teki*Wizard procedures.
#
# The function TekiInteractiveInstall coordinates everything -- it
# reads/verifies the Teki file, calls the wizards to select various
# parameters, downloads any files that are needed, and calls TekiInstall
#
# The functions TekiNormalizeDirName 

# Bring the name of a directory into "canonical" form.  What a pain!

proc TekiNormalizeDirName {d} {
    Debug_Enter TekiNormalizeDirName
    set cwd [pwd]
    switch [file pathtype $d] {
	absolute  -
	volumerelative {
	    set path {}
	    set len 0
	}

	relative {
	    set path [file split $cwd]
	    set len [llength $path]
	}
    }
    foreach c [file split $d] {
	switch $c {
	    {.} {}

	    {..} {
		incr len -1;
		set path [lrange $path 0 [expr $len-1]]
	    }

	    default {
		lappend path $c
		incr len
	    }
	}
    }
    Debug_Leave
    return [eval file join $path]
}

# Try to make the directory writable

proc TekiMakeWritable {dir} {
    global tcl_platform tcl_version
    set majorversion [lindex [split $tcl_version .] 0]
    switch $tcl_platform(platform) {
      unix {
        if {$majorversion >= 8} {
            file attributes $dir -permissions 755
        } else {
            exec chmod a+wx $dir
        }
      }

      mac* -
      windows {
        if {$majorversion >= 8} {
            file attributes $dir -readonly 0
        } else {
            error "Permission denied"
        }
      }
    }
}

# Make sure the directory exists, is writable, and is a directory.
#   If $dir exists and is a file, flag an error
#   If $dir doesn't exist, ask the user if they want to create it
#       if they do, then try to create it, otherwise flag an error
# If the return value is 0, then the directory can't be used
# for installation.  If the return value is 1, then we can proceed

proc TekiVerifyDir {dir} {
    #
    # Make the directory if needed
    #
    if {[file exists $dir]} {
        if {![file isdirectory $dir]} {
            TekiError "Error: $dir exists and is not a directory"
            return 0
        }
    } else {
        # Doesn't exist. Make it
        set rv [TekiWarning "Directory $dir does not exist, create?" 0 {Ok Cancel}]
        if {$rv == 0} {
            if [catch {file mkdir $dir} err] {
                TekiError "Error creating $dir\n  $err"
                return 0
            }
        } else {
            # Cancelled by user
            return 0
        }
    }

    # If we get to here, $dir exists and is a directory
    # Try to make it writable if it's not
    if {![file writable $dir]} {
        if [catch {TekiMakeWritable $dir} err] {
            TekiError "Error: $dir is not writable\n$err"
            return 0
        }
    }

    return 1
}

# Utility to delete an item from a list

proc TekiListDelete {list item} {
    set index [lsearch $list $item]
    if {$index != -1} {
        set list [concat [lrange $list 0 [expr $index-1]] \
                       [lrange $list [expr $index+1] end]]
    }
    return $list
}


# The (Teki*Wizard) procedures encode the logic of the wizards.
# Each procedure corresponds to one wizard.  The information gathered
# through the wizards is used to set elements in the global array TekiInfo()
#
# The entry point to the machine is the TekiTypeWizard (abbreviated
# "type").  At any time, pressing the cancel button aborts the wizards,
# pressing "finish" goes directly to "verify", pressing "back" goes back
# to the previous state, and, generally, pressing "next" goes on to
# the next state.  The only exception is in the initial wizard where
# the user selects between "custom" and "typical" installation.  If
# the user selects "typical" and selects "next", the installation
# parameters are reset to their defaults and the wizard goes directly
# to the verify state.
#
# To encode this logic, a sequence of procedure calls are executed, one
# for each wizard.  Each wizard calls the next wizard directly.  To
# implement the "back" button, the procedure just returns.  To implement
# the "cancel" and "finish" buttons, unusual return codes
# ($TekiInfo(cancelCode) for cancel, $TekiInfo(finishCode) for finish)
# are returned by the wizard procedures and caught at the entry point
# of the first wizard.
#
# The wizards are:
#    type - get installation type (custom or typical)
#    packages - let the user select which packages to install (only used
#               if there's more than one package available)
#    archList - get the list of architectures to install for
#    extras - ask whether to install examples, docs, and data
#    codeDir - get target directory for code files
#    docDir - get target directory for documentation (only if installing
#             documentation)
#    verify - verify all paramters

set TekiInfo(cancelCode)  5
set TekiInfo(finishCode)  6

# Reset the TekiInfo variables that corespond to the wizard parameters
# to their default values.

proc TekiWizardDefaults {} {
    global TekiInfo newPackage

    # reset to defaults
    set TekiInfo(packages) $newPackage(defaultPackages)
    set TekiInfo(archList) $newPackage(defaultArch)
    if {$TekiInfo(archList) == "all"} {
        set TekiInfo(archList) {}
        foreach sys $TekiInfo(systemNames) {
            lappend TekiInfo(archList) [lindex $sys 0]
        }
    }
    set TekiInfo(extras) {}
    if $newPackage(defaultInstallDoc) {
        lappend TekiInfo(extras) doc
    }
    if $newPackage(defaultInstallData) {
        lappend TekiInfo(extras) data
    }
    if $newPackage(defaultInstallExamples) {
        lappend TekiInfo(extras) examples
    }
    set TekiInfo(codeDir) [info library]
    set x [file dirname [file dirname $TekiInfo(codeDir)]]
    set TekiInfo(docDir) [file join $x doc]
}

proc TekiTypeWizard {} {
    global TekiInfo
    # Ask if typical or custom install
    while (1) {
        set rv [Wise_Radiolist "Select installation type" \
                         {{typical typical 1} {custom custom 0}} 1]
        switch $rv {
            {next typical} -
            {finish typical} {
                TekiWizardDefaults 
                TekiVerifyWizard
            }
            {next custom} {
                TekiPackagesWizard
            }
            {finish custom} {
                TekiVerifyWizard
            }
            cancel {
                return -code $TekiInfo(cancelCode)
            }
            default {
                TekiError "Internal error:\n  $rv\ndoesn't match case in switch"
            }
        }
    }
}

proc TekiPackagesWizard {} {
    global TekiInfo newPackage
    set packages $newPackage(available)

    #
    # If there's only one package, just pretend they hit "next"
    #
    if {[llength $packages] != 1} {
        while (1) {
            set choices {}
            foreach pkg $packages {
                if {[lsearch $TekiInfo(packages) $pkg] == -1} {
                    lappend choices [list $newPackage($pkg,name) $pkg 0]
                } else {
                    lappend choices [list $newPackage($pkg,name) $pkg 1]
                }
            }
            set rv [Wise_Checklist "Select packages to install" $choices]
            switch [lindex $rv 0] {
                next {
                    set TekiInfo(packages) [lrange $rv 1 end]
                    TekiArchListWizard
                }
                back {
                    return
                }
                finish {
                    set TekiInfo(packages) [lrange $rv 1 end]
                    TekiVerifyWizard
                }
                cancel {
                    return -code $TekiInfo(cancelCode)
                }
            }
        }
    } else {
        set TekiInfo(packages) $packages
        TekiArchListWizard
    }
}

proc TekiArchListWizard {} {
    global TekiInfo newPackage

    set objFileCount 0
    foreach pkg $TekiInfo(packages) {
        incr objFileCount [llength $newPackage($pkg,objFiles)]
    }
    if {$objFileCount == 0} {
        TekiExtrasWizard
        return
    }

    while (1) {
        #
        # We need to rebuild the choices list each time through the
        # loop so we can retain the users setting when they go on
        # to the next wizard and then hit the "back" button.  If
        # we don't rebuild it, then the wizard contents might
        # be out of sync with TekiInfo(archList), since this is
        # changed on next/finish
        #
        set choices {}
        foreach pkg $TekiInfo(packages) {
            foreach pair $newPackage($pkg,objFiles) {
                set name [lindex $pair 0]
                if {[lsearch $choices [list $name]*] == -1} {
                    if {[lsearch $TekiInfo(archList) $name] == -1} {
                        lappend choices [list $name $name 0]
                    } else {
                        lappend choices [list $name $name 1]
                    }
                }
            }
        }

        set rv [Wise_Checklist "Select architectures to install" $choices]
        switch [lindex $rv 0] {
            next {
                set TekiInfo(archList) [lrange $rv 1 end]
                TekiExtrasWizard
            }
            back {
                return
            }
            finish {
                set TekiInfo(archList) [lrange $rv 1 end]
                TekiVerifyWizard
            }
            cancel {
                return -code $TekiInfo(cancelCode)
            }
        }
    }
}

proc TekiExtrasWizard {} {
    global TekiInfo newPackage

    #
    # Figure out if there are any extras to install!  If not, skip
    # this step.
    #
    set docFileCount 0
    set dataFileCount 0
    set exampleFileCount 0
    foreach pkg $TekiInfo(packages) {
        incr docFileCount [llength $newPackage($pkg,docFiles)]
        incr dataFileCount [llength $newPackage($pkg,dataFiles)]
        incr exampleFileCount [llength $newPackage($pkg,exampleFiles)]
    }
    if {$docFileCount == 0} {
        set TekiInfo(extras) [TekiListDelete $TekiInfo(extras) doc]
    }
    if {$dataFileCount == 0} {
        set TekiInfo(extras) [TekiListDelete $TekiInfo(extras) data]
    }
    if {$exampleFileCount == 0} {
        set TekiInfo(extras) [TekiListDelete $TekiInfo(extras) example]
    }
    if {($docFileCount + $dataFileCount + $exampleFileCount) == 0} {
        TekiCodeDirWizard
        return
    }

    while (1) {
        set choices {}
        if $docFileCount {
            lappend choices [list "Install documentation" doc \
                                [expr [lsearch $TekiInfo(extras) doc] != -1]]
        }
        if $dataFileCount {
            lappend choices [list "Install data files" data \
                                [expr [lsearch $TekiInfo(extras) data] != -1]]
        }
        if $exampleFileCount {
            lappend choices [list "Install examples" examples \
                                [expr [lsearch $TekiInfo(extras) examples] != -1]]
        }
        set rv [Wise_Checklist "Select Extras to install" $choices]
        switch [lindex $rv 0] {
            next {
                set TekiInfo(extras) [lrange $rv 1 end]
                TekiCodeDirWizard
            }
            back {
                return
            }
            finish {
                set TekiInfo(extras) [lrange $rv 1 end]
                TekiVerifyWizard
            }
            cancel {
                return -code $TekiInfo(cancelCode)
            }
        }
    }
}

proc TekiCodeDirWizard {} {
    global TekiInfo auto_path env tcl_pkgPath
    while (1) {
        # Give the user several standard options of where to install
        # the code:
        #   option #1: the current value of the codeDir
        #   option #2: [info library]
        #   option #3: the TCLLIBPATH environment variable
        #   option #4: the parent of [info library]
        #   option #5: all the elements of tclPkgPath
        #   option #6: all the elements of auto_path
        #   option #7: "other"
        # The logic makes sure each choice only appears once.
        set choices {}
        set dir [TekiNormalizeDirName $TekiInfo(codeDir)]
        lappend choices [list $dir $dir 1]
        set dir [TekiNormalizeDirName [info library]]
        if {[lsearch $choices [list $dir $dir ?]] == -1} {
            lappend choices [list $dir $dir 0]
        }
        if [info exists env(TCLLIBPATH)] {
            set dir [TekiNormalizeDirName $env(TCLLIBPATH)]
            if {[lsearch $choices [list $dir $dir ?]] == -1} {
                lappend choices [list $dir $dir 0]
            }
        }
        set dir [TekiNormalizeDirName [info library]/..]
        if {[lsearch $choices [list $dir $dir ?]] == -1} {
            lappend choices [list $dir $dir 0]
        }
        foreach dir $tcl_pkgPath {
            set dir [TekiNormalizeDirName $dir]
            if {[lsearch $choices [list $dir $dir ?]] == -1} {
                lappend choices [list $dir $dir 0]
            }
        }
        foreach dir $auto_path {
            set dir [TekiNormalizeDirName $dir]
            if {[lsearch $choices [list $dir $dir ?]] == -1} {
                lappend choices [list $dir $dir 0]
            }
        }
        lappend choices [list other other 0]

        set rv [Wise_Radiolist "Select root directory for code files" $choices]

        #
        # If they choose other, let them chose a directory.
        # After we have the candidate directory, make sure it
        # exists and is writable.
        #
        set option [lindex $rv 0]
        set rv [lrange $rv 1 end]
        switch $option {
            next -
            finish {
                if {$rv == "other"} {
                    set dir [Wise_GetDirName $dir]
                    if {$dir == ""} {
                        continue;   # try again....
                    }
                } else {
                    set dir $rv
                }

                #
                # Warn them if they're installing in a non-standard place
                #
                set found 0
		foreach d $auto_path {
		    set nd [TekiNormalizeDirName $d]
		    if [string match $nd $dir] {
			set found 1
			break
		    }
		}
		if {!$found} {
		    foreach d $tcl_pkgPath {
			set nd [TekiNormalizeDirName $d]
			if [string match $nd $dir] {
			    set found 1
			    break
			}
		    }
		}
		if {!$found} {
                    TekiWarning "Warning: you are installing in a non-standard location
Be sure to add $dir to your TCLLIBPATH environment variable" 0 Ok
                }

                if {![TekiVerifyDir $dir]} {
                    continue
                }

                set TekiInfo(codeDir) $dir
                if {$option == "next"} {
                    TekiDocDirWizard
                } else {
                    TekiVerifyWizard
                }
            }

            back {
                return
            }

            cancel {
                return -code $TekiInfo(cancelCode)
            }
        }
    }
}

proc TekiDocDirWizard {} {
    global TekiInfo
    if {[lsearch $TekiInfo(extras) doc] == -1} {
        TekiVerifyWizard
        return
    }
    while (1) {
        # Give the user several standard options of where to install
        # the documentation:
        #   option #1: the current value of the docDir
        #   option #2: [info library]/../../doc
        #   option #3: $codeDir/doc
        #   option #4: "other"
        # The logic makes sure each choice only appears once.

        set choices {}
        set dir [TekiNormalizeDirName $TekiInfo(docDir)]
        lappend choices [list $dir $dir 1]
        set dir [TekiNormalizeDirName [info library]/../../doc]
        if {[lsearch $choices [list $dir $dir ?]] == -1} {
            lappend choices [list $dir $dir 0]
        }
        set dir [TekiNormalizeDirName $TekiInfo(codeDir)/doc]
        if {[lsearch $choices [list $dir $dir ?]] == -1} {
            lappend choices [list $dir $dir 0]
        }
        lappend choices [list other other 0]

        set rv [Wise_Radiolist "Select root directory for documentation files" $choices]

        set option [lindex $rv 0]
        set rv [lrange $rv 1 end]
        switch $option {
            next -
            finish {
                if {$rv == "other"} {
                    set dir [Wise_GetDirName $dir]
                    if {$dir == ""} {
                        continue;   # try again....
                    }
                } else {
                    set dir $rv
                }

                if {![TekiVerifyDir $dir]} {
                    continue
                }
                set TekiInfo(docDir) $dir
                TekiVerifyWizard
            }

            back {
                return
            }

            cancel {
                return -code $TekiInfo(cancelCode)
            }
        }
    }
}

proc TekiVerifyWizard {} {
    global TekiInfo newPackage
    set installDoc [expr [lsearch $TekiInfo(extras) doc] != -1]
    set installData [expr [lsearch $TekiInfo(extras) data] != -1]
    set installExamples [expr [lsearch $TekiInfo(extras) examples] != -1]
    set msg "Install these packages:\n"
    foreach pkg $TekiInfo(packages) {
        set msg [format "%s    %s %s\n" $msg \
                     $newPackage($pkg,name) \
                     $newPackage($pkg,version)]
    }
    set msg [format "%s\n...for these architectures:\n" $msg]
    foreach arch $TekiInfo(archList) {
        set msg [format "%s    %s\n" $msg $arch]
    }
    set msg [format "%s\nInstall code in \n    %s\n" $msg $TekiInfo(codeDir)]
    if {$installDoc} {
        set msg [format "%s\nInstall documentation in \n    %s\n" \
                     $msg $TekiInfo(docDir)]
    }
    if {$installExamples} {
        set msg [format "%s\nInstall examples in \n    %s\n" \
                     $msg $TekiInfo(codeDir)]
    }
    if {$installData} {
        set msg [format "%s\nInstall data files in \n    %s\n" \
                     $msg $TekiInfo(codeDir)]
    }
    set rv [TekiWarning $msg 0 {Finish {< Back} Cancel}]
    if {$rv == 0} {
        return -code $TekiInfo(finishCode)
    }
    if {$rv == 2} {
        return -code $TekiInfo(cancelCode)
    }
}

#
# Display copyrights for all selected packages.
# Returns a list of packages for which the user accepted the copyright
#
proc TekiDisplayCopyrights {pkgList} {
    global tclPkgInfo TekiInfo newPackage auto_path env

    Debug_Enter TekiDisplayCopyrights
    set packages {}
    set accept 0
    set dontaccept 1
    set acceptrest 2
    set abort 3
    set code 0
    foreach {pkg fn} $pkgList {
        if {($code != $acceptrest) &&
            [file exists $fn]} {
            set f [open $fn r]
            set msg [read $f]
            close $f
            set name $newPackage($pkg,name)
            set code [Wise_Message "Copyright for $name" \
                            $msg {Accept {Do not Accept} {Accept Rest} Abort}]
            if {$code == $accept} {
                lappend packages $pkg
            }
            if {$code == $acceptrest} {
                lappend packages $pkg
            }
            if {$code == $dontaccept} {
                TekiWarning "Skipping installation of $name" 0 {Ok}
            }
            if {$code == $abort} {
                TekiWarning "Aborting installation" 0 {Ok}
                return {}
            }
        } else {
            lappend packages $pkg
        }
    }
    Debug_Leave
    return $packages
}

# The following procedure is called by DoFileInstall and
# DoWWWBrowse to
#  1. Read/verify the teki file
#  2. Gather the installation parameters
#  3. Fetch & display copyright notices
#  4. Install the extension
#
# It does a web based install if $web is true, a file system
# based install if $web if false

proc TekiInteractiveInstall {tekiFile web} {
    global tclPkgInfo TekiInfo newPackage auto_path env
    Debug_Enter TekiInteractiveInstall

    # 1. Read and verify it

    catch {unset newPackage}
    set code [catch {TekiReadFile $tekiFile} err] 
    if $code {
        TekiError "Error reading $tekiFile\n  $err\nerrorCode = $code"
        catch {unset newPackage}
        return
    }
    set code [catch {TekiVerifyFile} err] 
    if $code {
        TekiError "Invalid Teki file $tekiFile:\n$err"
        catch {unset newPackage}
        return
    }

    # 2. Collect all the information from the user.

    TekiWizardDefaults
    set code [catch TekiTypeWizard err]
    if {$code == $TekiInfo(cancelCode)} {
        return
    }
    if {$code != $TekiInfo(finishCode)} {
        TekiError $err
        return
    }

    # 3. Fetch/display all the copyright notices.  If web based install,
    # download the copyright files.  The wwwCleanup variable is a list
    # of all the files in tmpdir that we have to delete when we're done.

    set fileList {}
    set nocopyright {}
    set tmpdir $TekiInfo(tmpDir)
    set index 0
    if $web {
        foreach pkg $TekiInfo(packages) {
            set inFile $newPackage($pkg,copyright)
            set srcURL $newPackage($pkg,srcURL)
            set outFile [file join $tmpdir copyright$index]
            if [string length $inFile] {
                if [catch {TekiDownloadFile $srcURL/$inFile $outFile} err] {
                    TekiWarning "Couldn't find copyright file\n$srcURL/$inFile\n$err\n\nskipping package" 0 {Ok}
                } else {
                    lappend fileList $pkg $outFile
                }
            } else {
                lappend nocopyright $pkg
            }
        }
    } else {
        foreach pkg $TekiInfo(packages) {
            set srcDir $newPackage($pkg,srcDir)
            set copyright $newPackage($pkg,copyright)
            set inFile [file join [file dirname $tekiFile] $srcDir $copyright]
            if [string length $copyright] {
                if {![file exists $inFile]} {
                    TekiWarning "Couldn't find copyright file\n$inFile\n\nskipping package" 0 {Ok}
                } else {
                    lappend fileList $pkg $inFile
                }
            } else {
                lappend nocopyright $pkg
            }
        }
    }

    set TekiInfo(packages) [concat $nocopyright [TekiDisplayCopyrights $fileList]]
    if $web {
        foreach file $fileList {
            catch {file delete -force $file}
        }
    }
    if {$TekiInfo(packages) == ""} {
        Debug_Leave
        return;
    }

    # 4. Install it!

    set installDoc [expr [lsearch $TekiInfo(extras) doc] != -1]
    set installData [expr [lsearch $TekiInfo(extras) data] != -1]
    set installExamples [expr [lsearch $TekiInfo(extras) examples] != -1]

    TekiInstall [file dirname $tekiFile] $web \
                $TekiInfo(codeDir) $TekiInfo(docDir) \
                $TekiInfo(packages) $TekiInfo(archList) \
                $installData $installDoc $installExamples 

    TekiGetAllPkgInfo
    TekiUpdateBrowser
    catch {unset newPackage}
    Debug_Leave
}

#-----------------------------------------------------------------------
#
# File menu callbacks -- 
#
# TekiDoFileInstall is called when the user select the Install item in the
#    File menu
# TekiDoFileUninstall is called when the user select the Uninstall item in the
#    File menu
# TekiDoFileExit is called when the user select the Exit item in the File menu

#
# TekiDoFileInstall -- called from the "File->Install" menu item.
#


proc TekiDoFileInstall {} {
    global tclPkgInfo TekiInfo newPackage auto_path env
    Debug_Enter TekiDoFileInstall

    catch {unset newPackage}
    # Get the teki install file
    set file [tk_getOpenFile -title "Open TEKI file" -filetypes {{{Tek Files} .tek} {{All Files} *}}]
    if {$file == ""} {
        return;
    }
    TekiInteractiveInstall $file 0
    Debug_Leave
}

#
# TekiDoFileUninstall {} {
#
#   Uninstall the current package, update UI
#
proc TekiDoFileUninstall {} {
    global tclPkgInfo tcl_library TekiInfo
    Debug_Enter TekiDoFileUninstall

    set packages {}
    foreach pkg $TekiInfo(currPackage) {
        set name $tclPkgInfo($pkg,name)
        if {($name == "Tcl") || ($name == "Tk")} {
            tk_dialog .error "Error" "Can't uninstall $pkg" {} 0 Ok
            continue
        }
        lappend packages $pkg
    }
    TekiUninstall $packages
    TekiGetAllPkgInfo
    TekiUpdateBrowser
    set w $TekiInfo(Browser)
    $w selection set end
    set info [$w get end]
    set TekiInfo(currPackage) [lindex $info 0]
    TekiUpdateReadme
        
    Debug_Leave
}

proc TekiDoFileExit {} {
   Debug_Enter TekiDoFileExit
   exit
}

#-----------------------------------------------------------------------
#
# WWW menu callbacks -- 
#

#
# Update the currently selected package.
#    1. Fetching the .tek file using the package's the URL field
#    2. Report the current version number and the new version number.
#       Ask the user if they want to proceed.
#    3. If so, copy all the package files into a temporary directory
#       and install from there
#
proc TekiDoWWWUpdate {} {
    global tclPkgInfo TekiInfo

    Debug_Enter TekiDoWWWUpdate
    Debug_Leave
}

proc TekiWWWBrowseInstall {} {
    global TekiInfo
    set tmpDir $TekiInfo(tmpDir)

    # Gather files for a batch install, and generate/use a bundle file
    set filelist {}
    set pkgList {}
    set index 0
    foreach i [.toc.list curselection] {
        if {$i == 0} {
            # skip labels :-)
            continue
        }

        # The first element of the selection is the package number, 
        # which is the index in TekiInfo(contents).  Retrieve it so
        # we know which package the user selected.
        set info [.toc.list get $i]
        set index [lindex $info 0]
        incr index -1
        set tuple [lindex $TekiInfo(contents) $index]
        set name [lindex $tuple 0]
        set inFile [lindex $tuple 5]
        set pkg [lindex $tuple 6]
        set tmpDir $TekiInfo(tmpDir)
        set outFile [file join $tmpDir tekiTmp$index]
        if [catch {TekiDownloadFile $inFile $outFile} err] {
            set rv [TekiWarning "Couldn't install package $name\nError occured while downloading\n$inFile\n$err" 0 {Ok Cancel}]
            if {$rv == 0} {
                foreach f $filelist {
                    file delete -force $f
                }
                return
            }
        } else {
            lappend filelist $outFile
            lappend pkgList $pkg
            incr index
        }
    }
    if {[llength $filelist] == 0} {
        TekiWarning "No valid packages found to install" 0 {Ok}
        return
    }
    set bundleFile [file join $tmpDir tekiBundle.tek]
    set f [open $bundleFile w]
    puts $f {# TekiFile 1.0
# Automatically generated TEKI bundle file
}
    foreach file $filelist {
        puts $f "TekiReadFile $file"
    }
    puts $f "set newPackage(defaultPackages) [list $pkgList]"
    close $f
    TekiInteractiveInstall $bundleFile 1
    file delete -force $bundleFile
    foreach f $filelist {
        file delete -force $f
    }
}

proc TekiWWWBrowseReadme {} {
    global TekiInfo
    set packages {}
    set tmpDir $TekiInfo(tmpDir)
    foreach i [.toc.list curselection] {
        if {$i == 0} {
            # skip labels :-)
            continue
        }

        # The first element of the selection is the package number, 
        # which is the index in TekiInfo(contents).  Retrieve it so
        # we know which package the user selected.
        set info [.toc.list get $i]
        set index [lindex $info 0]
        incr index -1
        set tuple [lindex $TekiInfo(contents) $index]
        set name [lindex $tuple 0]
        set ver [lindex $tuple 1]
        set url [lindex $tuple 4]
        if {[string length $url] == 0} {
            set rv [TekiWarning "No information available on $name $ver" 0 {Ok Cancel}]
            if {$rv == 1} {
                return
            }
            continue
        }

        set outFile [file join $tmpDir readme]
        if [catch {TekiDownloadFile $url $outFile} err] {
            set rv [TekiWarning "Coulnd't find readme for $name $ver:\n  $err" 0 {Ok Cancel}]
            if {$rv == 1} {
                return
            }
            continue
        }

        set f [open $outFile r]
        set msg [read $f]
        close $f
        file delete -force $outFile
        set code [Wise_Message "Information for $name" $msg {Ok Cancel}]
        if {$code == 1} {
            return
        }
    }
}


proc TekiWWWBrowseAll {} {
    global TekiInfo
    .toc.list delete 0 end
    set str [format "%-4s%-11s%-8s     %-15s%-28s" "#" "package" "version" "requires" "description"]
    .toc.list insert end $str
    set i 1
    foreach tuple $TekiInfo(contents) {
        set pkg [lindex $tuple 0]
        set ver [lindex $tuple 1]
        set req [lindex $tuple 2]
        set des [lindex $tuple 3]
        .toc.list insert end [format "%-4d%-15s%-8s%-16s%-28s" $i $pkg $ver $req $des]
        incr i
    }
}

proc TekiWWWBrowseCompat {} {
    global tcl_version tk_version TekiInfo
    .toc.list delete 0 end
    set str [format "%-4s%-11s%-8s     %-15s%-28s" "#" "package" "version" "requires" "description"]
    .toc.list insert end $str
    set i 1
    foreach tuple $TekiInfo(contents) {
        set sat 1
        foreach pair [lindex $tuple 2] {
            set pkg [lindex $pair 0]
            set ver [lindex $pair 1]
            Debug_Print $pkg $ver
            if {($pkg == "Tcl") && ![package vsatisfies $tcl_version $ver]} {
                set sat 0
            }
            if {($pkg == "Tk") && ![package vsatisfies $tk_version $ver]} {
                set sat 0
            }
        }
        if $sat {
            set pkg [lindex $tuple 0]
            set ver [lindex $tuple 1]
            set req [lindex $tuple 2]
            set des [lindex $tuple 3]
            .toc.list insert end [format "%-4d%-15s%-8s%-16s%-28s" $i $pkg $ver $req $des]
        }
        incr i
    }
}

proc TekiDoWWWBrowse {} {
    global TekiInfo
    set url $TekiInfo(browseURL)
    set tmpDir $TekiInfo(tmpDir)
    set outFile [file join $tmpDir contents.tkr]
    if [catch {TekiDownloadFile $url $outFile} err] {
        TekiWarning "Couldn't find browse information file\n$err" 0 {Ok}
        return
    }
    TekiReadTOCFile $outFile
    file delete -force $outFile


    # Ok, TekiInfo(contents) should contain a list of
    #   name  version  requires descript  readme-url  tek-file-url

    if {[info commands .toc] == ""} {
        toplevel .toc
        set w .toc
        set numButtons 5
        button $w.install -text Install -command TekiWWWBrowseInstall
        button $w.readme -text "View Readme" -command TekiWWWBrowseReadme
        button $w.showall -text "Show All Packages" -command TekiWWWBrowseAll
        button $w.showcompat -text "Show Compatible Packages" -command TekiWWWBrowseCompat
        button $w.cancel -text "Close" -command "wm withdraw .toc"
        scrollbar $w.scroll -command "$w.list yview"
        listbox $w.list -font $TekiInfo(fixedfont) -relief sunken -width 100 \
                     -bd 2 -yscrollcommand "$w.scroll set" -setgrid 1 -height 12 \
                     -selectmode extended
        grid $w.scroll -column $numButtons -row 0 -sticky ns
        grid $w.list -column 0 -columnspan $numButtons -row 0 -sticky nsew
        grid $w.install -column 0 -row 1
        grid $w.readme -column 1 -row 1
        grid $w.showall -column 2 -row 1
        grid $w.showcompat -column 3 -row 1
        grid $w.cancel -column 4 -row 1 -columnspan 2
    }
    Wise_CenterWindow .toc
    wm transient .toc .
    TekiWWWBrowseAll
}

#-----------------------------------------------------------------------
#
# WWW support -- 
#
# This section contains code to download files and directories from
# a URL into a temp file.

#
# Get a decent value for the temporary directory
#
switch $tcl_platform(platform) {
    windows    {set TekiInfo(tmpDir) C:/TEMP}
    unix       {set TekiInfo(tmpDir) /tmp}
}
if {[info exists env(TMP)] && [file isdirectory $env(TMP)]} {
    set TekiInfo(tmpDir) $env(TMP)
}
if {[info exists env(TMPDIR)] && [file isdirectory $env(TMPDIR)]} {
    set TekiInfo(tmpDir) $env(TMPDIR)
}
if {[info exists env(TEMP)] && [file isdirectory $env(TEMP)]} {
    set TekiInfo(tmpDir) $env(TEMP)
}
if {[info exists env(TEMPDIR)] && [file isdirectory $env(TEMPDIR)]} {
    set TekiInfo(tmpDir) $env(TEMPDIR)
}
set TekiInfo(browseURL)   http://www2.cs.cornell.edu/zeno/bsmith/contents.tkc

# -----------------------------------------------------------------------
#
# Routines to read and verify .tek files
#

# Todo:
#    Bullet-proof this section using a safe interpreter
#

proc TekiReadTOC1.0 {filename} {
    global TekiInfo
    if [catch {source $filename} err] {
        global errorCode errorInfo
        error "Error reading version 1.0 TEKI TOC file $filename.
Execute \"source $filename\" in a Tcl shell to find error" \
$errorInfo $errorCode
    }
}

proc TekiReadTOCFile {filename} {
    Debug_Enter TekiReadTOCFile 
    set f [open $filename r]
    set x [split [gets $f]]
    close $f
    if {([llength $x] != 3) ||
        [string compare [lindex $x 0] #] ||
        [string compare [lindex $x 1] TekiTOC] } {
        error "Invalid Teki Table-of-contents file"
    }
    set tekiFileVersion [lindex $x 2]
    if [catch {TekiReadTOC$tekiFileVersion $filename} err] {
        global errorCode errorInfo
        error $err $errorInfo $errorCode
    }
    Debug_Leave
}

proc TekiReadVersion1.0 {filename} {
    global newPackage
    if [catch {source $filename} err] {
        global errorCode errorInfo
        error "Error reading version 1.0 TEKI file $filename.
Execute \"source $filename\" in a Tcl shell to find error" \
$errorInfo $errorCode
    }
}

proc TekiReadFile {filename} {
    Debug_Enter ReadTekiFile 
    set cwd [pwd]
    cd [file dirname $filename]
    set filename [file tail $filename]
    set f [open $filename r]
    set x [split [gets $f]]
    close $f
    if {([llength $x] != 3) ||
        [string compare [lindex $x 0] #] ||
        [string compare [lindex $x 1] TekiFile] } {
        cd $cwd
        error "Invalid Teki file"
    }
    set tekiFileVersion [lindex $x 2]
    if [catch {TekiReadVersion$tekiFileVersion $filename} err] {
        global errorCode errorInfo
        cd $cwd
        error $err $errorInfo $errorCode
    }
    cd $cwd
    Debug_Leave
}

proc TekiVerifyFile {} {
    global newPackage
    foreach attr {available defaultPackages defaultArch
              defaultInstallDoc defaultInstallExamples defaultInstallData} {
        if ![info exists newPackage($attr)] {
            error "Teki file does not define attribute '$attr'"
        }
    }
    foreach pkg $newPackage(available) {
        foreach attr {name version description requires updateURL srcURL
                      registerURL srcDir destDir infoFile tclFiles dataFiles
                      docDir docFiles exampleFiles objFiles copyright} {
            if ![info exists newPackage($pkg,$attr)] {
                error "Package $pkg does not define attribute '$attr'"
            }
        }
    }
    foreach pkg $newPackage(defaultPackages) {
        if ![info exists newPackage($pkg,name)] {
            error "Teki file defines invalid package $pkg in as part of defaultPackages"
        }
    }
}

# ---------------------------------------------------------------------- -
#
# TekiInstall
#
# Install a package.
# The file has been read and verified, and the packages variable refers
# to good packages (they exist in newPackage, haven't already been
# installed, etc).  The errors we have to worry about are errors copying
# files.

#
# TekiDownloadFile --
#
#   Copy $url into $tmpDir/$file, making directories as needed
#
# Throws an error if user presses cancel button during download
# or another error occurs.
# The caller must delete the file when done.

proc TekiCancelDownload {} {
    global TekiInfo
    wm withdraw .dl
    http_reset $TekiInfo(token)
}

proc TekiDownloadProgressEnd {} {
    wm withdraw .dl
}

proc TekiDownloadProgressStart {url} {
    global TekiInfo
    if {[info commands .dl] == ""} {
        toplevel .dl
        label .dl.l -font $TekiInfo(fixedfont)
        scale .dl.s -orient horizontal -sliderrelief flat \
                    -sliderlength 0 -showvalue 0
        button .dl.cancel -text Cancel -command TekiCancelDownload
        grid .dl.l -row 0 -column 0 -sticky ew
        grid .dl.s -row 1 -column 0 -sticky ew
        grid .dl.cancel -row 2 -column 0
    } else {
        .dl.s configure -sliderlength 0
    }
    wm title .dl "Downloading $url"
    .dl.l configure -text $url
    Wise_CenterWindow .dl
}

proc TekiDownloadProgress {token max curr} {
    global TekiInfo
    set TekiInfo(token) $token
    if {$max > 0} {
        set width [winfo width  .dl.s]
        set pixels [expr int($curr*$width/$max)]
        .dl.s configure -sliderlength $pixels
        update idletasks
    }
}

proc TekiDownloadFile {url dest} {
    global errorInfo errorCode
    file mkdir [file dirname $dest]
    set f [open $dest w]
    fconfigure $f -translation binary
    TekiDownloadProgressStart $url
    set code [catch {http_get $url -channel $f -progress TekiDownloadProgress} token]
    TekiDownloadProgressEnd
    close $f
    if {$code} {
        catch {file delete -force $dest}
        error "HTTP error:\n  $token\n\nwhile fetching\n  $url" \
              $errorInfo $errorCode
    }
    upvar #0 $token state

    # The variable state(http) contains the server response
    # code.  If the code is [45]xx, there's a problem with the
    # URL.
    if {[string match {[45]*} [lindex $state(http) 1]]} {
        catch {file delete -force $dest}
        set err [lrange $state(http) 2 end]
        error "HTTP error:\n  $err\n\nwhile fetching\n  $url" \
    }

    # Handle URL redirects
    if {$state(status) == "reset"} {
        file delete -force $dest
        uplevel #0 unset $token
        error "Connection reset by user"
    }
    foreach {name value} $state(meta) {
        if {[regexp -nocase ^location$ $name]} {
            file delete -force $dest
            TekiDownloadFile [string trim $value] $dest
        }
    }
    uplevel #0 unset $token
}

#
# Copy from $src to $dest
# Make directories as needed
# Download file is source is a URL
# Example:
#   src = http://www2.cs.cornell.edu/bsmith/tmp/tcl-files/foo.tcl
#   dest = /usr/local/lib/tcl/dp4.0/tcl-files/foo.tcl
# 
proc TekiCopy {src dest} {
    file mkdir [file dirname $dest]
    if [string match http://* $src] {
        TekiDownloadFile $src $dest
    } else {
        file copy $src $dest
    }
}

#
# TekiInstall --
#
# Install the packages passed in.  Can be called
# with through the command line or the Teki GUI.
#
# Parameters:
#
#     dirBase -- directory where teki file is located.  Other
#                file (code, etc) are given by the srcDir relative
#                to this directory
#     web -- if true if we're doing a Web-based install and the
#            $srcURL for each package is used to locate files to copy.
#            If false, $dirBase/$srcDir for each package gives the
#            place where the source files are located.
#     codePrefix -- prefix for installing code, data, and examples.  For
#                   example, /usr/local/lib/tcl
#     docPrefix -- prefix for installing documentation.  (e.g., /usr/man)
#     packages -- list of packages to install (members of newPackage(installed))
#     archList -- list of archtectures, (members of TekiInfo(systemNames))
#     installData -- Boolean indicating whether to install data files
#     installDoc -- Boolean indicating whether to install documentation
#     installExamples -- Boolean indicating whether to install examples
#
# Notes
#     Installs documentation in $docPrefix/$newPackage(destDir)
#     Installs tcl files, object files, data, and examples in
#          $codePrefix/$newPackage(destDir)
#   
#
proc TekiInstall {dirBase web codePrefix docPrefix packages archList
                {installData 1} {installDoc 1} {installExamples 1}} {
    global newPackage tcl_platform TekiInfo errorCode errorInfo

    Debug_Enter TekiInstall

    if {![file isdirectory $codePrefix]} {
        TekiError "Error: Destination directory $codePrefix doesn't exist"
        return
    }

    foreach pkg $packages {

        if $web {
            set srcDir $newPackage($pkg,srcURL)
        } else {
            set srcDir [file join $dirBase $newPackage($pkg,srcDir)]
        }

        set steps {
            "Making directory"
            "Creating pkgIndex.tcl"
            "Information file"
            "Tcl files"
        }
        if {$installData && [string length $newPackage($pkg,dataFiles)]} {
            lappend steps "Data files"
        }
        if {$installExamples && [string length $newPackage($pkg,exampleFiles)]} {
            lappend steps "Examples"
        }
        if {$installDoc && [string length $newPackage($pkg,docFiles)]} {
            lappend steps "Documentation"
        }
        foreach pair $newPackage($pkg,objFiles) {
            set arch [lindex $pair 0]
            if {[string match "all" $archList] ||
                ([lsearch -exact $archList $arch] != -1)} {
                lappend steps "Files for $arch"
            }
        }
        Progress_StepInit "Installing $pkg" $steps
        Undo_Add $pkg Progress_StepEnd



        # Make the destination directory.  If it exists, verify that
        # we should replace it

        set destDir [file join $codePrefix $newPackage($pkg,destDir)]
        if [file isdirectory $destDir] {
            set rval [TekiWarning \
                "Warning: Directory $destDir already exists.
Delete will replace it
Cancel will abort the installation of this package
Abort will abort the installation of this and all remaining packages" \
                1 {Delete Cancel Abort}]
            if {$rval == 2} {
                Undo_All $pkg
                break
            }
            if {$rval == 1} {
                Undo_All $pkg
                continue
            }
            if [catch {file delete -force $destDir} err] {
            set rval [TekiWarning \
                "Warning: Couldn't delete $destDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages" \
                0 {Cancel Abort}]
                Undo_All $pkg
                if {$rval == 0} {
                    continue
                } else {
                    break
                }
            }
        }
        Progress_StepPrint



        # Create $destDir, open $destDir/pkgIndex.tcl, and start writing it...

        set fn [file join $destDir pkgIndex.tcl]
        if [catch {file mkdir [file dirname $fn]} err] {
            set rval [TekiWarning \
                "Couldn't create target directory $destDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages" \
                0 {Cancel Abort}]
            Undo_All $pkg
            if {$rval == 0} {
                continue
            } else {
                break
            }
        }
        Undo_Add $pkg "file delete -force \"$destDir\""

        if [catch {set pkgIndex [open $fn w]} err] {
            set rval [TekiWarning \
                "Couldn't create pkgIndex.tcl in $destDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages" \
                0 {Cancel Abort}]
            Undo_All $pkg
            if {$rval == 0} {
                continue
            } else {
                break
            }
        }
        Undo_Add $pkg "close $pkgIndex"

        puts $pkgIndex "
# This file was automatically generated by TEKI
global tclPkgInfo
if {!\[info exists tclPkgInfo(installed)\] ||
    \[lsearch \$tclPkgInfo(installed) $pkg\] == -1} {
    lappend tclPkgInfo(installed) $pkg

    set tclPkgInfo($pkg,codePrefix)      [list $codePrefix]
    set tclPkgInfo($pkg,docPrefix)       [list $docPrefix]
    set tclPkgInfo($pkg,installData)     $installData
    set tclPkgInfo($pkg,installDoc)      $installDoc
    set tclPkgInfo($pkg,installExamples) $installExamples

    set tclPkgInfo($pkg,name)            [list $newPackage($pkg,name)]
    set tclPkgInfo($pkg,version)         [list $newPackage($pkg,version)]
    set tclPkgInfo($pkg,description)     [list $newPackage($pkg,description)]
    set tclPkgInfo($pkg,requires)        [list $newPackage($pkg,requires)]
    set tclPkgInfo($pkg,tekiFile)        [list $newPackage($pkg,tekiFile)]
    set tclPkgInfo($pkg,updateURL)       [list $newPackage($pkg,updateURL)]
    set tclPkgInfo($pkg,registerURL)     [list $newPackage($pkg,registerURL)]
    set tclPkgInfo($pkg,destDir)         [list $newPackage($pkg,destDir)]
    set tclPkgInfo($pkg,tclFiles)        [list $newPackage($pkg,tclFiles)]
    set tclPkgInfo($pkg,systemNames)     [list $TekiInfo(systemNames)]"

        Progress_StepPrint




        # Copy in all the platform independent files to $destDir
        # Preserve the package directory hierarchy

        set infoFile $newPackage($pkg,infoFile)
        if [string length $infoFile] {
            if [catch {TekiCopy $srcDir/$infoFile $destDir/$infoFile} err] {
                set rval [TekiWarning \
                    "Couldn't copying\n$infoFile\nto\n$destDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages
Ignore will ignore this error and continue installation" \
                    0 {Cancel Abort Ignore}]
                if {$rval == 0} {
                    Undo_All $pkg
                    continue
                }
                if {$rval == 1} {
                    Undo_All $pkg
                    break
                }
                set f {}
            } else {
                set f [file join $destDir $newPackage($pkg,infoFile)]
            }
        } else {
            set f {}
        }
        puts $pkgIndex "    set tclPkgInfo($pkg,infoFile)        [list $f]"
        Progress_StepPrint

        set code 2
        foreach file $newPackage($pkg,tclFiles) {
            if [catch {TekiCopy $srcDir/$file $destDir/$file} err] {
                set code [TekiWarning \
                    "Error copying\n$file\nto\n$destDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages" \
                    0 {Cancel Abort}]
                break;
            }
        }
        if {$code == 0} {
            Undo_All $pkg
            continue
        }
        if {$code == 1} {
            Undo_All $pkg
            break
        }
        Progress_StepPrint

        set dataFiles {}
        if $installData {
            set dataFiles $newPackage($pkg,dataFiles)
            foreach file $dataFiles {
                if [catch {TekiCopy $srcDir/$file $destDir/$file} err] {
                    set code [TekiWarning \
                        "Error copying data file\n$file\nto\n$destDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages
Ignore will ignore this error and continue installation" \
                        0 {Cancel Abort Ignore}]
                    if {$code != 2} {
                        break
                    }
                }
            }
            if {$code == 0} {
                Undo_All $pkg
                continue
            }
            if {$code == 1} {
                Undo_All $pkg
                break
            }
            Progress_StepPrint
        }
        puts $pkgIndex "    set tclPkgInfo($pkg,dataFiles) [list $dataFiles]"

        set exampleFiles {}
        if $installExamples {
            set exampleFiles $newPackage($pkg,exampleFiles)
            foreach file $exampleFiles {
                if [catch {TekiCopy $srcDir/$file $destDir/$file} err] {
                    set code [TekiWarning \
                        "Error copying\n$file\nto\n$destDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages
Ignore will ignore this error and continue installation" \
                        0 {Cancel Abort Ignore}]
                    if {$code != 2} {
                        break
                    }
                }
            }
            if {$code == 0} {
                Undo_All $pkg
                continue
            }
            if {$code == 1} {
                Undo_All $pkg
                break
            }
            Progress_StepPrint
        }
        puts $pkgIndex "    set tclPkgInfo($pkg,exampleFiles) [list $exampleFiles]"

        set docFiles {}
        set docSrcDir $srcDir/$newPackage($pkg,docDir)
        set docDestDir [file join $docPrefix $newPackage($pkg,destDir)]
        if $installDoc {
            #
            # Make the destination directory.  If it exists, verify that
            # we should replace it
            #
            if [file isdirectory $docDestDir] {
                set rval [TekiWarning \
                    "Warning: Directory $docDestDir already exists.
Delete will replace it
Cancel will abort the installation of this package
Abort will abort the installation of all packages" \
                    1 {Delete Cancel Abort}]
                if {$rval == 1} {
                    Undo_All $pkg
                    continue
                }
                if {$rval == 2} {
                    Undo_All $pkg
                    break
                }
                if [catch {file delete -force $docDestDir} err] {
                    set code [TekiWarning \
                        "Error: Couldn't delete $docDestDir:\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages
Ignore will ignore this error and continue installation" \
                        0 {Cancel Abort Ignore}]
                    if {$code == 0} {
                        Undo_All $pkg
                        continue
                    }
                    if {$code == 1} {
                        Undo_All $pkg
                        break
                    }
                }
            }



            # Create $docDestDir

            if [catch {file mkdir $docDestDir} err] {
                set code [TekiWarning \
                    "Error creating documentation target directory $docDestDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages
Ignore will ignore this error and continue installation" \
                        0 {Cancel Abort Ignore}]
                if {$code == 0} {
                    Undo_All $pkg
                    continue
                }
                if {$code == 1} {
                    Undo_All $pkg
                    break
                }
            } else {
                Undo_Add $pkg "file delete -force \"$docDestDir\""
            }


            # Copy all the documentation files
            
            foreach file $newPackage($pkg,docFiles) {
                if [catch {TekiCopy $docSrcDir/$file $docDestDir/$file} err] {
                    set code [TekiWarning \
                        "Error copying\n$file\nto\n$docDestDir\n$err
Cancel will abort the installation of this package
Abort will abort the installation of all packages
Ignore will ignore this error and continue installation" \
                        0 {Cancel Abort Ignore}]
                    break
                }
            }
            if {$code == 0} {
                Undo_All $pkg
                continue
            }
            if {$code == 1} {
                Undo_All $pkg
                break
            }
            Progress_StepPrint
        }




        # Copy in all the platform dependent files
        # If we get an error and have to abort in the middle,
        # we'll throw an error to break out of the nested
        # loops.

        set installedArchs {}
        set code  [catch {
          foreach pair $newPackage($pkg,objFiles) {
              set system [lindex $pair 0]
              #
              # system is a name for the system, such as Solaris
              # one Windows.   $archList is either the
              # string "all" or a list of these names
              #
              if {[string match "all" $archList] ||
                  ([lsearch -exact $archList $system] != -1)} {
                  set fileList {}
                  lappend installedArchs $system
                  foreach file [lindex $pair 1] {
                      if [catch {TekiCopy $srcDir/$file $destDir/$file} err] {
                          set rval [TekiWarning \
                              "Error copying\n$file\nto\n$destDir\n$err\n
Cancel will abort the installation of this package
Abort will abort the installation of all packages
Ignore will ignore this error and continue installation" \
                          0 {Cancel Abort Ignore}]
                          switch $rval {
                            0 {
                              Undo_All $pkg
                              error {} 5;  # continue
                            }

                            1 {
                              Undo_All $pkg
                              error {} 6;  # break
                            }

                            2 {}
                          }
                      }
                      lappend fileList $file
                  }
                  Progress_StepPrint
                  puts $pkgIndex "    set tclPkgInfo($pkg,objFiles,$system) [list $fileList]"
              }
          }
        } err]
        switch $code {
            0 {}
            5 continue
            6 break
            default {
                error $err $errorInfo $errorCode 
            }
        }
        puts $pkgIndex "    set tclPkgInfo($pkg,archList) [list $installedArchs]"

        # Finish up!

#        puts $pkgIndex "    TekiPackageInit $pkg"

        puts $pkgIndex "}"
	  set tclfilesdir "$codePrefix/$pkg"
	  set outstr [concat package ifneeded dp 4.0 \[list LoadLib $tclfilesdir\]]
	  puts $pkgIndex $outstr
	  puts $pkgIndex "proc LoadLib dir {"
	  set outstr [concat foreach file \[glob \$dir/library/*.tcl\]]
	  puts -nonewline $pkgIndex "    $outstr"
	  puts $pkgIndex " {"
	  set outstr [concat uplevel #0 source \[list \$file\]]
	  puts $pkgIndex "		$outstr"
	  puts $pkgIndex "	}"

	  set outstr [concat uplevel #0 load \[list \$dir/$file\]]
	  puts $pkgIndex "	$outstr"
	  puts $pkgIndex "}"
        close $pkgIndex
        Undo_Clear $pkg
        Progress_StepEnd
    }
    TekiGetAllPkgInfo
    Debug_Leave
}

#
# TekiUninstall --
#
# Uninstall all the packages passed in.  Can be called with through the
# command line or the Teki GUI.
#
proc TekiUninstall {pkgList} {
    global tclPkgInfo
    set noWarn 0
    foreach pkg $pkgList {
        if {!$noWarn} {
            set rval [TekiWarning "Uninstall package $pkg ?" \
                0 {Yes {Yes All} Cancel}]
            if {$rval == 2} {
                return
            }
            if {$rval == 1} {
                set noWarn 1
            }
        }

        # Delete all the code/data/example files.
        # These are stored under the $codePrefix directory
        set codePrefix $tclPkgInfo($pkg,codePrefix)
        set destDir [file join $codePrefix $tclPkgInfo($pkg,destDir)]
        if [catch {file delete -force $destDir} err] {
            TekiError "Error: Couldn't delete $destDir:\n$err\nAborting uninstallation"
            break
        }

        #
        # delete doc files
        #
        if {$tclPkgInfo($pkg,installDoc)} {
            set docPrefix $tclPkgInfo($pkg,docPrefix)
            set dir [file join $docPrefix $tclPkgInfo($pkg,destDir)]
            if [catch {file delete -force $dir} err] {
                TekiError "Error: Couldn't delete $dir:\n$err\nAborting uninstallation"
                break
            }
        }
    }
}

# ---------------------------------------------------------------------
#
# Support procedures for all extensions.  The following two procedures
# are used at runtime to source/load the right files (including architecture
# dependent files).  TekiPackageInit is called by the pkgIndex.tcl file
# with the package name, after the tclPkgInfo variables have been set up.
# It determines if the OS is supported, and if so, makes the right calls
# to package ifneeded.  TekiPackageSetup is called when package require
# is called.  It loads all the files (including architecture dependent
# files
#

#
# Predefined system names
#
set TekiInfo(systemNames)   {
        {Solaris SunOS 5*}
        {SunOS SunOS 4*}
        {HPUX HP* 9*}
        {Linux Linux* 2*}
        {FreeBSD FreeBSD* 2*}
        {Win95/NT Win* *}
}

proc TekiPackageInit {pkg} {
    global tclPkgInfo tcl_platform

    set os $tcl_platform(os)
    set ver $tcl_platform(osVersion)

    set system {}
    foreach tuple $tclPkgInfo($pkg,systemNames) {
        set osPattern [lindex $tuple 1]
        set verPattern [lindex $tuple 2]
        if {[string match $osPattern $os] &&
            [string match $verPattern $ver]} {
            set system [lindex $tuple 0]
            break;
        }
    }
    if [string length $system] {
        set name $tclPkgInfo($pkg,name)
        set version $tclPkgInfo($pkg,version) 
        package ifneeded $name $version "TekiPackageSetup $pkg $system"
    }
}

proc TekiPackageSetup {pkg system} {
    global tclPkgInfo
    set currDir [pwd]
    set prefix $tclPkgInfo($pkg,codePrefix)
    set destDir [file join $prefix $tclPkgInfo($pkg,destDir)]
    cd $destDir
    set tclFiles $tclPkgInfo($pkg,tclFiles)
    if [info exists tclPkgInfo($pkg,objFiles,$system)] {
        set objFiles $tclPkgInfo($pkg,objFiles,$system)
    } else {
        set objFiles {}
    }
    foreach f $tclFiles {
        catch {uplevel #0 "source $f"}
    }
    foreach f $objFiles {
        if [string match *.tcl $f] {
            catch {uplevel #0 "source $f"}
        } else {
            catch {uplevel #0 "load $f"}
        }
    }
    cd $currDir
}

proc TekiGetAllPkgInfo {} {
    global tcl_pkgPath auto_path tclPkgInfo tcl_version tk_version
    catch {unset tclPkgInfo}
    set tclver tcl$tcl_version
    set tclPkgInfo(installed) $tclver
    set tclPkgInfo($tclver,name)           Tcl
    set tclPkgInfo($tclver,requires)       {}
    set tclPkgInfo($tclver,version)        $tcl_version
    set tclPkgInfo($tclver,description)    "Tcl core"
    set tclPkgInfo($tclver,infoFile)       {}

    if [info exists tk_version] {
        set tkver tk$tk_version
        lappend tclPkgInfo(installed)         $tkver
        set tclPkgInfo($tkver,name)           Tk
        set tclPkgInfo($tkver,requires)       [list [list Tcl $tcl_version]]
        set tclPkgInfo($tkver,version)        $tk_version
        set tclPkgInfo($tkver,description)    "Tk core"
        set tclPkgInfo($tkver,infoFile)       {}
    }

    #
    # Get list of all pkgIndex.tcl files
    #
    set fileList {}
    foreach dir [concat $auto_path [list $tcl_pkgPath]]  {
        if {![catch {glob [file join $dir pkgIndex.tcl] [file join $dir * pkgIndex.tcl]} files]} {
            set fileList [concat $files $fileList]
        }
    }
    set code 0
    foreach file $fileList {
        if [catch {source $file} err] {
            if {$code != 1} {
                set code [TekiWarning \
                    "Error sourcing $file:\n  $err?" 0 {Ignore {Ignore All} Abort}]
            }
            if {$code == 2} {
                exit
            }
        }
    }
}

# ---------------------------------------------------------------------
#
# Initialization code
#

TekiGetAllPkgInfo
if $TekiInfo(gui) {
    TekiCreateUI
    wm deiconify .
}

if {$tcl_version < 8.0} {
    proc fcopy args {
        eval unsupported0 $args
    }
}
