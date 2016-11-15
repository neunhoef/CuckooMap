#!/bin/bash

inputFile='rawResults.csv'
outputFile='results.md'

if [ -f $outputFile ] ; then
  rm $outputFile
fi

IFS=$'\n'; set -f; list=($(<$inputFile))
numResults=$(expr ${#list[@]} / 3)
echo '# Results' >> $outputFile
echo '' >> $outputFile
for i in $(seq 0 $(expr $numResults - 1 ))
do
  IFS=',' read -ra params <<< ${list[$(expr 3 * $i)]}
  IFS=',' read -ra umResults <<< ${list[$(expr $(expr 3 * $i) + 1)]}
  IFS=',' read -ra ccResults <<< ${list[$(expr $(expr 3 * $i) + 2)]}
  echo '## Result Set' $i >> $outputFile
  echo '' >> $outputFile
  echo '### Simulation Parameters' >> $outputFile
  echo '' >> $outputFile
  echo '| `nOpCount` | `nInitialSize` | `nMaxSize` | `nWorking`  | `pInsert`  | `pLookup`  | `pRemove`  | `pWorking`  | `pMiss`  |' >> $outputFile
  echo '| --- | --- | --- | --- | --- | --- | --- | --- | --- |' >> $outputFile
  echo '|' ${params[0]} '|' ${params[1]} '|' ${params[2]} '|' ${params[3]} '|' ${params[4]} '|' ${params[5]} '|' ${params[6]} '|' ${params[7]} '|' ${params[8]} '|' >> $outputFile
  echo '' >> $outputFile
  echo '### Operation Latencies' >> $outputFile
  echo '' >> $outputFile
  echo '| | UM 50.0p | UM 95.0p | UM 99.0p | UM 99.9p | CM 50.0p | CM 95.0p | CM 99.0p | CM 99.9p |' >> $outputFile
  echo '| --- | --- | --- | --- | --- | --- | --- | --- | --- |' >> $outputFile
  echo '| **`insert`** |' ${umResults[0]} '|' ${umResults[1]} '|' ${umResults[2]} '|' ${umResults[3]} '|' ${ccResults[0]} '|'  ${ccResults[1]} '|'  ${ccResults[2]} '|'  ${ccResults[3]} '|' >> $outputFile
  echo '| **`lookup`** |' ${umResults[4]} '|' ${umResults[5]} '|' ${umResults[6]} '|' ${umResults[7]} '|' ${ccResults[4]} '|'  ${ccResults[5]} '|'  ${ccResults[6]} '|'  ${ccResults[7]} '|' >> $outputFile
  echo '| **`remove`** |' ${umResults[8]} '|' ${umResults[9]} '|' ${umResults[10]} '|' ${umResults[11]} '|' ${ccResults[8]} '|'  ${ccResults[9]} '|'  ${ccResults[10]} '|'  ${ccResults[11]} '|' >> $outputFile
  echo '' >> $outputFile
done
