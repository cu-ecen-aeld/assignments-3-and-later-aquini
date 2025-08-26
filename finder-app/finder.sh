#!/bin/bash
#

FILESDIR=${1}
SEARCHSTR=${2}

if [ $# != 2 ]; then
    echo "ERROR: Illegal number of arguments"
    echo "USE:" 
    echo "    $0 <FILES-DIR> <STRING-TO-SEARCH>"
    exit 1
fi

if [ ! -d ${FILESDIR} ]; then
    echo "ERROR: ${FILESDIR}: Not a directory"
    exit 1
fi

if [ -e ${SEARCHSTR} ]; then
    echo "ERROR: Empty search string"
    exit 1
fi

# save the file list produced by find into an array. 
# https://www.gnu.org/software/bash/manual/html_node/Arrays.html
FILES=($(find -L ${FILESDIR} -type f))

LINES=0
for F in ${FILES[@]}; do
    LINES=$((LINES + $(grep "${SEARCHSTR}" $F | wc -l)))
done

echo "The number of files are ${#FILES[@]} and the number of matching lines are ${LINES}"
