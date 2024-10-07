LD_LIBRARY_PATH=/usr/local/lib
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/zhou822/GTX/build/
export LD_LIBRARY_PATH
for a in 16 24 32 40 48 56 64
do
    numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-22.properties  -R 10 -u -l gtx_rw -w 64 -r $a --blacklist cdlp,wcc,lcc
done