lib_LTLIBRARIES = libmonitor.la

scanner.c: scanner.l
	$(LEX) -o $@ $^

parser.c: parser.y 
	$(YACC) -o $@ --defines=parser.tab.h $^

LDADD = $(HWLOC_top_builddir)/libmonitor.la
libmonitor_la_SOURCES = monitor_util.h monitor.c parser.c scanner.c pwatch.c list_avail.c monitor_replay.h monitor_replay.c
libmonitor_la_LDFLAGS = -lpapi -ldl -lpthread -lm
AM_CPPFLAGS = $(HWLOC_CPPFLAGS) -I$(top_srcdir)/utils/hwloc
libmonitor_la_LIBADD = $(top_builddir)/hwloc/libhwloc.la

CLEANFILES = scanner.c parser.c parser.tab.h
DISTCLEANFILES = scanner.c parser.c parser.tab.h

