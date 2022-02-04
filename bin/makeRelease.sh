#! /bin/bash
# Make the Tcl-DP source code release.
#
# To release tcldp:

# 1. Change these variables to establish the version you want to release
majorVersion=4
minorVersion=2
patchVersion=0

# 2. Check in to CVS, and tag the repository with the tag,
#      tcldp-majorVersion-minorVersion-patchVersion
#    For example,
#      cvs ci
#      cvs tag tcldp-4-2-0

# 3. Run this script:
#      bin/makeRelease.sh
#    You will be asked for a password.  Just specify no password by hitting Enter.
#    The .tgz and .zip release files will appear in your current working directory.


releaseName=tcldp-${majorVersion}.${minorVersion}.${patchVersion}
tagName=tcldp-${majorVersion}-${minorVersion}-${patchVersion}

sourceReleaseFiles="
api \
bin \
CHANGES \
CMakeLists.txt \
compat \
doc \
dplite \
dp.tek \
dunix_patch \
examples \
FAQ \
generic \
library \
LICENSE \
man \
nameserver \
README \
services \
tekilib \
teki.tcl \
tests \
TODO \
unix \
win"

startingDir=$PWD
rm -rf makeReleaseTemp
mkdir  makeReleaseTemp
cd     makeReleaseTemp

echo "When prompted for the password to anonymous@tcldp.cvs.sourceforge.net, just hit Enter."
cvs -d:pserver:anonymous@tcldp.cvs.sourceforge.net:/cvsroot/tcldp login
cvs -d:pserver:anonymous@tcldp.cvs.sourceforge.net:/cvsroot/tcldp export -r $tagName -d $releaseName tcldp

# Make a TGZ file for UNIX
tar cvfz ${startingDir}/${releaseName}.tgz --exclude compat $releaseName

# Convert text files to DOS.  Use this command to find all the binary files
# that should be excluded according to CVS:
#        find . -name Entries -exec grep /-kb/ '{}' \;
find $releaseName \
	\! -iname '*.doc' \
	\! -iname '*.dsw' \
	\! -iname '*.gif' \
	\! -iname '*.mbt' \
	\! -iname '*.mrt' \
	\! -iname '*.pdf' \
	\! -iname '*.png' \
	-exec unix2dos -k '{}' \;
# Delete those nutty u2dtmp files left by unix2dos
find $releaseName -name 'u2dtmp*' -exec rm -f '{}' \;

# Make a ZIP file for Windows
zip -r ${startingDir}/${releaseName}.zip $releaseName -x compat
