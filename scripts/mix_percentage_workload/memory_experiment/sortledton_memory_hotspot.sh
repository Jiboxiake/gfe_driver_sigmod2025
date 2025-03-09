sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/hotspot2.graphlog --aging_timeout 24h -l sortledton.4 -w 30 -r 30 --blacklist sssp,bfs,wcc,cdlp,pagerank --mixed_workload true --block_size 512 --track_memory true
echo
echo '***********************************************************'
echo
sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/hotspot2.graphlog --aging_timeout 24h -l sortledton.4 -w 30 -r 30 --blacklist sssp,bfs,wcc,lcc,pagerank --mixed_workload true --block_size 512 --track_memory true
echo
echo '***********************************************************'
echo
sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/hotspot2.graphlog --aging_timeout 24h -l sortledton.4 -w 30 -r 30 --blacklist sssp,bfs,wcc,cdlp,lcc --mixed_workload true --block_size 512 --track_memory true
echo
echo '***********************************************************'
echo