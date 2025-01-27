#!/bin/bash

# test multiple database writers on the same machine
CBIRD="C:\cbird\cbird.exe"

$CBIRD -select-all -remove

for i in {0..5}; do 
  $CBIRD -i.bsize 10 -i.idxthr 2 -update &
done

for i in {0..10}; do 
  $CBIRD -select-all -first -remove &
  sleep 1
done

wait $(jobs -p)
$CBIRD -update -select-all -verify -similar -show 

