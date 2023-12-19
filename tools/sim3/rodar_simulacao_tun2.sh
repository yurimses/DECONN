#!/bin/bash

java -Xmx4096m -jar ss1s3_00.jar &

sleep 20

./tunslip6_tun2 -a 127.0.0.3 aaaa::1/64
