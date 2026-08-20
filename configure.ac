#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([osnshlpr-ssh], [090], [none])
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_SRCDIR([main.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h stddef.h stdlib.h string.h strings.h sys/stat.h sys/param.h sys/statvfs.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_STRUCT_ST_BLOCKS
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_REALLOC

# AC_CANONICAL_HOST is needed to access the 'host_os' variable    
AC_CANONICAL_HOST

build_linux=no
build_windows=no
build_mac=no

# Detect the target system
case "${host_os}" in
    linux*)
        build_linux=yes
        ;;
    cygwin*|mingw*)
        build_windows=yes
        ;;
    darwin*)
        build_mac=yes
        ;;
    *)
        AC_MSG_ERROR(["OS $host_os is not supported"])
        ;;
esac

# Pass the conditionals to automake
AM_CONDITIONAL([LINUX], [test "$build_linux" = "yes"])
AM_CONDITIONAL([WINDOWS], [test "$build_windows" = "yes"])
AM_CONDITIONAL([OSX], [test "$build_mac" = "yes"])

AC_CHECK_FUNCS([clock_gettime fdatasync ftruncate memmove memset mkdir realpath rmdir strchr strdup strerror strrchr strstr])
AC_CHECK_FUNCS([inotify_init], [AC_DEFINE([HAVE_INOTIFY], [1], [Check for inotify])])
AC_CHECK_FUNCS([statx],[AC_DEFINE([HAVE_STATX], [1], [Define if statx is available])], AC_MSG_NOTICE([Build without statx]))

AC_CHECK_HEADERS(stdio.h sys/param.h linux/fuse.h)
AC_CHECK_HEADER([pthread.h], [], [AC_MSG_ERROR([required header pthread.h not found])])
AC_CHECK_HEADER([security/pam_appl.h],[AC_DEFINE([HAVE_LIBPAM], [1], [Define if pam is available])], [AC_MSG_WARN([header pam_appl.h not found])])

AC_SEARCH_LIBS(pthread_create, pthread,, AC_MSG_ERROR([Please install pthreads library first.]))

# test the os

AS_IF([test LINUX], [

    AC_ARG_WITH([linux-epoll], [AS_HELP_STRING([--with-linux-epoll], [Enable building with epoll eventloop yes/no.])])

    AS_IF([ test "x$with_linux_epoll" = "xyes"], [

	AC_DEFINE([USE_LINUX_EPOLL], [1], [Define to use linux epoll])
	AC_MSG_NOTICE([Use the linux event poll subsystem.])

    ], [

	AS_IF([ test HAVE_GLIB2 ], [

	    AC_DEFINE([USE_GLIB_MAINLOOP], [1], [Define to use glib mainloop])
	    AC_MSG_NOTICE([Use the glib mainloop.])

	], [])

    ])

], [

    AS_IF([ test HAVE_GLIB2 ], [

	AC_DEFINE([USE_GLIB_MAINLOOP], [1], [Define to use glib mainloop])
	AC_MSG_NOTICE([Use the glib mainloop.])

    ], [])

])

# pkg

PKG_CHECK_MODULES([SYSTEMD], [libsystemd], [AC_DEFINE([HAVE_LIBSYSTEMD], [1], [Define libsystemd is available])], AC_MSG_ERROR([Error: libsystemd not found.]))
PKG_CHECK_MODULES(SQLITE3, [sqlite3], [AC_DEFINE([HAVE_SQLITE3], [1], [Define if sqlite 3 is available])], AC_MSG_ERROR([Error: sqlite 3 not found.]))
PKG_CHECK_MODULES(LIBARCHIVE, [libarchive], [AC_DEFINE([HAVE_LIBARCHIVE], [1], [Define if libarchive is available])], AC_MSG_ERROR([Error: libarchive not found.]))

# conditional

AM_CONDITIONAL([WITH_LINUX_EPOLL], [ test "x$with_linux_epoll" = "xyes"])
AM_CONDITIONAL([LIBSYSTEMD_AVAIL], [ test HAVE_LIBSYSTEMD ])
AM_CONDITIONAL([SQLITE_AVAIL], [ test HAVE_SQLITE3 ])
AM_CONDITIONAL([LIBARCHIVE_AVAIL], [ test HAVE_LIBARCHIVE ])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
