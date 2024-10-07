for a in 10 15 20 25 30 35 40 45
do
    numactl -N 1 -l /home/zhou822/gfe_driver_bw/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 1 -u --log /home/zhou822/test_log/hotspot2.graphlog --aging_timeout 24h -l livegraph3_ro -w $a -r $((50- $a)) --blacklist sssp,pagerank,bfs,wcc,cdlp --mixed_workload true
done