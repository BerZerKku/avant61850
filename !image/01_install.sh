#!/bin/bash

DIR=/boot/install

etc_config() {
	echo "etc_config"

	echo "copy /etc"
	cp -vRT $DIR/etc/ /etc

	echo "set timezone"
	#set system timezone from /etc/timezone (names from /usr/share/zoneinfo)
	dpkg-reconfigure --frontend noninteractive tzdata

	#set auto start and stop script /etc/init.d/prosoft
	chmod +x /etc/init.d/prosoft
	update-rc.d prosoft start 83 2 3 4 5 . stop 1 0 1 6 .
}

boot_config() {
	echo "boot_config"

	echo "delete *.dtb and kernel.img"
	rm /boot/*.dtb
	rm /boot/kernel.img

	echo "copy /boot"
	cp -vRT $DIR/boot/ /boot
}

xr7_config() {
	echo "xr7 config"

	echo "copy /var"
	cp -vRT $DIR/var /var
}

root_config() {
	echo "/root config"

	echo "copy /root"
	cp -vRT $DIR/root /root
}

home_config() {
	echo "/home config"

	echo "copy /home"
	cp -vRT $DIR/home /home

	echo "!!!!!!!!!!!!!!!!!!!!!!!"
	echo " adduser avant"
	echo " passwd avant"
	echo " usermod -aG sudo avant"
	echo "!!!!!!!!!!!!!!!!!!!!!!!"
}

rm_all() {
	rm -rf $DIR
}

boot_config
etc_config
xr7_config
root_config
home_config
#rm_all

echo 
echo "!!! Reboot pls !!!"
echo