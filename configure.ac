m4_define([wkb_major], [0])
m4_define([wkb_minor], [0])
m4_define([wkb_micro], [2])
m4_define([wkb_version], [wkb_major.wkb_minor.wkb_micro])

AC_PREREQ([2.64])
AC_INIT([weekeyboard], [wkb_version])

AC_SUBST([weekeyboard_MAJOR],   [wkb_major])
AC_SUBST([weekeyboard_MINOR],   [wkb_minor])
AC_SUBST([weekeyboard_MICRO],   [wkb_micro])
AC_SUBST([weekeyboard_VERSION], [wkb_version])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11 -Wall parallel-tests foreign color-tests dist-bzip2])

AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC
AC_PROG_SED

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT([disable-static])

PKG_PROG_PKG_CONFIG()

PKG_CHECK_MODULES(WAYLAND, [wayland-client >= 1.2.0])
PKG_CHECK_MODULES(EFL, [eina evas ecore ecore-evas ecore-wayland edje])

AC_ARG_WITH(edje-cc,
            AS_HELP_STRING([--with-edje-cc=PATH], [Path to edje_cc binary]),
            [EDJE_CC_PATH=${withval}], [EDJE_CC_PATH=$($PKG_CONFIG --variable=prefix edje)/bin/edje_cc])
AC_SUBST([EDJE_CC_PATH])

PKG_CHECK_MODULES(IBUS, [eldbus eet efreet])
AC_CHECK_PROG([have_ibus], [ibus], [yes], [no])

AS_IF([ test "x$have ibus" = "xno" ],
      [ AC_MSG_ERROR([The ibus executable was not found.]) ])

IBUS_ADDR=`ibus address > /dev/null 2>&1`
AS_IF([ test $? -ne 0 ],
      [ AC_MSG_WARN([The ibus executable does not support 'address' argument.]) ])

WAYLAND_SCANNER_RULES(['$(top_srcdir)/protocol'])

CFLAGS="$CFLAGS -Wextra -Wno-unused-parameter"
AC_SUBST(CFLAGS)

CXXFLAGS="$CXXFLAGS -Wextra -Wno-unused-parameter"
AC_SUBST(CXXFLAGS)

AC_CONFIG_FILES([Makefile
		 src/Makefile
         data/themes/Makefile
		 protocol/Makefile])
AC_OUTPUT
