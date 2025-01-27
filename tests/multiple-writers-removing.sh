#!/bin/bash

# test multiple writers trying to remove
CBIRD="$1"
DIR="$2"

while [ 1 ]; do

  for x in {1..5}; do
    $CBIRD -use "$DIR" -select-all -first -remove &
  done
  wait $(jobs -p)

done
