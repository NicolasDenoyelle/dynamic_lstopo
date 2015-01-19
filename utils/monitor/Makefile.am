lib_LTLIBRARIES = libmonitor.la

list_avail: make_scanner.c list_avail.c
	$(CC) -o $@ $^ -L$(HWLOC_top_builddir)/hwloc/libhwloc.la -lpapi -lhwloc

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

DISTCLEANFILES = scanner.c scanner.l parser.c parser.tab.h list_avail

