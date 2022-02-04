# Test suite master control script for tcltest.  This file will run
# all test files PROJECT_SOURCE_DIR/tests/*.test, skipping those
# specified with ::tcltest::configure -notfile.
#
# John McGehee http://www.voom.net 7/26/2010

# CMake creates testconfig.tcl from testconfig.tcl.in, substituting CMake variables.
source [file join tests testconfig.tcl]
puts "Using Tcl-DP in '$softwareUnderTestDir'."

# Total failures across all test files for use as an exit status for this script
set totalNumTestsFailed 0

# Every time ::tcltest::cleanupTests runs, it runs cleanupTestsHook.
# Increment global variable totalNumTestsFailed by the number of tests
# in the current test file.
proc ::tcltest::cleanupTestsHook {} {
	global totalNumTestsFailed
	incr totalNumTestsFailed $::tcltest::numTests(Failed)
	puts "Encountered $totalNumTestsFailed failures so far."
}

# The *.test unit tests are in the source directory
set sourceDir [file dir [file dir [info script]]]
::tcltest::configure -testdir [file join $sourceDir tests]
::tcltest::configure -tmpdir [file join Testing Temporary]

# Skip these test files
# TODO: Enable and disable these tests automatically.
#       Also see testConstraint isIpm in testconfig.tcl.in.
::tcltest::configure -notfile {ipm.test}

eval ::tcltest::configure $argv

::tcltest::runAllTests

# Exit status is the number of test failures
exit $totalNumTestsFailed
