#!/bin/sh
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 64 
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 56 
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 40 
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 32 
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 24
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 16 
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 12
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 8  
numactl -N 0 -l /home/zhou822/gfe_driver_bigdata/build/gfe_driver -G /home/zhou822/gfe_experiment_data/edit-enwiki/out.edit-enwiki.el -u -l teseo.13 -w 4  