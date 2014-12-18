lib_LTLIBRARIES = libmonitor.la

libmonitor_la_SOURCES = src/monitor.c src/parser.c src/scanner.c src/pwatch.c 
libmonitor_la_CPPFLAGS = src
libmonitor_la_LDFLAGS = -L/opt/papi-5.3.0/lib/ -lpapi -ldl -lpthread -lm

libmonitor_la_LIBADD = \
        $(top_builddir)/hwloc/libhwloc.la

