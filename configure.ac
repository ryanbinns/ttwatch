AC_INIT(ttwatch, version-0.1)

AC_ARG_WITH(unsafe, [AS_HELP_STRING([--with-unsafe], [enable unsafe functions])])
AC_ARG_WITH(freebsd, [AS_HELP_STRING([--with-freebsd], [enable FreeBSD support])])

AC_PROG_CC
AC_LANG([C++])

AN_MAKEVAR([AR], [AC_PROG_AR])
AN_PROGRAM([ar], [AC_PROG_AR])
AC_DEFUN([AC_PROG_AR], [AC_CHECK_TOOL([AR], [ar], [ar])])
AC_PROG_AR

ok=1

AC_CHECK_HEADERS(ctype.h, [], [ok=0])
AC_CHECK_HEADERS(errno.h, [], [ok=0])
AC_CHECK_HEADERS(getopt.h, [], [ok=0])
AC_CHECK_HEADERS(math.h, [], [ok=0])
AC_CHECK_HEADERS(memory.h, [], [ok=0])
AC_CHECK_HEADERS(stdarg.h, [], [ok=0])
AC_CHECK_HEADERS(stddef.h, [], [ok=0])
AC_CHECK_HEADERS(stdio.h, [], [ok=0])
AC_CHECK_HEADERS(stdlib.h, [], [ok=0])
AC_CHECK_HEADERS(string.h, [], [ok=0])
AC_CHECK_HEADERS(time.h, [], [ok=0])
AC_CHECK_HEADERS(unistd.h, [], [ok=0])
AC_CHECK_HEADERS(pwd.h, [], [ok=0])
AC_CHECK_HEADERS(grp.h, [], [ok=0])
AC_CHECK_HEADERS(fcntl.h, [], [ok=0])
AC_CHECK_HEADERS(sys/types.h, [], [ok=0])
AC_CHECK_HEADERS(sys/stat.h, [], [ok=0])
AC_CHECK_HEADERS(string, [], [ok=0])
AC_CHECK_HEADERS(vector, [], [ok=0])

libusb_header_dir="/usr/include /usr/include/libusb-1.0 /usr/local/include /usr/local/include/libusb-1.0"

found_libusb_h=0
AC_CHECK_HEADERS(libusb.h, [found_libusb_h=1], [])
if test "x$found_libusb_h" = x0; then
    for dir in $libusb_header_dir; do
        AC_CHECK_HEADERS($dir/libusb.h, [found_libusb_h=1], [])
        if test "x$found_libusb_h" = x1; then
            CPPFLAGS="$CPPFLAGS -I$dir"
            break
        fi
    done
fi
if test "x$found_libusb_h" = x0; then
    ok=0
fi

libusb_library=usb-1.0
if test "x$with_freebsd" = xyes; then
    libusb_library=usb
    AC_MSG_WARN([FreeBSD mode enabled - skipping libusb version check])
fi

AC_CHECK_LIB($libusb_library, libusb_init, [], [ok=0])
if test "x$with_freebsd" != xyes; then
    AC_MSG_CHECKING([that the libusb version is 1.0.16 or higher])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <libusb.h>
#ifdef LIBUSB_API_VERSION
#   if LIBUSB_API_VERSION < 0x01000102
#       LIBUSB Version too old
#   endif
#else
#   ifdef LIBUSBX_API_VERSION
#       if LIBUSBX_API_VERSION < 0x01000102
#           LIBUSB Version too old
#       endif
#   else
#       Unable to determine LIBUSB version
#   endif
#endif
    ]],
    [[]])],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_RESULT([no])
     AC_MSG_ERROR([The libusb version must be 1.0.16 or higher])])
fi

AC_CHECK_HEADERS(curl/curl.h, [], [ok=0])
AC_CHECK_LIB(curl, curl_easy_init, [], [ok=0])

if test "x$with_unsafe" = xyes; then
    CFLAGS="$CFLAGS -DUNSAFE"
fi

if test "x$ok" = x1; then
    AC_OUTPUT(Makefile)
else
    AC_MSG_NOTICE([Errors prevent Makefile being generated])
fi

