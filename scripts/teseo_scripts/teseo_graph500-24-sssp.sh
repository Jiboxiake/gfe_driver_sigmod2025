LD_LIBRARY_PATH=/usr/local/lib
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/zhou822/GTX_Mono/build/
export LD_LIBRARY_PATH 
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l teseo.13 -w 64 -r 64 -R 10 --blacklist bfs,cdlp,wcc,lcc,pagerank
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l teseo.13 -w 64 -r 56 -R 10 --blacklist bfs,cdlp,wcc,lcc,pagerank
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l teseo.13 -w 64 -r 48 -R 10 --blacklist bfs,cdlp,wcc,lcc,pagerank
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l teseo.13 -w 64 -r 40 -R 10 --blacklist bfs,cdlp,wcc,lcc,pagerank
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l teseo.13 -w 64 -r 32 -R 10 --blacklist bfs,cdlp,wcc,lcc,pagerank
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l teseo.13 -w 64 -r 24 -R 10 --blacklist bfs,cdlp,wcc,lcc,pagerank
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l teseo.13 -w 64 -r 16 -R 10 --blacklist bfs,cdlp,wcc,lcc,pagerank