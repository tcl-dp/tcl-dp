# Tcl-DP Distributed Programming for Tcl

Tcl-DP adds TCP, UDP, IP-multicast connection management,
remote procedure call (RPC), serial communication and distributed object
protocols to Tcl.

A C interface to the RPC primitives is also provided in the `api/`
directory.

See the [Release Notes](CHANGES.md) for the version history.

## Building and Installing on UNIX

Download and install Tcl 8.3.2 or above.  Tcl 8.4.19 and Tcl 8.5.6 are
the most heavily tested.

Your Linux distribution may already have CMake 2.8.  If not, download
and install [CMake 2.8 or above](http://www.cmake.org).

Download the Tcl-DP tarball and unarchive it:
```
tar xvfz tcldp-4.2.0.tgz 
```

Change to the `tcldp4.2.0/` directory:
```
cd tcldp-4.2.0
```

With CMake it is recommended that you build in a directory other than
the source tree (a new directory within the source tree is fine).
For example:
```
mkdir Release    # Your choice of directory name
cd  Release
```

Run cmake, specifying the directory containing `CMakeLists.txt`, which in
this example is `..`:
```
cmake ..
```

This does basically the same thing as the familiar `./configure`.

The above will set up the build to link against the default installation
of Tcl on your system.  You can specify another Tcl with the `TCL_HOME`
variable:
```
cmake -DTCL_HOME:PATH=/home/voom/tcl8.4.19 ..
```

To build debug code,
```
cd ..          # Return to tcldp-4.2.0/ (or anywhere else you like)
mkdir Debug    # Your choice of directory name
cd Debug
cmake -DCMAKE_BUILD_TYPE:STRING=Debug ..
```

To prepare to install in a location other than `/usr/local/`,
```
cmake -DCMAKE_INSTALL_PREFIX:PATH=/home/voom ..
```

The first time you run CMake, it caches the values of variables like
`TCL_HOME` and `CMAKE_BUILD_TYPE` in the file `CMakeCache.txt`, and uses
these values on subsequent runs.  Therefore, if you run CMake with
`TCL_HOME=~/tcl8.4.19`, and then you change your mind and want to run CMake
with `TCL_HOME=~/tcl8.3.31`, you must either edit or delete `CMakeCache.txt`.
See [Running CMake](http://www.cmake.org/cmake/help/runningcmake.html)
for details.

CMake created a Makefile for you, so build using GNU Make:
```
make
```

Run the unit tests:
```
make test
```

Currently, the ipm (IP multicast) test is skipped.  The email, ser_xmit,
and serial tests fail.

If you want, you can re-run the tests with CTest to see more details:
```
ctest -V
```

Or you can run all tests except those whose name matches "rpc":
```
ctest -V -E rpc
```

Or run only the tests whose name matches "api":
```
ctest -V -R api
```

See the [test documentation](tests/README.md) for more about the current state
of the unit tests.

Install in `/usr/local/dp42/`.  This requires root (sudo) privilege:
```
sudo make install
```

or if you want to install somewhere other than `/usr/local/`, run CMake with
variable `CMAKE_INSTALL_PREFIX` as explained above.

Test the installation:
```
tclsh ../tests/all.tcl  -constraints isInstallTest -notfile "api.test  email.test ipm.test ser_xmit.test serial.test"
```

See the [test documentation](tests/README.md) for more about the current state
of the unit tests.

## How to Build and Install on Windows

Tcl-DP builds and installs on Windows, but a Windows expert really needs
to have a look at the unit test failures.

Download and install Microsoft Visual Studio.  Visual Studio 10 Express
is the most heavily tested.

Download and install Tcl 8.3.2 or above.  Tcl 8.4.16 is the most heavily
tested.

Download and install [CMake 2.8 or above](http://www.cmake.org).  Do not
launch CMake from the Windows Start Menu.  Follow the instructions below.

Download and unzip `tcldp-4.2.0.zip` to a folder.  In the following discussion,
it is presumed to be unzipped to your
`My Documents\Visual Studio 2010\Projects\tcldp-4.2.0\` folder.

Launch a Visual Studio Command Prompt by selecting
**Start > All Programs > Microsoft Visual Studio 2010 Express > Visual Studio Command Prompt (2010)**
This command prompt's environment is set up for the Visual Studio tools.

Launch CMake from the Visual Studio Command Prompt by typing:
```
cmake-gui
```

A CMake GUI window will appear.

In the CMake window, set "Where is the source code" to the place where
you unzipped the Tcl-DP source code.  For example:
```
C:/Documents and Settings/voom/My Documents/Visual Studio 2010/Projects/tcldp-4.2.0
```

With CMake it is recommended that you build in a folder other than the
source tree (a new folder within the source tree is fine).  Set "Where
to build the binaries" to such a folder.  For example:
```
C:/Documents and Settings/voom/My Documents/Visual Studio 2010/Projects/tcldp-4.2.0/Release
```

Edit the CMake variables in the Name/Value pane of the CMake window:
  * You probably want to build an optimized release, so set
    `CMAKE_BUILD_TYPE` to `Release`  For a debugging build, set it
    to `Debug`.  See the
    [`CMAKE_BUILD_TYPE` documentation](http://www.cmake.org/cmake/help/cmake-2-8-docs.html#variable:CMAKE_BUILD_TYPE)
    for details.
  * If you want to link against a Tcl version other than what is
    installed in `C:\Program Files\Tcl`, press the **Add Entry** button
    and add a variable named `TCL_HOME` whose value is the path to
    the Tcl version you want to use.
  * If you will be installing Tcl-DP in a location other than
    `C:\Program Files`, set `CMAKE_INSTALL_PREFIX` to that path

Press the **Configure** button:

  * You will be asked if you want to create the build folder (`Release`
    in the above example).  Respond as you please.
  * Next CMake will ask you what generator (build system) to use.
    Specify `NMAKE Makefiles`.  See the `TODO` file for a discussion of
    why you cannot take advantage of the other tempting choices. 

If any of the variables remain highlighted in red, press the **Configure**
button again, and repeat until no variables are red.

Press the **Generate** button.  This creates the nmake Makefile.

Return to the Visual Studio Command Prompt.  Change your working directory
to the "Where to build the binaries" folder.  For example:
```
cd %HOMEPATH%
cd "My Documents\Visual Studio 2010\Projects\tcldp-4.2.0\Release
```

CMake created a Makefile for you, so build using nmake:
```
nmake
```

Run the unit tests:

```
nmake test
```
Currently, the rpc test hangs.  Hit Ctrl-C to escape.

If you want, you can re-run the tests with CTest to see more details:
```
ctest -V
```

Or you can run all tests except those whose name matches "rpc":
```
ctest -V -E rpc
```

Or run only the tests whose name matches "api":
```
ctest -V -R api
```

See `tests/README` for more details about the current state of the unit
tests.  In particular, the rpc test hangs.  Hit Ctrl-C to escape.

Currently the ipm (IP multicast) test hangs.  The email test is skipped.
ser_xmit and serial tests fail.  Maybe these features really do work,
but this is not so good.

Install in `C:\Program Files\dp42\`,
```
nmake install
```

or if you want to install somewhere other than `C:\Program Files`, set variable
`CMAKE_INSTALL_PREFIX` as explained above.

Test the installation:
```
tclsh ..\tests\all.tcl  -constraints isInstallTest -notfile "api.test email.test ipm.test rpc.test ser_xmit.test serial.test"
```

You may need to specify a complete path for tclsh, for example, by replacing
tclsh above with `C:\Program Files\Tcl\bin\tclsh84`.

See `tests/README` for more details about the current state of the unit
tests.

## Using Tcl-DP

Using Tcl-DP requires nothing more than wish or tclsh.  Just add
`package require dp` to Tcl scripts in which you want to use Tcl-DP.

Please see the FAQ before emailing us if you have problems using DP.

## Documentation

Documentation for the DP API and library functions is available in the
`doc/` subdirectory in standard HTML format.  You can use any web browser
to read the files.

In the `examples/` subdirectory, several sample applications are supplied
that use Tcl-DP.  As you can see from the examples, the distributed
programming mechanisms of Tcl-DP are very simple.  A `dp_RPC` command,
for example, sends a Tcl command to a remote process, which evaluates
the command in the destination Tcl interpreter and returns the result
as the value of the `dp_RPC` command.

## History

Tcl-DP was originally created at Cornell University. It was once
available at
~~http://www.cs.cornell.edu/Info/Projects/zeno/Projects/Tcl-dp.html~~,
but this site has since been taken down.

Then Mike Gerdts modified 1997 version 4.0b2 to support Tcl 8
and uploaded it as
[Tcl-DP 4.1 on SourceForge](https://sourceforge.net/projects/tcldp/files/tcldp-4.1/).

John McGehee updated Tcl-DP to Tcl 8.5 and Linux 2.6 and added a
new CMake build and test system. He released this as
[Tcl DP 4.2.0 on SourceForge](https://sourceforge.net/projects/tcldp/files/tcldp-4.2.0/).

Now, Tcl-DP has been ported to GitHub using version 4.2.0 as a starting
point.

See the [Release Notes](CHANGES.md) for the most detailed project history.

## Credits

Tcl-DP exists due to the efforts of Brian Smith, Mike Perham, Tibor
Janosi and Ioi Lam.

We extend our thanks to everyone who helped to create, debug, and
distribute this software.  Although there are too many people to mention
at this point, the following people deserve special attention:

- John Ousterhout, creator of Tcl/Tk
- Pekka Nikander, creator of tcpConnect
- Tim MacKenzie, creator of tclRawTCP
- Larry Rowe, co-creator of the original Tcl-DP;
- Lou Salkind, ported Tcl-DP to Tcl 7.0/Tk 3.3
- R. Lindsay Todd, developed security mechanism
- Peter Liu, developed extended name server code
- Ulla Bartsich, integrated many changes into Tcl-DP 3.3
- Mike Grafton, wrote extensive test suites for Tcl-DP 3.x
- Jon Knight, wrote IP-multicast extensions to Tcl-DP 3.x
- Gordon Chaffee, ported Tcl-DP 3 to Windows-NT
- John McGehee, added CMake build and test, upgraded to Tcl 8.5 and Linux 2.6
