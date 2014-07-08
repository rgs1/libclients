AC_PREREQ(2.60)
AC_INIT([clients],
        [1],
        [rgs+clients@itevenworks.net],
        [clients],
        [http://www.github.com/rgs1/libclients/])
AC_CONFIG_SRCDIR([src/clients.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([
	check-news
	foreign
	1.11
	-Wall
	-Wno-portability
	silent-rules
	tar-pax
	subdir-objects
])
AC_PROG_CC_STDC
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])
LT_INIT([
	disable-static
	pic-only
])
AC_PREFIX_DEFAULT([/usr])

AC_PROG_SED
AC_PROG_MKDIR_P

AC_ARG_ENABLE([logging],
        AS_HELP_STRING([--disable-logging], [disable system logging @<:@default=enabled@:>@]),
        [], enable_logging=yes)
AS_IF([test "x$enable_logging" = "xyes"], [
        AC_DEFINE(ENABLE_LOGGING, [1], [System logging.])
])

AC_ARG_ENABLE([debug],
        AS_HELP_STRING([--enable-debug], [enable debug messages @<:@default=disabled@:>@]),
        [], [enable_debug=no])
AS_IF([test "x$enable_debug" = "xyes"], [
        AC_DEFINE(ENABLE_DEBUG, [1], [Debug messages.])
])

AC_CHECK_LIB([pthread], \
             [pthread_mutex_lock], \
             [have_pthread=yes], \
             [AC_MSG_ERROR([pthread library not found])] \
)

AC_CHECK_LIB([zookeeper_st], \
             [zookeeper_init], \
             [have_zookeeper=yes], \
             [AC_MSG_ERROR([zookeeper_st library not found])] \
)

AC_CHECK_FUNCS([ \
	__secure_getenv \
	secure_getenv\
])

dnl libsmall
prev_top_build_prefix=$ac_top_build_prefix
AX_CONFIG_DIR([libsmall])
ac_top_build_prefix=$prev_top_build_prefix

export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:"$ac_top_build_prefix"libsmall/src
PKG_CHECK_MODULES(LIBSMALL, libsmall >= 0.0.0)
AC_SUBST(LIBSMALL_CFLAGS)
AC_SUBST(LIBSMALL_LIBS)

# this should be configurable
ZK_HEADERS_PATH="/usr/include/zookeeper"

my_CFLAGS="\
-Wall \
-Wchar-subscripts \
-Wformat-security \
-Wmissing-declarations \
-Wmissing-prototypes \
-Wnested-externs \
-Wpointer-arith \
-Wshadow \
-Wsign-compare \
-Wstrict-prototypes \
-Wtype-limits \
-I${ZK_HEADERS_PATH} \
"
AC_SUBST([my_CFLAGS])

LDFLAGS="\
-lpthread \
-lzookeeper_st \
"

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES([
        Makefile
])

AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE $VERSION
        =====

        prefix:                 ${prefix}
        sysconfdir:             ${sysconfdir}
        libdir:                 ${libdir}
        includedir:             ${includedir}

        compiler:               ${CC}
        cflags:                 ${CFLAGS}
        ldflags:                ${LDFLAGS}

        logging:                ${enable_logging}
        debug:                  ${enable_debug}
])
