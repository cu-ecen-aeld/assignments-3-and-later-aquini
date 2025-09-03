#!/bin/sh
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

LIST="$(find -L ${FILESDIR} -type f)"
LINES=0
FILES=0
for F in ${LIST}; do
    LINES=$((LINES + $(grep "${SEARCHSTR}" $F | wc -l)))
    FILES=$((FILES + 1))
done

echo "The number of files are ${FILES} and the number of matching lines are ${LINES}"
