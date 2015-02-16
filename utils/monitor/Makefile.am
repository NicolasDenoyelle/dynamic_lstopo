lib_LTLIBRARIES = libmonitor.la

noinst_PROGRAMS=list_avail
list_avail_SOURCES = make_scanner.c list_avail.c
list_avail_LDADD = $(top_builddir)/hwloc/libhwloc.la -lpapi
list_avail_CFLAGS = $(AM_CFLAGS)

scanner.l: list_avail
	./$< $@

scanner.c: scanner.l
	$(LEX) -o $@ $^

parser.c: parser.y
	$(YACC) -o $@ --defines=parser.tab.h $^

libmonitor_la_SOURCES = monitor_util.h monitor.c parser.c scanner.c pwatch.c list_avail.c monitor_replay.h monitor_replay.c
include_HEADERS = monitor.h
libmonitor_la_LDFLAGS = -lpapi -ldl -lpthread -lm
AM_CPPFLAGS = $(HWLOC_CPPFLAGS) -I$(top_srcdir)/utils/hwloc
libmonitor_la_LIBADD = $(top_builddir)/hwloc/libhwloc.la

CLEANFILES = scanner.c scanner.l parser.c parser.tab.h list_avail
DISTCLEANFILES = scanner.c scanner.l parser.c parser.tab.h list_avail

