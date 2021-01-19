#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=${PWD}/tools/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-
export KDIR=${PWD}/linux-raspbian-3.18.11

make_kernel() {
	${KDIR}/my_script.sh
}

make_kernel
