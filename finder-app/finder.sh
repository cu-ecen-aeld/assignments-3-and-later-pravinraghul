#!/bin/sh

FILESDIR=$1
SEARCHSTR=$2

# check if the parameters are given
if [ $# -ne 2 ]
then
    exit 1
fi

# check whether the directory exist in the path
if [ ! -d $FILESDIR ]
then
    exit 1
fi

no_of_files=$( grep $SEARCHSTR -r $FILESDIR -l | wc -l )
no_of_lines=$( grep $SEARCHSTR -r $FILESDIR | wc -l )

echo "The number of files are $no_of_files and the number of matching lines are $no_of_lines"