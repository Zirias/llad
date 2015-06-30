#!/bin/sh

# This example scripts just prints the first 4 arguments it was called with.
# When running llad normally, this will end up in the "daemon" facility of
# syslog.

echo matched: "\"$1\""
echo capture \#1: "\"$2\""
echo capture \#2: "\"$3\""
echo capture \#3: "\"$4\""
echo ... doing nothing.

