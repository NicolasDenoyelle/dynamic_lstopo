bin_PROGRAMS= hwloc_monitor_demo
hwloc_monitor_demo_LDADD=$(top_builddir)/hwloc/libhwloc.la
hwloc_monitor_demodir=$(datadir)
hwloc_monitor_demo_DATA= perf_group.txt
hwloc_monitor_demo_SOURCES= shared_miss_gen.c linked_list.c
hwloc_monitor_demo_CFLAGS= $(OPENMP_CFLAGS)
hwloc_monitor_demo_CPPFLAGS=$(OPENMP_CFLAGS) 
hwloc_monitor_demo_LDFLAGS= -lncurses -lm -lhwloc

