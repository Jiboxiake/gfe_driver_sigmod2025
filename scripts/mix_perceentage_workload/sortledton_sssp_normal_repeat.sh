for a in 42 42 42 42 42 42 42 42 42 42 42 42 42 42
do
    numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/test0.graphlog --aging_timeout 24h -l sortledton.4 -w $a -r $((60- $a)) --blacklist pagerank,cdlp,bfs,wcc,lcc --mixed_workload true --block_size 512
    echo
    echo '***********************************************************'
    echo
done