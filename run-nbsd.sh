#!/bin/sh
set -x
export DISPLAY=:0
export FREETYPE_CFLAGS="-I/usr/X11R7/include/freetype2"
export FREETYPE_LIBS="-Wl,-R/usr/X11R7/lib -L/usr/X11R7/lib -lfreetype"
export LDPATH="/usr/X11R7/lib:/usr/lib:/usr/pkg/lib"

if [ -x "/usr/pkg/bin/ccache" ]
then
    export CC="ccache gcc"
else
    export CC="gcc"
fi

if [ $(uname -m) = i386 ]
then
    ./configure CFLAGS='-g -Og' CPPFLAGS='-g -Og' --disable-tests --without-curses --with-freetype --with-png --without-cups --without-dbus --without-hal --without-ldap --x-includes=/usr/X11R7/include --x-libraries=/usr/X11R7/lib --without-pulse wine_cv_linux_gethostbyname_r_6=no ac_cv_header_resolv_h=no --prefix=/usr/pkg --mandir=/usr/pkg/man --build=i486--netbsdelf --host=i486--netbsdelf 
fi
gmake clean
gmake -j4
./wine ./programs/notepad/notepad.exe.so


