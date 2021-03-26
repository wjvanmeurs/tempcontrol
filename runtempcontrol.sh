#!/bin/bash
#-------------------------------------------------------------------------------
#  	File: 	runtempcontrol.sh
#	Date: 	Mar 25, 2021
# 	Author:	Wil van Meurs
#-------------------------------------------------------------------------------
# 	This script is run at start-up (in local.rc) to run the tempcontrol
#	executable to keep the CPU temperature in safe limits.
#	Because the i2c libraries used by the executable spill open files, the
#	executable crashes after a number of hours.
#	The task of this script is to start a new instance of tempcontrol and
#	log the event to /var/log/tempcontrol.log
#-------------------------------------------------------------------------------
tempControlLogFile="/var/log/tempcontrol.log"

# Remove previous instance of log file: /usr/bin
rm -f $tempControlLogFile

# No write access to group and others:
umask 022

while [ 1 ] 
do
    timedate=`date +%y-%m-%d\ %H:%M`
    echo "$timedate: Starting tempcontrol executable" >> $tempControlLogFile
    tempcontrol 2>> $tempControlLogFile
done
exit 0


