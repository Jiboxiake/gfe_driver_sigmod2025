#!/bin/sh
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 64 --is_timestamped true --block_size 512
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 56 --is_timestamped true --block_size 512
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 48 --is_timestamped true --block_size 512
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 40 --is_timestamped true --block_size 512
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 32 --is_timestamped true --block_size 512
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 24 --is_timestamped true --block_size 512
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 16 --is_timestamped true --block_size 512
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 8 --is_timestamped true --block_size 512
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l sortledton.4 -w 4 --is_timestamped true --block_size 512