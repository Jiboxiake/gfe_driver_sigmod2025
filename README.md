
Our version of GFE_Driver is developed on top of the original GFE_Driver (https://github.com/cwida/gfe_driver) and modified GFE_Driver (https://github.com/PerFuchs/gfe_driver/tree/master) that are used to evaluate previous works.

GFE Driver
---

The GFE (Graph Framework Evaluation) Driver is the program used to run the experiments in "GTX: A  Write-Optimized Latch-free Graph Data System with Transactional Support", measuring the throughput of updates in libraries supporting structural dynamic graphs and the completion times of 
the [Graphalytics kernels](https://github.com/ldbc/ldbc_graphalytics) concurrently. 
The driver supports the following systems: [GTX](https://github.com/Jiboxiake/GTX-SIGMOD2025), [Sortledton](https://gitlab.db.in.tum.de/per.fuchs/sortledton), [Teseo](https://github.com/cwida/teseo), 
[LLAMA](https://github.com/goatdb/llama), [GraphOne](https://github.com/the-data-lab/GraphOne), 
[Stinger](http://stingergraph.com/) and [LiveGraph](https://github.com/thu-pacman/LiveGraph-Binary) while we ran our experiments only for systems that support concurrent reads and writes under transactions (GTX, Sortledton, Teseo, and LiveGraph).
It can run four kinds of experiments: insert all edges in a random permuted order or timestamp-based order from an input graph, execute the updates specified by a [graphlog file](https://github.com/Jiboxiake/graphlog), run the kernels of the Graphalytics suite: BFS, PageRank (PR), weighted shortest paths (SSSP), and concurrently execute updates and graph analytics.  
In our paper we reported all insert experiments, all update experiments, and read-write mixed workload experiment. We also ran Graphalytics experiment but due to space we did not present the results in our papaer.
### Build 

#### Requisites 
- O.S. Linux
- Autotools, [Autoconf 2.69+](https://www.gnu.org/software/autoconf/)
- A C++17 compliant compiler with support for OpenMP. We tested it with GCC 10.
- libnuma 2.0 +
- [libpapi 5.5 +](http://icl.utk.edu/papi/)
- [SQLite 3.27 +](https://sqlite.org)
- Intel Threading Building Blocks 2 (version 2020.1-2)

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

##### LiveGraph

Download the source code from the [official repository](https://github.com/thu-pacman/LiveGraph). 
In the paper, we evaluated version `eea5a40ce6ee443f901e44323c1119f59113eaf3`.
Then build the LiveGraph library.
After the library is built, configure the driver by pointing the path to where the library has been built:

```
mkdir build && cd build
../configure --enable-optimize --disable-debug --with-livegraph=/path/to/livegraph/build
```

##### Teseo

Use the branch `master` from https://github.com/cwida/teseo.
In the paper, we evaluated version `2c37c2831c4d2acaaa838a86e1318363ce68c45b`.

```
git clone https://github.com/cwida/teseo
cd teseo
./autoreconf -iv
mkdir build && cd build
../configure --enable-optimize --disable-debug
make -j
```

If the build has been successful, it should at least create the archive `libteseo.a`.
Then configure the driver with:

```
mkdir build && cd build
../configure --enable-optimize --disable-debug --with-teseo=/path/to/teseo/build   
```

##### Sortledton
Use the branch `master` from `https://gitlab.db.in.tum.de/per.fuchs/sortledton`.
For the paper, we evaluated commit "6eb638f3ad38f8a10a127e7e118528f4c8d07a6e".

Follow the instructions in the README of the repository to setup and build the library.
Then configure the driver with:

```
mkdir build && cd build
../configure --enable-optimize --disable-debug --with-sortledton=/path/to/microbenchmark/build   
```

#### GTX
Currently we use the branch 'master' from 'https://github.com/Jiboxiake/GTX-SIGMOD2025' .
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

### Executing the driver


The driver takes as input a list of options together with a graph, and emits the results into a sqlite3 database. It also prints out the results to console.
There are four kinds of experiments that can be executed:

- **Insertions only**: insert all vertices and edges from an input graph:

Insert in a random order:
```
./gfe_driver -G /path/to/input/graph.properties -u -l <system_to_evaluate> -w <num_threads>

```
Insert in a timestamp-based order:
```
./gfe_driver -G /path/to/input/graph.el -u -l <system_to_evaluate> -w <num_threads> --is_timestamped true
```

- **Updates**: perform all insertions and deletions from a log. Add the option --log /path/to/updates.graphlog:

```
./gfe_driver -G /path/to/input/graph.properties -u --log /path/to/updates.graphlog -l <system_to_evaluate> -w <num_threads>
```

- **Graphalytics**: execute kernels from the Graphalytics suite. Add the option `-R <N>` to repeat `N` times the execution of Graphalytics kernel(s) one by one. E.g., to run the BFS, PageRank and single source shortest path (SSSP) five times, after all vertices and edges have been inserted, use:

```
./gfe_driver -G /path/to/input/graph.properties -u -l <system_to_evaluate> -w <num_threads> -R 5 -d output_results.sqlite3 --blacklist cdlp,wcc,lcc
```
For our paper, we did not separately conduct Graphalytics-only experiments but include them as part of the concurrent read-write experiments.

- **Concurrent mixed read-write**: execute the updates experiment and concurrently run graph analytics. We currently support concurrent graph topology scan, graph property scan, BFS, and PageRank. We subsitute CDLP and WCC with graph topology scan and property scan. For example, to execute updates from logs and concurrently run PageRank, run:

```
./gfe_driver -G /path/to/input/graph.properties  -R 3 -u --log /path/to/updates.graphlog -l <system_to_evaluate> -w <num_threads> -r <num_reader_threads> --blacklist sssp,cdlp,bfs,wcc,lcc --mixed_workload true
```

- **Memory Usage**: measure the total memory usage of the graph insertion, graph update, and concurrent mixed read-write experiments in KBs. For the graph insert experiments, we measured the total memory usage using 32 worker threads. For the mixed-workload experiment, we measured the memory usage using 30 reader threads and 30 writer threads (50% balanced workload). For the experiments, run:
```
./gfe_driver -G /path/to/input/graph.el -u -l <system_to_evaluate> -w <num_threads> --track_memory true
./gfe_driver -G /path/to/input/graph.properties  -R 3 -u --log /path/to/updates.graphlog -l <system_to_evaluate> -w <num_threads> -r <num_reader_threads> --blacklist sssp,cdlp,bfs,wcc,lcc --mixed_workload true --track_memory true
```

Type `./gfe_driver -h` for the full list of options and for the libraries that can be evaluated (option `-l`). The driver spawns the number of threads given by the option `-w` to concurrently run all insertions or updates. For Graphalytics, it defaults to the total number of the physical threads in the machine. This setting can be changed with the option `-r <num_threads>`. Note that the numbers
in the library codes (e.g. teseo.**6**, stinger**3**) are unrelated to the versions of the systems evaluated, they were only used
internally for development purposes.

The database `output_results.sqlite3` will contain the final results. Refer to [this repository](https://github.com/whatsthecraic/gfe_notebooks) to see how to load and inspect the data within Jupyter notebooks. In our paper  "GTX: A  Write-Optimized Latch-free Graph Data System with Transactional Support", we did not use the notebook but generated the figures directly from the experiment output.
All scripts of running the experiments mentioned in the paper can be found at [/scripts/] . We also implemented a mixed workload of updates and transactional single edge reads but did not include it in the paper. It is implemented as a variance of the update experiment and can be found at [/experiment/details/].



