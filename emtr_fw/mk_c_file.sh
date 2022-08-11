#!/bin/bash

VERS=$1

if [ -z ${VERS} ]
then
  echo "Specify existing version"
  exit
fi

if [ ! -d ${VERS} ]
then
  echo "Folder ${VERS} does not exist"
  exit
fi

INP=v${VERS}/pw_cable-ota-hdr-${VERS//./_}.bin
OUTP=v${VERS}/emtr_fw.c

echo "Input : ${INP}"
echo "Output: ${OUTP}"

if [ ! -f ${INP} ]
then
  echo "${INP} not found"
  exit
fi

python3 gen_c_file.py emtrFwBin  ${INP} ${OUTP}

if [ ! -f ${OUTP} ]
then
  echo "${OUTP} not found"
  exit
fi

sed -i "1i // EMTR ${VERS}" ${OUTP}
sed -i 's/^unsigned char .*$/const unsigned char emtrFwBin[] = {/' ${OUTP} 
sed -i 's/^unsigned int .*$/const unsigned int emtrFwBinLen = sizeof(emtrFwBin);/' ${OUTP}
