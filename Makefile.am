ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
SUBDIRS = libsmall
EXTRA_DIST =
CLEANFILES =
AM_MAKEFLAGS = --no-print-directory

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-I${top_srcdir}/src/clients \
	-I${top_srcdir}/src

AM_CFLAGS = ${my_CFLAGS} \
	@LIBSMALL_CFLAGS@ \
	-fvisibility=hidden \
	-ffunction-sections \
	-fdata-sections

AM_LDFLAGS = \
	@LIBSMALL_LIBS@ \
	-Wl,--gc-sections \
	-Wl,--as-needed

SED_PROCESS = \
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(SED) \
	-e 's,@VERSION\@,$(VERSION),g' \
	-e 's,@prefix\@,$(prefix),g' \
	-e 's,@exec_prefix\@,$(exec_prefix),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@includedir\@,$(includedir),g' \
	< $< > $@ || rm $@

%.pc: %.pc.in Makefile
	$(SED_PROCESS)

LIBCLIENTS_CURRENT=1
LIBCLIENTS_REVISION=0
LIBCLIENTS_AGE=0

pkginclude_HEADERS =\
	src/clients/clients.h

lib_LTLIBRARIES = src/libclients.la

src_libclients_la_SOURCES =\
	src/clients.c

EXTRA_DIST += src/libclients.sym

src_libclients_la_LDFLAGS = $(AM_LDFLAGS) \
	-version-info $(LIBCLIENTS_CURRENT):$(LIBCLIENTS_REVISION):$(LIBCLIENTS_AGE) \
	-Wl,--version-script=$(top_srcdir)/src/libclients.sym
src_libclients_la_DEPENDENCIES = ${top_srcdir}/src/libclients.sym

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = src/libclients.pc
EXTRA_DIST += src/libclients.pc.in
CLEANFILES += src/libclients.pc

bin_PROGRAMS = \
	create-delete-ephemerals \
	create-ephemerals \
	create-idle-clients \
	create-multiple-ephemerals \
	get-children-with-watch \
	get-data-with-watch \
	poll-path

create_ephemerals_SOURCES = src/create-ephemerals.c
create_ephemerals_LDADD = src/libclients.la

create_idle_clients_SOURCES = src/create-idle-clients.c
create_idle_clients_LDADD = src/libclients.la

create_multiple_ephemerals_SOURCES = src/create-multiple-ephemerals.c
create_multiple_ephemerals_LDADD = src/libclients.la

create_delete_ephemerals_SOURCES = src/create-delete-ephemerals.c
create_delete_ephemerals_LDADD = src/libclients.la

get_children_with_watch_SOURCES = src/get-children-with-watch.c
get_children_with_watch_LDADD = src/libclients.la

get_data_with_watch_SOURCES = src/get-data-with-watch.c
get_data_with_watch_LDADD = src/libclients.la

poll_path_SOURCES = src/poll-path.c
poll_path_LDADD = src/libclients.la
