numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l sortledton.4 -w 16 -r 16 --blacklist sssp,cdlp,pagerank,wcc,lcc --mixed_workload true --block_size 512
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l sortledton.4 -w 16 -r 32 --blacklist sssp,cdlp,pagerank,wcc,lcc --mixed_workload true --block_size 512
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l sortledton.4 -w 16 -r 48 --blacklist sssp,cdlp,pagerank,wcc,lcc --mixed_workload true --block_size 512
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l sortledton.4 -w 32 -r 32 --blacklist sssp,cdlp,pagerank,wcc,lcc --mixed_workload true --block_size 512
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l sortledton.4 -w 32 -r 16 --blacklist sssp,cdlp,pagerank,wcc,lcc --mixed_workload true --block_size 512
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l sortledton.4 -w 40 -r 24 --blacklist sssp,cdlp,pagerank,wcc,lcc --mixed_workload true --block_size 512
numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l sortledton.4 -w 48 -r 16 --blacklist sssp,cdlp,pagerank,wcc,lcc --mixed_workload true --block_size 512
