MAINTAINERCLEANFILES = Makefile.in

EDJE_CC = @EDJE_CC_PATH@
EDJE_FLAGS_VERBOSE_ =
EDJE_FLAGS_VERBOSE_0 =
EDJE_FLAGS_VERBOSE_1 = -v
EDJE_FLAGS = $(EDJE_FLAGS_VERBOSE_$(V))

filesdir = $(pkgdatadir)
files_DATA = default_600.edj \
	     default_720.edj \
	     default_1080.edj

DEFAULT_FILES = default/default.edc \
		default/default_600.edc \
		default/default_720.edc \
		default/default_1080.edc \
		default/ignorekeys.txt \
		default/fonts/DroidSans-Bold.ttf \
		default/fonts/DroidSans.ttf \
		default/images/key-hint-bg.png \
		default/images/icon-language.png \
		default/images/icon-backspace.png \
		default/images/key-special.png \
		default/images/icon-space.png \
		default/images/key-hint.png \
		default/images/icon-shift.png \
		default/images/icon-shift-active.png \
		default/images/icon-enter.png \
		default/images/key-special-pressed.png \
		default/images/key-default.png \
		default/images/key-default-pressed.png

EXTRA_DIST = $(DEFAULT_FILES)

default_600.edj: Makefile default/default_600.edc $(default_FILES)
	$(EDJE_CC) $(EDJE_FLAGS) \
	    -dd $(top_srcdir)/data/themes/default \
	    -id $(top_srcdir)/data/themes/default/images \
	    -fd $(top_srcdir)/data/themes/default/fonts \
	    $(top_srcdir)/data/themes/default/default_600.edc \
	    $(top_builddir)/data/themes/default_600.edj

default_720.edj: Makefile default/default_720.edc $(default_FILES)
	$(EDJE_CC) $(EDJE_FLAGS) \
	    -dd $(top_srcdir)/data/themes/default \
	    -id $(top_srcdir)/data/themes/default/images \
	    -fd $(top_srcdir)/data/themes/default/fonts \
	    $(top_srcdir)/data/themes/default/default_720.edc \
	    $(top_builddir)/data/themes/default_720.edj

default_1080.edj: Makefile default/default_1080.edc $(default_FILES)
	$(EDJE_CC) $(EDJE_FLAGS) \
	    -dd $(top_srcdir)/data/themes/default \
	    -id $(top_srcdir)/data/themes/default/images \
	    -fd $(top_srcdir)/data/themes/default/fonts \
	    $(top_srcdir)/data/themes/default/default_1080.edc \
	    $(top_builddir)/data/themes/default_1080.edj

clean-local:
	rm -f $(top_builddir)/data/themes/*.edj
