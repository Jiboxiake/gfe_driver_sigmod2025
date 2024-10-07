LD_LIBRARY_PATH=/usr/local/lib
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/zhou822/GTX/build/
export LD_LIBRARY_PATH
for a in 8 16 24 32 40 48 56 64
do
    numactl -N 0 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u --log /home/zhou822/test_log/hotspot2.graphlog --aging_timeout 24h -l gtx_rw -w $a
    numactl -N 0 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u --log /home/zhou822/test_log/test0.graphlog --aging_timeout 24h -l gtx_rw -w $a
done