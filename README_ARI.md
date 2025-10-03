
Our version of GFE_Driver is developed on top of the original GFE_Driver (https://github.com/cwida/gfe_driver) and modified GFE_Driver (https://github.com/PerFuchs/gfe_driver/tree/master) that are used to evaluate previous works.

GFE Driver
---

The GFE (Graph Framework Evaluation) Driver is the program used to run the experiments in "GTX: A  Write-Optimized Latch-free Graph Data System with Transactional Support", measuring the throughput of updates in libraries supporting structural dynamic graphs and the completion times of 
the [Graphalytics kernels](https://github.com/ldbc/ldbc_graphalytics) concurrently. 
We make this readme solely for the evaluation by SIGMOD ARI.
### Build 

#### Requisites 
- O.S. Linux
- Autotools, [Autoconf 2.69+](https://www.gnu.org/software/autoconf/)
- A C++17 compliant compiler with support for OpenMP. We tested it with GCC 10.
- libnuma 2.0 +
- [libpapi 5.5 +](http://icl.utk.edu/papi/)
- [SQLite 3.27 +](https://sqlite.org)
- Intel Threading Building Blocks 2 (version 2020.1-2)
- [GTX](https://github.com/purduedb/GTX)

#### Configure
Clone the repository.

Initialise the sources and the configure script by:

```
cd gfe_driver_gtx
git submodule update --init
mkdir build && cd build
autoreconf -iv ..
```

The driver needs to be linked with the system to evaluate, which has to be built ahead. 
We do not recommend linking the driver with multiple systems at once, 
due to the usage of global variables in some systems and other naming clashes. 
Instead, it is safer to reconfigure and rebuild the driver each time for a single specific system.


#### Evaluating GTX
Currently we use the branch 'master' from 'https://github.com/purduedb/GTX' .
If GFE_DRIVER is used to reproduce the experiments for billion-edges grahs, please change the [USING_BIGDATA] flag to true in ./core/graph_global.hpp.
Follow the instruction in REAME to build GTX. After GTX has been built, configure the driver with:
```
mkdir build && cd build
../configure --enable-optimize --disable-debug --with-gtx=/path/to/GTX/build
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/path/to/GTX/build
export LD_LIBRARY_PATH 
```

#### Compile

Once configured, run `make -j`. There is no `install` target, the final artifact is the executable `gfe_driver`. 

If in the mood of running the testsuite, type `make check -j`.

### Datasets

In our experiments, we used the following input graphs and data sets:

- `dota-league` and `graph500-SF`, with `SF` in {22, 24, 26} were taken from the [official Graphalytics collection](https://www.graphalytics.org/datasets).
- `uniform-SF`, with `SF` in {22, 24, 26} were generated with an [ad-hoc tool](https://github.com/whatsthecraic/uniform_graph_generator). These are synthetic graphs having the same number of vertices and edges of `graph500-SF`, but a uniform node degree distribution.
- The logs for the experiments with updates, i.e. with both insertions and deletions,
  were generated with another [ad-hoc tool](https://github.com/whatsthecraic/graphlog). 
- `yahoo-songs`, `edit-enwiki` and `twitter` were taken from the [Konect webpage](http://konect.cc/networks/) they were prepared 
  for our experiments by sorting them by timestamp (`yahoo-songs` and `edit-enwiki`) and removing duplicates (all 3 datasets) by using `tools/timestampd_graph_2_edge_list.py`.  

A complete image of all datasets used in the experiments can be downloaded from the following sources: [input graphs](https://zenodo.org/record/3966439),
[graph logs](https://purdue0-my.sharepoint.com/:f:/g/personal/zhou822_purdue_edu/EiBWFr3Ah_JEjjebIEwxtfsB9k_QF8WWmWTFhOuC1S77VQ?e=AqAJGY), and [timestamped graphs](https://zenodo.org/record/5752476).

### Executing the driver with GTX


The driver takes as input a list of options together with a graph, and emits the results into a sqlite3 database. It also prints out the results to console.
There are four kinds of experiments that can be executed:

- **Insertions only**: insert all vertices and edges from an input graph:

Insert in a random order:
```
./gfe_driver -G /path/to/input/graph.properties -u -l gtx_rw -w <num_threads>

```
Insert in a timestamp-based order:
```
./gfe_driver -G /path/to/input/graph.el -u -l gtx_rw -w <num_threads> --is_timestamped true
```

- **Updates**: perform all insertions and deletions from a log. Add the option --log /path/to/updates.graphlog:

```
./gfe_driver -G /path/to/input/graph.properties -u --log /path/to/updates.graphlog -l gtx_rw -w <num_threads>
```

- **Graphalytics**: execute kernels from the Graphalytics suite. Add the option `-R <N>` to repeat `N` times the execution of Graphalytics kernel(s) one by one. E.g., to run the BFS, PageRank and single source shortest path (SSSP) five times, after all vertices and edges have been inserted, use:

```
./gfe_driver -G /path/to/input/graph.properties -u -l gtx_rw -w <num_threads> -R 5 -d output_results.sqlite3 --blacklist cdlp,wcc,lcc
```
For our paper, we did not separately conduct Graphalytics-only experiments but include them as part of the concurrent read-write experiments.

- **Concurrent mixed read-write**: execute the updates experiment and concurrently run graph analytics. We currently support concurrent graph topology scan, graph property scan, BFS, and PageRank. We subsitute CDLP and WCC with 1-hop and 2-hop neighbors. For example, to execute updates from logs and concurrently run PageRank, run:

```
./gfe_driver -G /path/to/input/graph.properties  -R 3 -u --log /path/to/updates.graphlog -l gtx_rw -w <num_threads> -r <num_reader_threads> --blacklist sssp,cdlp,bfs,wcc,lcc --mixed_workload true
```

- **Memory Usage**: measure the total memory usage of the graph insertion, graph update, and concurrent mixed read-write experiments in KBs. For the graph insert experiments, we measured the total memory usage using 32 worker threads. For the mixed-workload experiment, we measured the memory usage using 30 reader threads and 30 writer threads (50% balanced workload). For the experiments, run:
```
./gfe_driver -G /path/to/input/graph.el -u -l gtx_rw -w <num_threads> --track_memory true
./gfe_driver -G /path/to/input/graph.properties  -R 3 -u --log /path/to/updates.graphlog -l gtx_rw -w <num_threads> -r <num_reader_threads> --blacklist sssp,cdlp,bfs,wcc,lcc --mixed_workload true --track_memory true
```
### All Experiment Scripts
All our scripts used in the Experiment Evaluation section of our paper "GTX: A  Write-Optimized Latch-free Graph Data System with Transactional Support" can be found at [/scripts/]. All graph insertion experiment scripts for each evaluated system can be found at the directory with the same name. For example, all GTX graph insertion experiment scripts are found at [/scripts/gtx_scripts]. All mixed-workload experiment scripts are found under [/scripts/mixed_percentage_workload] and memory measurement scripts are found under [/scripts/mixed_percentage_workload/memory_experiment].

Type `./gfe_driver -h` for the full list of options and for the libraries that can be evaluated (option `-l`). The driver spawns the number of threads given by the option `-w` to concurrently run all insertions or updates. For Graphalytics, it defaults to the total number of the physical threads in the machine. This setting can be changed with the option `-r <num_threads>`. Note that the numbers
in the library codes (e.g. teseo.**6**, stinger**3**) are unrelated to the versions of the systems evaluated, they were only used
internally for development purposes.




