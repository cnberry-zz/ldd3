#!/bin/sh

module="scullc"
device="scullc"

# invoke rmmod with all arguments we got
/sbin/rmmod $module $* || exit 1

# Remove stale nodes

rm -f /dev/${device}[0-3] 

