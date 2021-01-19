#!/bin/bash

# Example:
# dtc -I dtb -O dts sun8i-h3-orangepi-pc.dtb -o /tmp/tmp.dts

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Add file *.dtb or *.dtbo"
    exit 1
fi

FILE="$1"
FILEEXT="${FILE##*.}"

if [ "$FILEEXT" != "dtb" ] && [ "$FILEEXT" != "dtbo" ]; then
	echo "Illegal file extension"
	echo "Add file *.dtb or *.dtbo"
fi

FILENAME="${FILE%%.*}"

dtc -I dtb -O dts $FILE -o $FILENAME.dts