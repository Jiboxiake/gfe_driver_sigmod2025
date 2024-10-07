for a in 16 24 32 40 48 56 64
do
    numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l livegraph3_ro  -w $a --log /home/zhou822/test_log/hotspot0.graphlog --aging_timeout 24h
done