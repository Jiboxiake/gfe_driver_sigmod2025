LD_LIBRARY_PATH=/usr/local/lib
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/zhou822/GTX/build/
export LD_LIBRARY_PATH
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 64 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 56 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 48 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 40 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 32 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 24 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 16 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 8 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/graph500-24.properties -u -l gtx_rw -w 4 --log /home/zhou822/test_log/hotspot1.graphlog --aging_timeout 24h