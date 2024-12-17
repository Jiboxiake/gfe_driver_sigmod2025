for a in 18 30 42 54
do
    sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
    numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/test0.graphlog --aging_timeout 24h -l sortledton.4 -w $a -r $((60- $a)) --blacklist pagerank,sssp,cdlp,wcc,lcc --mixed_workload true --block_size 512
    echo
    echo '***********************************************************'
    echo
done