#!/bin/bash

log=$1
echo -n "nm   "; grep -c "no match" $log
echo -n "q_kp "; grep -c "no keypoints in template" $log
echo -n "c_kp "; grep -c "no keypoints in candidate" $log
echo -n "noma "; grep -c "no keypoint matches" $log
echo -n "<3   "; grep -c "less than 3 keypoint matches" $log
echo -n "xfrm "; grep -c "no transform found" $log
echo -n "proj "; grep -c "no projection" $log
echo -n "hash "; grep -c "projection doesn't match" $log
