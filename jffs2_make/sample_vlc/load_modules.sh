#!/bin/sh

insmod vmm.ko mmz=anonymous,0,0xA2000000,64M:sram,0,0xa7000000,4M:huge,0,0xc0000000,128K anony=1

/bin/echo /sbin/mdev > /proc/sys/kernel/hotplug
mkdir -p /lib/firmware
cp rtthread_arc.bin /lib/firmware/
insmod rtvbus.ko ra=0xa7e00000 rs=65536 fn=rtthread_arc.bin fa=0xa7f00000

insmod media_process.ko
insmod isp.ko
insmod vpu.ko
insmod enc.ko
insmod vou.ko
insmod jpeg.ko
insmod bgm.ko
insmod fd.ko
insmod vbus_ac.ko

