#!/bin/bash

KERNEL=kernel7
KERNEL_PATH=${KERNEL_PATH:-$(dirname $(readlink -f $0))}
INSTALL_PATH=${INSTALL_PATH:-${KERNEL_PATH}/!build}

export ARCH=${ARCH:-arm}
export CROSS_COMPILE=${CROSS_COMPILE:-${KERNEL_PATH}/../tools/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-}

make_config() {
	# Enable CONFIG_MARVELL_PHY as module : 
	# 	Device Drivers → Network Device Support → PHY Device support and infrastructure → Drivers for Marvell PHYs = M
	
	# Enable RTC DS3231 as module:
	#   Device Drivers → Real Time Clock → <M> Dallas/Maxim DS1307/37/38/39/40, ST M41T00, EPSON RX-8025
	
	#make bcm2709_defconfig
	#make bcmrpi_defconfig
	make -C $KERNEL_PATH menuconfig
	#make -C $KERNEL_PATH oldconfig
}

make_image() {
	make -C $KERNEL_PATH -j $(nproc) zImage 
}

make_dtbs() {
	make -C $KERNEL_PATH -j $(nproc) dtbs 	
}

make_modules() {
	make -C $KERNEL_PATH -j $(nproc) modules 
}

make_install() {
	INSTALL_KERNEL_PATH=${INSTALL_PATH}/boot

	export INSTALL_MOD_PATH=${INSTALL_PATH}
	export INSTALL_DTBS_PATH=${INSTALL_KERNEL_PATH}/overlays

	if [ -f localversion-rt ]; then
		echo "*** KERNEL RT ***"
		KERNEL+="rt"
	fi

	if [ -d $INSTALL_PATH ]; then
		rm -rf $INSTALL_PATH
	fi

	if [ -d $INSTALL_PATH.old ]; then
		rm -rf $INSTALL_PATH.old
	fi	

	mkdir -p ${INSTALL_KERNEL_PATH}
	mkdir -p ${INSTALL_DTBS_PATH}
	$KERNEL_PATH/scripts/mkknlimg ${KERNEL_PATH}/arch/arm/boot/zImage ${INSTALL_KERNEL_PATH}/$KERNEL.img

	make -C ${KERNEL_PATH} -j $(nproc) modules_install 
	make -C ${KERNEL_PATH} -j $(nproc) dtbs_install 

	mkdir -p $INSTALL_PATH/build_config
	cp -f $KERNEL_PATH/.config $INSTALL_PATH/build_config
	cp -f $KERNEL_PATH/build.sh $INSTALL_PATH/build_config

	if [ -d ${INSTALL_KERNEL_PATH}.old ]; then
		mv ${INSTALL_KERNEL_PATH}.old/* ${INSTALL_KERNEL_PATH}/ 
		rm -rf ${INSTALL_KERNEL_PATH}.old
	fi

	if [ -d ${INSTALL_DTBS_PATH}.old ]; then
		if [ "$(ls -A ${INSTALL_DTBS_PATH}.old)" ]; then
			mv ${INSTALL_DTBS_PATH}.old/* ${INSTALL_DTBS_PATH}/ 
		fi
		rm -rf ${INSTALL_DTBS_PATH}.old
	fi
}

make_clean() {
	mv .config .config.temp
	make -C ${KERNEL_PATH} -j $(nproc) mrproper
	rm -rf ${INSTALL_PATH}
	mv .config.temp .config
}

make_config
make_image
make_modules
make_dtbs
make_install
# make_clean


