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
  IFS=',' read -ra cmResults <<< ${list[$(expr $(expr 3 * $i) + 2)]}
  echo '## Result Set' $i >> $outputFile
  echo '' >> $outputFile
  echo '### Simulation Parameters' >> $outputFile
  echo '' >> $outputFile
  echo '| `nOpCount` | `nInitialSize` | `nMaxSize` | `nWorking`  | `pInsert`  | `pLookup`  | `pRemove`  | `pWorking`  | `pMiss`  |' >> $outputFile
  echo '| --- | --- | --- | --- | --- | --- | --- | --- | --- |' >> $outputFile
  echo '|' ${params[0]} '|' ${params[1]} '|' ${params[2]} '|' ${params[3]} '|' ${params[4]} '|' ${params[5]} '|' ${params[6]} '|' ${params[7]} '|' ${params[8]} '|' >> $outputFile
  echo '' >> $outputFile
  echo '### Final Table Size' >> $outputFile
  echo '' >> $outputFile
  echo '| `UM` | `CM` |' >> $outputFile
  echo '| --- | --- |' >> $outputFile
  echo '|' ${umResults[0]} '|' ${cmResults[0]} '|' >> $outputFile
  echo '' >> $outputFile
  echo '### Operation Latencies' >> $outputFile
  echo '' >> $outputFile
  echo '| | UM 50.0p | UM 95.0p | UM 99.0p | UM 99.9p | CM 50.0p | CM 95.0p | CM 99.0p | CM 99.9p |' >> $outputFile
  echo '| --- | --- | --- | --- | --- | --- | --- | --- | --- |' >> $outputFile
  echo '| **`insert`** |' ${umResults[1]} '|' ${umResults[2]} '|' ${umResults[3]} '|' ${umResults[4]} '|' ${cmResults[1]} '|'  ${cmResults[2]} '|'  ${cmResults[3]} '|'  ${cmResults[4]} '|' >> $outputFile
  echo '| **`lookup`** |' ${umResults[5]} '|' ${umResults[6]} '|' ${umResults[7]} '|' ${umResults[8]} '|' ${cmResults[5]} '|'  ${cmResults[6]} '|'  ${cmResults[7]} '|'  ${cmResults[8]} '|' >> $outputFile
  echo '| **`remove`** |' ${umResults[9]} '|' ${umResults[10]} '|' ${umResults[11]} '|' ${umResults[12]} '|' ${cmResults[9]} '|'  ${cmResults[10]} '|'  ${cmResults[11]} '|'  ${cmResults[12]} '|' >> $outputFile
  echo '' >> $outputFile
done
