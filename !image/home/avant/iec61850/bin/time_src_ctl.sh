#!/bin/bash
BASEDIR=$(dirname "$0")

echo BASEDIR = $BASEDIR

cd $BASEDIR
mkdir -p ../tmp
./time_src_ctl --out=../tmp/time_monitor.json --config=../config/time_src_ctl.json
#./time_src_ctl --out=../tmp/time_monitor.json
