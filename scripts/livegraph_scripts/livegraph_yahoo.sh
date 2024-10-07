for a in 8 16 24 32 40 48 56 64
do
    numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/yahoo-song/out.yahoo-song.el -u -l livegraph3_ro -w $a 
    numactl -N 1 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/yahoo-song/out.yahoo-song.el -u -l livegraph3_ro -w $a --is_timestamped true
done