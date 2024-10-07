for a in 16 24 32 40 48 56 64
do
    numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/dota-league.properties  -R 10 -u -l livegraph3_ro -w 64 -r $a --blacklist cdlp,wcc,lcc
done