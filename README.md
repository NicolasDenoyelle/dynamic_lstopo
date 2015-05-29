## hwloc fork with hierarchical performance monitoring
### Introduction

The Hardware Locality (hwloc) software project aims at easing the process of
discovering hardware resources in parallel architectures. It offers
command-line tools and a C API for consulting these resources, their locality,
attributes, and interconnection. hwloc primarily aims at helping
high-performance computing (HPC) applications, but is also applicable to any
project seeking to exploit code and/or data locality on modern computing
platforms.

hwloc is actually made of two subprojects distributed together:

  * The original hwloc project for describing the internals of computing nodes.
 It is described in details between sections Hardware Locality (hwloc)
 Introduction and Network Locality (netloc) Introduction.
  * The network-oriented companion called netloc (Network Locality), described
 in details starting at section Network Locality (netloc) Introduction.
 Netloc may be disabled, but the original hwloc cannot. Both hwloc and
 netloc APIs are documented after these sections.

This version also contains a new version of lstopo utility to display or record performance counters.
The counters collected with PAPI are aggregated into upper levels of the hierarchy to display live a consistent view of the machine state.

### Installation

hwloc (http://www.open-mpi.org/projects/hwloc/) is available under the BSD
license. It is hosted as a sub-project of the overall Open MPI project (http://
www.open-mpi.org/). Note that hwloc does not require any functionality from
Open MPI -- it is a wholly separate (and much smaller!) project and code base.
It just happens to be hosted as part of the overall Open MPI project.

Nightly development snapshots are available on the web site. Additionally, the
code can be directly cloned from Git:

```
git clone https://github.com/open-mpi/hwloc.git
cd hwloc
./autogen.sh
```

Note that GNU Autoconf >=2.63, Automake >=1.11 and Libtool >=2.2.6 are required
when building from a Git clone.

Installation by itself is the fairly common GNU-based process:

```
./configure --prefix=...
make
make install
```

hwloc- and netloc-specific configure options and requirements are documented in
sections hwloc Installation and Netloc Installation respectively.

Also note that if you install supplemental libraries in non-standard locations,
hwloc's configure script may not be able to find them without some help. You
may need to specify additional CPPFLAGS, LDFLAGS, or PKG_CONFIG_PATH values on
the configure command line.

For example, if libpciaccess was installed into /opt/pciaccess, hwloc's
configure script may not find it be default. Try adding PKG_CONFIG_PATH to the
./configure command line, like this:

```
./configure PKG_CONFIG_PATH=/opt/pciaccess/lib/pkgconfig ...
```

Running the "lstopo" tool is a good way to check as a graphical output whether
hwloc properly detected the architecture of your node. Netloc command-line
tools can be used to display the network topology interconnecting your nodes.


### Machine and applications monitoring

![Current machine state](https://github.com/NicolasDenoyelle/dynamic_lstopo/blob/monitor/E5-2650.png)

If monitoring is enabled during configuration, lstopo utility will use PAPI to Monitor an application or a machine into a graphical display.

#### Requirements

X output must be enabled.
You must ave permission to read every performance counters. It can be achieved by running the command: 
```
echo "-1" > /proc/sys/kernel/perf_event_paranoid" as root.
```
PAPI, bison and lex must be installed.
If you use a custom installation of these, you have to append CPPFLAGS for includes and LDFLAGS for lib to configure command line.

#### Installation
	
Simply install hwloc.
Use configure option `--enable-monitor` to force monitor compilation.
The configure summary shows if monitors is enabled.

#### Usage
        
lstopo man page contains every extra perf option.
`lstopo --help` to. 

Use lstopo the same way as usual + append perf options:
`lstopo --perf` will display random availables counters on random topology nodes.
You can describe which counter(s) to display on a specific topology node with `--perf-input`.
Performance input files syntax is the same as follow:

```
#commentary
#counter_name{hwloc_obj_name,PAPI_COUNTER1*3.14159265359/PAPI_COUNTER2}`
my_counter{L1i,PAPI_FP_OPS*PAPI_L1_ICM/100}`
```

If an object or a counter isn't available on your system, a list of 
availables items will be dumped.

#### Monitoring an application

* As a backend:
```
lstopo <perf options> <your_application> <your application args>
```
Note: setting output to a pdf, a png, or a svg file will accumulate values and output them once, instead of classic graphic output where the display is refreshed periodically.


* Instrumenting your code:
Use `monitor.h` header in `<hwloc_install_include_dir>` and link your application  with `-lmonitor -lhwloc`
Once you executed your application, you can display the trace with `lstopo --perf-replay <output_file>`.

* Here is a minimal snippet example:
```
#include <monitor.h>
#include <hwloc.h>
#include <sys/types.h>
#include <unistd.h>

monitors_t monitor;

int main(void)
{
	/* Initialize monitors */
	monitors = load_Monitors_from_config(NULL, "my_perf_file", "my_output_file", 0);
	/* Attach to current pid */
	Monitors_watch_pid(monitors,getpid());

	/* start and collect current hardware counters' value */
	Monitors_start(monitors);

	/* Your code here */
	/* ... */

	/* update to current hardware counters' value */
	Monitors_update_counters(monitors);

	/* destroy monitors */
	delete_Monitors(monitors);
	return 0;
}
```

* Here is a snippet for sampling phases:

```
#include <pthread.h>
#include <monitor.h>
#include <hwloc.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

monitors_t monitors;
pthread_t sampling_thr;	
pthread_mutex_t sample;
int end_flag = 0;

void take_a_sample(union sigval unused)
{
   pthread_mutex_unlock(&sample);
}

void * sampling_thread(void* refresh_usec)
{
   /* Init structure and timer for periodic samples */
   struct sigevent sevp;
   timer_t timerid;
   sevp.sigev_notify=SIGEV_THREAD;
   sevp.sigev_notify_function=take_a_sample;
   if(timer_create(CLOCK_THREAD_CPUTIME_ID, &sevp, &timerid)==-1){
   	perror("timer_create");
	return NULL;
   }		
   struct itimerspec interval = {{0, 1000*(long)(intptr_t)(refresh_usec)}, {0, 0}} ;	

   /* Init mutex for sampling loop unlock every interval */
   pthread_mutex_init(&sample,NULL);
   pthread_mutex_lock(&sample);	

   /* trigger timer */
   timer_settime(timerid, 0, &interval, NULL);
   while(end_flag==0){
	pthread_mutex_lock(&sample);
	/* update to current hardware counters' value */
	Monitors_update_counters(monitors);
   }

   pthread_mutex_destroy(&sample);
   return NULL;
}

void sampling_start(long refresh_usec)
{
   sampling_thr = pthread_create(&sampling_thr,NULL,sampling_thread,NULL);
}

void sampling_stop(void)
{
   end_flag = 1;
   pthread_join(sampling_thr,NULL);
   end_flag = 0;
}

int main(void)
{
	/* Initialize monitors */
	monitors = load_Monitors_from_config(NULL, "my_performance_file", "my_output_file", 0);
	/* Attach to current pid */
	Monitors_watch_pid(monitors,getpid());
	/* start and collect current hardware counters' value */
	Monitors_start(monitors);
	/* Set a marker to find this phase into my_output_file */
	Monitors_set_phase(monitors, 0);
	/* Start sampling region every millisecond */
	sampling_start(1000);
	/* Your code here */
	/* ... */
	/* Stop sampling */
	sampling_stop();
	/* destroy monitors */
	delete_Monitors(monitors);
	return 0;
}
``` 

