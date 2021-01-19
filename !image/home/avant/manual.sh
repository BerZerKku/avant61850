#ROOTFS=/var/raspbian10
#DIR=/home/pi
ROOTFS=/var/ubuntu20
DIR=/root

#cp /etc/resolv.conf $DIR/etc

#mountpoint -q $DIR/proc || mount -t proc /proc $DIR/proc/
#mountpoint -q $DIR/sys  || mount --rbind /sys  $DIR/sys/
#mountpoint -q $DIR/dev  || mount --rbind /dev  $DIR/dev/

echo "Start chroot"
chroot $ROOTFS sh -c "cd $DIR; /bin/bash"
