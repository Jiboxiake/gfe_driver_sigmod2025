for a in 4 8 16 24 32 40 48 56 64
do
    numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/dota-league.properties -u -l livegraph3_ro -w $a
done