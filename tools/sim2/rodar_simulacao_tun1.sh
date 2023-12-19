#!/bin/bash

java -Xmx4096m -jar ss1s2_00.jar &

sleep 20

./tunslip6_tun1 -a 127.0.0.2 aaaa::1/64
