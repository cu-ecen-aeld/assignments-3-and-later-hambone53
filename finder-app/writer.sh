#!/bin/bash
# Takes in a file to write and a string to write

if [ $# -lt 2 ]; then
    echo "Not enough arguments: writer.sh \"file to write\" \"string to write\""
    exit 1
fi

if [ $# -gt 2 ]; then
    echo "Too many arguments: writer.sh \"file to write\" \"string to write\""
    exit 1
fi

DIR="$(dirname "$1")"

if mkdir -p $DIR; then
    echo "Success making directory"
else
    echo "Failed to make directory"
    exit 1
fi

if echo $2 > $1; then
    echo "Success writing!"
else
    echo "failed to write file"
    exit 1
fi