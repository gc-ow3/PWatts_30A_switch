#!/bin/bash

VERS=$1

if [ -z $VERS ]
then
  echo "Need version (n.n.n)"
  exit
fi

espsecure.py sign_data --version 1 --keyfile pw240_prv_signing_key.pem --output pw240_$VERS.bin ../build/pw240.bin
