LD_LIBRARY_PATH=/usr/local/lib
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/zhou822/GTX-SIGMOD2025/build/
export LD_LIBRARY_PATH
for a in 16 24 32 40 48 56 64
do
    sudo sh -c 'echo 3 >  /proc/sys/vm/drop_caches'
    numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/graph500/graph500-24.properties -u -l gtx_rw -w $a
done