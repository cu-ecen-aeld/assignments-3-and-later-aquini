#!/bin/bash
#

WRITEFILE=${1}
WRITESTR=${2}

if [ $# != 2 ]; then
    echo "ERROR: Illegal number of arguments"
    echo "USE:" 
    echo "    $0 <PATH-TO-FILE> <STRING-TO-WRITE>"
    exit 1
fi

if [ -e ${WRITESTR} ]; then
    echo "ERROR: Empty search string"
    exit 1
fi

DIR=$(dirname ${WRITEFILE})
if [ ! -d ${DIR} ]; then
    mkdir -p ${DIR} &> /dev/null
    if [ $? != 0 ]; then
        echo "ERROR: cannot create dir ${DIR}"
	exit 1
    fi   
fi

touch ${WRITEFILE} &> /dev/null
if [ $? != 0 ]; then
    echo "ERROR: cannot create file ${WRITEFILE}"
    rmdir ${DIR}
    exit 1
fi

echo "${WRITESTR}" > ${WRITEFILE}
