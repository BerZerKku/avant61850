#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=~/workspace-hd/rpi-kernel/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-
export KDIR=~/workspace-hd/rpi-kernel/linux-rpi-3.18.11
export INSTALL_MOD_PATH=$PWD/drivers

make modules