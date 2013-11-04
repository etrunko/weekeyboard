bin_PROGRAMS =					\
	weekeyboard

AM_CFLAGS=					\
	@WAYLAND_CFLAGS@			\
	@EFL_CFLAGS@				\
	@IBUS_CFLAGS@				\
	-DPKGDATADIR='"$(pkgdatadir)"'

AM_LDFLAGS=					\
	@WAYLAND_LIBS@				\
	@EFL_LIBS@				\
	@IBUS_LIBS@

weekeyboard_SOURCES=				\
	wkb-main.c				\
	wkb-log.c				\
	wkb-log.h				\
	wkb-ibus.h				\
	wkb-ibus.c				\
	wkb-ibus-helper.h			\
	wkb-ibus-helper.c			\
	wkb-ibus-defs.h				\
	wkb-ibus-panel.c			\
	wkb-ibus-config.c			\
	wkb-ibus-config-key.c			\
	wkb-ibus-config-key.h			\
	wkb-ibus-config-eet.c			\
	wkb-ibus-config-eet.h			\
	input-method-protocol.c			\
	input-method-client-protocol.h		\
	text-protocol.c				\
	text-client-protocol.h


noinst_PROGRAMS =				\
	weekeyboard-config-eet-test		\
	weekeyboard-ibus-test

weekeyboard_config_eet_test_SOURCES =		\
	wkb-log.c				\
	wkb-log.h				\
	wkb-ibus-config-key.c			\
	wkb-ibus-config-key.h			\
	wkb-ibus-config-eet.c			\
	wkb-ibus-config-eet.h			\
	wkb-ibus-config-eet-test.c


weekeyboard_ibus_test_SOURCES =			\
	wkb-ibus.h				\
	wkb-ibus.c				\
	wkb-ibus-helper.h			\
	wkb-ibus-helper.c			\
	wkb-log.c				\
	wkb-log.h				\
	wkb-ibus-defs.h				\
	wkb-ibus-panel.c			\
	wkb-ibus-config.c			\
	wkb-ibus-config-key.c			\
	wkb-ibus-config-key.h			\
	wkb-ibus-config-eet.c			\
	wkb-ibus-config-eet.h			\
	wkb-ibus-test.c

@wayland_scanner_rules@

BUILT_SOURCES=					\
	 input-method-protocol.c		\
	 input-method-client-protocol.h		\
	 text-protocol.c			\
	 text-client-protocol.h
