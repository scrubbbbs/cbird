#!/bin/bash

# try to corrupt database by killing the process
CBIRD="$1"
DIR="$2"
SIGNAL=9

while [ 1 ]; do
 
$CBIRD -use "$DIR" -i.bsize 1 -update &
sleep 2 
kill -$SIGNAL $(jobs -p) 
wait $(jobs -p)
$CBIRD -use "$DIR" -select-all -first -remove

done
