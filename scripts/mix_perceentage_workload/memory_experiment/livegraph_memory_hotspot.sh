LD_LIBRARY_PATH=/usr/local/lib
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/zhou822/GTX-SIGMOD2025/build/
export LD_LIBRARY_PATH 
sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/hotspot2.graphlog --aging_timeout 24h -l livegraph3_ro -w 30 -r 30 --blacklist sssp,bfs,wcc,cdlp,pagerank --mixed_workload true
echo
echo '***********************************************************'
echo
sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/hotspot2.graphlog --aging_timeout 24h -l livegraph3_ro -w 30 -r 30 --blacklist sssp,bfs,wcc,lcc,pagerank --mixed_workload true
echo
echo '***********************************************************'
echo
sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/hotspot2.graphlog --aging_timeout 24h -l livegraph3_ro -w 30 -r 30 --blacklist sssp,lcc,wcc,cdlp,pagerank --mixed_workload true
echo
echo '***********************************************************'
echo
sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/hotspot2.graphlog --aging_timeout 24h -l livegraph3_ro -w 30 -r 30 --blacklist sssp,bfs,wcc,cdlp,lcc --mixed_workload true
echo
echo '***********************************************************'
echo
sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties  -R 1 -u --log /home/zhou822/graph_source/hotspot2.graphlog --aging_timeout 24h -l livegraph3_ro -w 30 -r 30 --blacklist lcc,bfs,wcc,cdlp,pagerank --mixed_workload true
echo
echo '***********************************************************'
echo