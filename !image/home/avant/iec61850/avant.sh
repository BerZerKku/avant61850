#!/bin/bash
BASEDIR=$(dirname "$0")
FLAG=""
PROCESS_NAME="avant"

if [ "$1" = "stop" ]; then
  PID=$(pidof $PROCESS_NAME)
  if [ "$PID" != "" ]; then
    $(kill $PID)
  fi
  exit
fi

if [ "$(arch)" = "x86_64" ]; then
    ARCH_SUBDIR="linux-x64"
elif [ "$(arch)" = "armv7l" ]; then
    ARCH_SUBDIR="linux-aarch32"
    FLAG+=" taskset -c 2,3" # run the process exclusively on cores 2 and 3
fi

cd $BASEDIR
chrt -rr 99 $FLAG bin/$ARCH_SUBDIR/$PROCESS_NAME config/upask_minimal_tx_rx.json

