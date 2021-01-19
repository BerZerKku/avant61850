#!/bin/bash

function mnt() {
	echo "MOUNTING"
	mount -t proc proc ${2}/proc
	mount -o bind /sys ${2}/sys
	mount -o bind /dev ${2}/dev
	mkdir -p ${2}/root/bvp
	mount -o bind /home/avant/bvp ${2}/root/bvp
	mkdir -p ${2}/root/iec61850
	mount -o bind /home/avant/iec61850 ${2}/root/iec61850
}

function umnt() {
	echo "UNMOUNTING"
	umount ${2}/proc
	umount ${2}/sys
	umount ${2}/dev
	umount ${2}/root/bvp
	umount ${2}/root/iec61850
}

if [ "$1" == "-m" ] && [ -n "$2" ]; then
	mnt $1 $2
elif [ "$1" == "-u" ] && [ -n "$2" ]; then
	umnt $1 $2
else
	echo ""
	echo "Either 1'st, 2'nd or both parameters were missing"
	echo ""
	echo "1'st parameter can be one ot these: -m(mount) OR -u(unmount)"
	echo "2'nd parameter is the full path of rootfs directory (with trailing '/')"
	echo ""
	echo "For example: ch-mount -m /media/sdcard"
	echo ""
	echo "1st parameter : ${1}"
	echo "2nd parameter : ${2}"
fi
