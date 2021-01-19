#!/bin/bash

app_install() {
	apt-get update
	apt-get install -y --force-yes raspi-config
	apt-get install -y device-tree-compiler
	apt-get install -y i2c-tools
	apt-get install -y mc
	apt-get install -y chkconfig
}

app_install
