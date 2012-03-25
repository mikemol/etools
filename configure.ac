AC_PREREQ([2.68])
AC_INIT([enttools], [1.0], [mikemol@gmail.com])
AC_CONFIG_SRCDIR([src/entwatch.c])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_INSTALL

AC_CHECK_FUNCS([atexit])
AC_CHECK_FUNCS([munmap])
AC_CHECK_FUNCS([strerror])
AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([string.h])
AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([unistd.h])
AC_FUNC_MMAP
AC_TYPE_SIZE_T

AC_CONFIG_FILES([Makefile
		 src/Makefile])

AC_OUTPUT

echo \
"-------------------------------------------------

 ${PACKAGE_NAME} Version ${PACKAGE_VERSION}

 Prefix: '${prefix}'.
 Compiler: '${CC} ${CFLAGS} ${CPPFLAGS}'

 Package features:
   Async Execution: ${async_exec}

 Now type 'make @<:@<target>@:>@'
   where the optional <target> is:
     all                - build all binaries
     install            - install everything

     check              - run unit tests
     dist               - build distribution tarball

     entwatch           - build entwatch
     entbuff		- build entbuff

--------------------------------------------------"
