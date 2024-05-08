#!/bin/sh

WRITEFILE=$1
WRITESTR=$2

if [ $# -ne 2 ]
then
    echo "Usage: writer.sh <path> <string>"
    exit 1
fi

if [ ! -f $WRITEFILE ]
then
    FILEDIR=$(dirname $WRITEFILE)
    if [ ! -d $FILEDIR ]
    then
        mkdir -p $FILEDIR
        if [ $? -ne 0 ]
        then
            exit 1
        fi
    fi
    touch $WRITEFILE
    echo $WRITESTR > $WRITEFILE
fi

echo $WRITESTR > $WRITEFILE
