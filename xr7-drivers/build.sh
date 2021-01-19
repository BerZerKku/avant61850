#!/bin/bash
CDIR=$(dirname $(readlink -f $0)) # current path

export ARCH=${ARCH:-arm}
export CROSS_COMPILE=${CROSS_COMPILE:-${CDIR}/../tools/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-}
export KDIR=${KDIR:-${CDIR}/../linux-raspbian-3.18.11}
export INSTALL_MOD_PATH=${INSTALL_MOD_PATH:-${CDIR}/!build}

while [ -n "$1" ]; do
	case "$1" in
		--debug) 
			echo 
			echo "*** Found the --debug option ***" 
			echo
			export DEBUG=y
			;;
	esac
	shift
done

if [ ! -d $INSTALL_MOD_PATH ]; then
	mkdir -p $INSTALL_MOD_PATH;
fi

make -C ${CDIR} clean
make -C ${CDIR} modules

