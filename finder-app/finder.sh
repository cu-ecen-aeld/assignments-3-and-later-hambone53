#!/bin/sh

if [ $2 == '' ]; then
    echo "No search string specified."
    exit 1
fi

if [ $1 == '' ]; then
    echo "No directory specified in argument 1."
    exit 1
fi

if [ ! -d "$1" ]; then
    echo "No valid directory specified"
    exit 1
fi

numLines=$(grep -r $2 $1 | wc -l)
numFiles=$(grep -rl $2 $1 | wc -l)

echo "The number of files are $numFiles and the number of matching lines are $numLines"