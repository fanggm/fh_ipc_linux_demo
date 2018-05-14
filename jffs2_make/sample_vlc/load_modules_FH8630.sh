#!/bin/sh

insmod vmm.ko mmz=anonymous,0,0xA1c00000,34M:huge,0,0xc0000000,128K anony=1

/bin/echo /sbin/mdev > /proc/sys/kernel/hotplug
mkdir -p /lib/firmware
cp rtthread_arc_FH8630.bin /lib/firmware/
insmod rtvbus.ko ra=0xa3e00000 rs=65536 fn=rtthread_arc_FH8630.bin fa=0xa3f00000

insmod media_process.ko
insmod isp.ko
insmod vpu.ko
insmod enc.ko
insmod vou.ko
insmod jpeg.ko
insmod bgm.ko
insmod fd.ko
insmod vbus_ac.ko

