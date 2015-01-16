lib_LTLIBRARIES = libmonitor.la

list_avail_SOURCES = make_scanner.c list_avail.c
list_avail_LDADD = $(HWLOC_top_builddir)/hwloc/libhwloc.la
list_avail_LDFLAGS = -L/home/ndenoyel/papi-5.4.0/build/lib -lpapi 
list_avail_CFLAGS = $(AM_CFLAGS)

bin_PROGRAMS = list_avail

scanner.l: list_avail
	./$< $@

scanner.c: scanner.l
	$(LEX) -o $@ $^

parser.c: parser.y
	$(YACC) -o $@ --defines=parser.tab.h $^

libmonitor_la_SOURCES = monitor.c parser.c scanner.c pwatch.c list_avail.c
libmonitor_la_LDFLAGS = -lpapi -ldl -lpthread -lm
AM_CPPFLAGS = $(HWLOC_CPPFLAGS) -I$(top_srcdir)/utils/hwloc
libmonitor_la_LIBADD = $(top_builddir)/hwloc/libhwloc.la

DISTCLEANFILES = scanner.c scanner.l parser.c parser.tab.h

