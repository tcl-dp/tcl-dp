# Tcl-DP Release Notes

## Tcl-DP 4.2

John McGehee, @jmcgeheeiv August 15, 2010

The last formal release of Tcl-DP was 4.0b2 in 1997.  Since then, both
Tcl and compute platforms have advanced.  Tcl-DP 4.2 is updated to the
latest Tcl and operating system versions, while maintaining compatibility
with Tcl-DP 4.0.

Summary of new features:

- Compatible with Tcl 8.3.2 through Tcl 8.5
- Compatible with Linux 2.6 (most throughly tested with Fedora 11)
- Completely new CMake 2.8 build, test, install and packaging system
- Ported unit tests to tcltest 2.2
- Fatal bug in the C-API fixed
- Updated web site http://tcldp.sourceforge.net, intended to supersede
  the original at
  http://www.cs.cornell.edu/Info/Projects/zeno/Projects/Tcl-dp.html
- C-API unit test added

Some unit tests fail. This might be a problem with the test alone:
  * On UNIX: ipm, email, ser_smit and serial tests fail
  * On Windows: ipm, udp, ser_smit and serial tests fail

Pending user feedback on the necessity of these features and the
Windows platform, these failing tests will be disregarded,  See
tests/README for more information.

Otherwise, Tcl-DP 4.2 functionality is the same as Tcl-DP 4.0.

## Tcl-DP 4.1

John McGehee, @jmcgeheeiv July 25, 2010

What I have written here supersedes anything in the README file.

The main purpose of Tcl-DP 4.1 is support for Tcl 8.

In 2003, Mike Gerdts uploaded UNIX and Windows source code tarballs for Tcl-DP.
The Source Forge tcldp project repository was initially created from these
files.

For the sake of organization, I call this Mike Gerdts 2003 code "version
4.1".  However, the source code itself still thinks it is "dp4.0".

Unfortunately there are some problems:

- Tcl-DP does not build with Tcl 8.5 or higher.  I used Tcl 8.4.19.
  - There are lots of compiler warnings
  - The C API does not work.  I fixed this, and these changes will appear in
    Tcl-DP 4.2.

### Comparison Between the SourceForge repository and and Mike Gerdts' Uploads

When Mike Gerdts uploaded tcldp-unix.tar.gz and tcldp-win.tar.gz (which I
have named "Tcl-DP 4.1", he was not quite sure about the relationship
between the files in the tarballs and the repository.  He wrote,

    This includes integration of any fixes found in (what I beleive to be) my
    latest source trees. The Windows one in particular needs lots of help
    because of carriage-return/newline issues. I believe that the code in the
    windows tar file will compile and work with MSVC++ 6. I believe that the
    unix tar ball is up to date with CVS and works on Linux and Solaris.
    Unfortunately I (or sourceforge) seem to be CVS impaired at the moment and
    can't do the comparison.

Mike included links to his tar files:
- http://tcldp.sourceforge.net/tcldp-unix.tar.gz
- http://tcldp.sourceforge.net/tcldp-win.tar.gz

The files in the tcldp project file manager folder `tcldp-4.1/` are exact copies
of the above files.

Since Mike expressed doubt as to the relationship between the tcldp project
repository and his tarballs, I compared them, and found no material differences
between Mike's `tcldp-unix.tar.gz`, Mike's `tcldp-win.tar.gz`, and Tcl-DP
release 4.1.

I happen to have the details of the differences at hand, so here they are.

tcldp-unix.tar.gz contains files that do not appear in the repository:

- tcldp-unix/generic/.#dpRPC.c.1.1.1.4
- tcldp-unix/unix/autom4te-2.53.cache
- tcldp-unix/unix/dpconfig.h
- tcldp-unix/unix/install
- tcldp-unix/unix/stamp-h1

Among these, only `tcldp-unix/unix/install` might be interesting.  I will leave it
out of the repository for now.

In tcldp-win.tar.gz, the newlines are DOS style, which is correct--I only saw
this difference because my repository working copy is on Linux.

## Tcl-DP 4.0b2   Nov 28, 1997

:warn: DP will now only compile on Tcl 7.6 and 8.0 release.

Summary of new features:

- Updated core socket code to support changes in Tcl 8.0 channels.  
  - This update forced major changes in the socket architecture.
  - Notice the new files in the non-portable directories.
- Drastically updated SPAM to TEKI.
- Integrated RPC race condition fix from <Paul-Harrington@deshaw.com>.
- Updated ftp example (hopefully it works! :)

## Tcl-DP 4.0b1   May 6, 1997

Summary of new features:

- Fixed some overall DP problems with extraneous files, docs, and
  Makefile bugs.
- Fixed some broken parts of TCP.  Linger, addressing, etc.
- Added a C API library for communication between a C-based client and a
  Tcl-DP server.  Consider this alpha quality software and please send
  us bug reports.
- Added a "Help!" FAQ to the distribution in order to answer those
  questions we get asked all the time...

## Tcl-DP 4.0a2   March 3, 1997

Summary of new features:

- Ran DP through Purify 4.0 on every OS.  Fixed alot of bugs.  :)
- Fixed many bugs with the SPAM installation.  Please let us know if you
  continue to have problems.
- Fixed several bugs in the RPC/RDO code and library procedures.
  Broken connection caused client to spin out of control - FIXED.
  RDO callback/onerror did not work because dp_rpcFile was not being set
- Fixed a bug or two in the filter and serial port code.
- Added several new channel filters including uuencode/decode and
  simple xor encryption/decryption.
- Updated ftp example so now it has a chance of working.
- Shrank SendRPCMessage() by about an order of a magnitude.

## Released Tcl-DP 4.0a1   February 17, 1997

Summary of new features:

- Completely rewrote Tcl-DP source, test suite and docs.
- Ported library files.
