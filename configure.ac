AC_INIT(ttwatch, version-0.1)

AC_ARG_WITH(unsafe, [  --with-unsafe           enable unsafe functions])

AC_PROG_CC
AC_LANG(C)

AN_MAKEVAR([AR], [AC_PROG_AR])
AN_PROGRAM([ar], [AC_PROG_AR])
AC_DEFUN([AC_PROG_AR], [AC_CHECK_TOOL([AR], [ar], [ar])])
AC_PROG_AR

ok=1

AC_CHECK_HEADERS(getopt.h, [], [ok=0])
AC_CHECK_HEADERS(memory.h, [], [ok=0])
AC_CHECK_HEADERS(stdarg.h, [], [ok=0])
AC_CHECK_HEADERS(stdio.h, [], [ok=0])
AC_CHECK_HEADERS(stdlib.h, [], [ok=0])
AC_CHECK_HEADERS(string.h, [], [ok=0])
AC_CHECK_HEADERS(time.h, [], [ok=0])
AC_CHECK_HEADERS(unistd.h, [], [ok=0])
AC_CHECK_HEADERS(pwd.h, [], [ok=0])
AC_CHECK_HEADERS(fcntl.h, [], [ok=0])
AC_CHECK_HEADERS(sys/types.h, [], [ok=0])
AC_CHECK_HEADERS(sys/stat.h, [], [ok=0])

AC_CHECK_HEADERS(libusb-1.0/libusb.h, [], [ok=0])
AC_CHECK_LIB(usb-1.0, libusb_init, [], [ok=0])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <libusb-1.0/libusb.h>
]],
[[
#if LIBUSB_API_VERSION < 0x01000102
#error "LIBUSB Version too old"
#endif
]])],
[], [AC_MSG_ERROR([[The libusb version must be 1.0.16 or higher]])])

AC_CHECK_HEADERS(curl/curl.h, [], [ok=0])
AC_CHECK_LIB(curl, curl_easy_init, [], [ok=0])

if test "x$with_unsafe" = xyes; then
    AC_SUBST(unsafe, [-DUNSAFE])
else
    AC_SUBST(unsafe, [ ])
fi

if test "x$ok" = x1; then
    AC_OUTPUT(Makefile)
else
    AC_MSG_NOTICE([Errors prevent Makefile being generated])
fi

