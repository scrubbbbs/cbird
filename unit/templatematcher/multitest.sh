#!/bin/bash

# iterate to find best tuning
for x in {15..70}; do

export ORB_THRESH=$x
echo "========================================================="
echo "testing ORB_THRESH=$ORB_THRESH"
./runtest 2>&1 > log-$x.txt
./parselog.sh log-$x.txt

done
