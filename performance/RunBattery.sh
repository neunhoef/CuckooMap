#!/bin/bash

seed=119384893020

inputFile='battery.csv'
outputFile='rawResults.csv'

if [ -f $outputFile ] ; then
  rm $outputFile
fi

IFS=$'\n'; set -f; list=($(<$inputFile))
for item in ${list[@]}
do
  echo $item >> $outputFile
  for useCuckoo in {0..1}
  do
    params=$(echo $item | sed 's/,/ /g')
    command=$(echo '../PerformanceTest' $useCuckoo $params $seed)
    result=$(eval $command)
    echo $result >> $outputFile
  done
done

$(./FormatResults.sh)
rm $outputFile
