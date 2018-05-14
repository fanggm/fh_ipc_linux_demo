#!/bin/sh
./load_modules.sh
sleep 1
./reset_sensor.sh
./sample_vlcview 192.168.70.25 1234
