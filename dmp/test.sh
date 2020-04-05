#!/bin/bash
echo "Make module"
make
export size=512
insmod dmp.ko
echo "Check module was loaded:"
lsmod | grep "dmp"
echo "Create test block device:"
dmsetup create zero1 --table "0 $size zero"
echo "Check device was successfully created:"
ls -al /dev/mapper/*
echo "Create proxy device:"
dmsetup create dmp1 --table "0 $size dmp /dev/mapper/zero1 0"
echo "Check device was successfully created:"
ls -al /dev/mapper/*
echo "Read&write operation:"
dd if=/dev/random of=/dev/mapper/dmp1 bs=4k count=1
dd of=/dev/null if=/dev/mapper/dmp1 bs=4k count=1
echo "Statistics:"
cat /sys/module/dmp/stat/volumes
