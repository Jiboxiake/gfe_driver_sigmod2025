#!/bin/sh
LD_LIBRARY_PATH=/usr/local/lib
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/zhou822/GTX-SIGMOD2025/build/
export LD_LIBRARY_PATH
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 64 --is_timestamped true
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 56 --is_timestamped true
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 48 --is_timestamped true
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 40 --is_timestamped true
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 32 --is_timestamped true
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 24 --is_timestamped true
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 16 --is_timestamped true
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 8  --is_timestamped true
numactl -N 1 -l /home/zhou822/gfe_driver_sigmod2025/build/gfe_driver -G /ssd_root/zhou822/graph_source/yahoo-song/out.yahoo-song.el -u -l gtx_rw -w 4  --is_timestamped true