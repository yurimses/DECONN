#!/bin/bash

java -Xmx4096m -jar ss1s1_00.jar &

sleep 20

./tunslip6_tun0 -a 127.0.0.1 aaaa::1/64
