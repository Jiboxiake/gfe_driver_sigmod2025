for a in 10 15 20 25 30 35 40 45
do
    numactl -N 1 -l /home/zhou822/gfe_driver_bw/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 1 -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l sortledton.4 -w $a -r $((50- $a)) --blacklist sssp,cdlp,pagerank,wcc,lcc --mixed_workload true --block_size 512
done