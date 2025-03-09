LD_LIBRARY_PATH=/usr/local/lib
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/zhou822/GTX/build/
export LD_LIBRARY_PATH
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 64
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 56
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 48
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 40
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 32
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 24
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 16
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 8
numactl -N 1 -l /home/zhou822/gfe_driver_gtx/build/gfe_driver -G /home/zhou822/gfe_experiment_data/graph_source/out.twitter.el -u -l gtx_rw -w 4