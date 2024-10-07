for a in 5 10
do
    numactl -N 0 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/hotspot2.graphlog --aging_timeout 24h -l gtx_rw -w $a -r $((50- $a)) --blacklist sssp,bfs,pagerank,wcc,lcc --mixed_workload true
    numactl -N 0 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties  -R 3 -u --log /home/zhou822/test_log/hotspot2.graphlog --aging_timeout 24h -l gtx_rw -w $a -r $((50- $a)) --blacklist sssp,cdlp,bfs,pagerank,lcc --mixed_workload true
done