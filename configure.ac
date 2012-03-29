AC_PREREQ([2.67])
AC_INIT([enttools], [1.0], [mikemol@gmail.com])
AM_INIT_AUTOMAKE([1.11 silent-rules foreign subdir-objects])
AC_CONFIG_SRCDIR([src/entwatch.c])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC

AC_CONFIG_FILES([Makefile])

AC_OUTPUT

AC_MSG_NOTICE([
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
])