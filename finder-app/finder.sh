#!/bin/bash

if [ $# != 2 ]; then
	echo "Wrong number of arguments"
	exit 1
fi

filesdir="$1"
searchstr="$2"

if [ ! -d "$filesdir" ]; then
	echo "dir $filesdir doesn't exists"
	exit 1
fi

files=$(ls "$filesdir" |wc -l)
lines=$(grep "$searchstr" "$filesdir"/* |wc -l)
echo "The number of files are $files and the number of matching lines are $lines"

exit 0