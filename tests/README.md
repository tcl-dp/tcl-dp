# Tcp-DP Unit Tests

## Running the tests using CMake and CTest (UNIX and Windows)

The unit tests are defined in the `CMakeLists.txt` configuration file,
so the easiest way to run them is,
```
cd /path/to/build/directory
make test
```

To see more output, use CTest, which comes with CMake:
```
ctest -V
```

You can specify a subset of tests.  For example,
to run tests that match the string "rpc":
```
ctest -V -R rpc
```

## Running the Tests Using tclsh (UNIX)

If you prefer, you can also run tcltest using tclsh.  Assuming the source
directory to which you downloaded the source code is `../`, run all tests
on Linux,
```
export LD_LIBRARY_PATH=.:/path/to/tcl/lib
/path/to/tcl/bin/tclsh ../tests/all.tcl
# Exit status is the number of failed tests
```

However, setting `LD_LIBRARY_PATH` globally is bad form.  It is done here merely
to simplify these instructions.

Use the `-file` option to specify just one test:
```
/path/to/tcl/bin/tclsh ../tests/all.tcl -file rpc.test
# Exit status is the number of failed tests
```

If you dislike having `all.tcl` in the middle of this, you can run the
test directly,
```
/path/to/tcl/bin/tclsh ../tests/rpc.test
# Exit status is always 0
```

However, without `all.tcl`, you will not get a nonzero exit status to indicate
failure.

## Not All Tests Work in Tcl-DP 4.2

Most of the tests have been brought back to life, after apparently not
working for quite a while.  They work better on UNIX than Windows.

The following unit tests fail:
- On UNIX: ipm, email, ser_smit and serial tests fail
- On Windows: ipm, udp, ser_smit and serial tests fail
   
Pending user feedback on the necessity of these features and the Windows
platform, these failing tests will be disregarded.

# Tests Ported to Tcltest in Tcl-DP 4.2

The tests formerly used an ancestor of tcltest, that was defined in
file `tests/defs` (now removed).  This has been replaced by tcltest, the
standard test framework that is a part of Tcl.  The old test framework was
mercifully similar to tcltest.  Nonetheless, the tests look a bit unusual,
even when converted to tcltest.  For example, in the old test framework,
you had to concatenate the return code and return value into a list,
and then give a list containing the expected result:
```
test udp-1.4.5.2 {fconfigure udp} {
    list [catch {
	fconfigure $sock1 -sendBuffer
    } msg] $msg
} {0 4096}
```

Most tests were converted to tcltest by merely adding the `-body` and
`-result` options, but to save time, the peculiar
`{returnCode returnValue}` result list was retained:
```
test udp-1.4.5.2 {fconfigure udp} -body {
    list [catch {
    fconfigure $sock1 -sendBuffer
    } msg] $msg
} -result {0 4096}
```

The above test works, but it is complex and not very readable.

Whenever you add or modify a test, it would be best to use the more
readable standard tcltest style:
```
test udp-1.4.5.2 {fconfigure udp} -body {
    fconfigure $sock1 -sendBuffer
} -returnCodes 0 -result 4096
```

This particular test is an example of one of the more important functional
changes in the tests.  Sockets in modern operating systems have autotuned
buffer sizes.  Therefore this test has actually been changed to accept
any positive number:
```
test udp-1.4.5.2 {fconfigure udp} -body {
	fconfigure $sock1 -sendBuffer
} -returnCodes 0 -match regexp -result {[1-9][0-9]*}
```
