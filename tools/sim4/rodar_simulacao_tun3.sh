#!/bin/bash

java -Xmx4096m -jar ss1s4_00.jar &

sleep 20

./tunslip6_tun3 -a 127.0.0.4 aaaa::1/64
