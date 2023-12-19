#!/bin/bash

java -Xmx4096m -jar 20grid_02.jar &

sleep 20

./tunslip6 -a 127.0.0.1 aaaa::1/64
