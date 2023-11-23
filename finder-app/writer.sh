#!/bin/bash

# Check the number of arguments (-ne not equal)
if [ $# -ne 2 ]; then
    echo "Wrong number of arguments"
    exit 1
fi

# Assign variables
writefile="$1"
writestr="$2"

# -z true if string is empty
if [ -z "$writefile" ]; then
	echo $writefile is not a file
	exit 1
fi

# Create directory if it doesn't exist
mkdir -p "$(dirname "$writefile")"
echo $writestr > $writefile

# Check if the last command was successful (0 if failed)
if [ "$?" -ne 0 ]; then
	echo "Unable to create the file"
	exit 1
fi

exit 0
