#!/bin/bash

inputFile='rawResults.csv'
outputFile='results.md'

if [ -f $outputFile ] ; then
  rm $outputFile
fi

IFS=$'\n'; set -f; list=($(<$inputFile))
numResults=$(expr ${#list[@]} / 6)
echo '# Results' >> $outputFile
echo '' >> $outputFile
for i in $(seq 0 $(expr $numResults - 1 ))
do
  IFS=',' read -ra params <<< ${list[$(expr 6 * $i)]}
  IFS=',' read -ra cmResults <<< ${list[$(expr $(expr 6 * $i) + 1)]}
  IFS=',' read -ra umResults <<< ${list[$(expr $(expr 6 * $i) + 2)]}
  IFS=',' read -ra auResults <<< ${list[$(expr $(expr 6 * $i) + 3)]}
  IFS=',' read -ra rdResults <<< ${list[$(expr $(expr 6 * $i) + 4)]}
  IFS=',' read -ra btResults <<< ${list[$(expr $(expr 6 * $i) + 5)]}
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
  echo '| `CM` | `UM` | `AU` | `RD` | `BT` |' >> $outputFile
  echo '| --- | --- | --- | --- |' >> $outputFile
  echo '|' ${umResults[0]} '|' ${cmResults[0]} '|' ${auResults[0]} '|' ${rdResults[0]} '|' ${btResults[0]} '|' >> $outputFile
  echo '' >> $outputFile
  echo '### Operation Latencies' >> $outputFile
  echo '' >> $outputFile
  echo '#### **`insert`**' >> $outputFile
  echo '' >> $outputFile
  echo '| | 50.0p | 95.0p | 99.0p | 99.9p |' >> $outputFile
  echo '| --- | --- | --- | --- | --- |' >> $outputFile
  echo '| **CM** |' ${cmResults[1]} '|' ${cmResults[2]} '|' ${cmResults[3]} '|'  ${cmResults[4]} '|' >> $outputFile
  echo '| **UM** |' ${umResults[1]} '|' ${umResults[2]} '|' ${umResults[3]} '|' ${umResults[4]} '|' >> $outputFile
  echo '| **AU** |' ${auResults[1]} '|' ${auResults[2]} '|' ${auResults[3]} '|' ${auResults[4]} '|' >> $outputFile
  echo '| **RD** |' ${rdResults[1]} '|' ${rdResults[2]} '|' ${rdResults[3]} '|' ${rdResults[4]} '|' >> $outputFile
  echo '| **BT** |' ${btResults[1]} '|' ${btResults[2]} '|' ${btResults[3]} '|' ${btResults[4]} '|' >> $outputFile
  echo '' >> $outputFile
  echo '#### **`lookup`**' >> $outputFile
  echo '' >> $outputFile
  echo '| | 50.0p | 95.0p | 99.0p | 99.9p |' >> $outputFile
  echo '| --- | --- | --- | --- | --- |' >> $outputFile
  echo '| **CM** |' ${cmResults[5]} '|' ${cmResults[6]} '|' ${cmResults[7]} '|'  ${cmResults[8]} '|' >> $outputFile
  echo '| **UM** |' ${umResults[5]} '|' ${umResults[6]} '|' ${umResults[7]} '|' ${umResults[8]} '|' >> $outputFile
  echo '| **AU** |' ${auResults[5]} '|' ${auResults[6]} '|' ${auResults[7]} '|' ${auResults[8]} '|' >> $outputFile
  echo '| **RD** |' ${rdResults[5]} '|' ${rdResults[6]} '|' ${rdResults[7]} '|' ${rdResults[8]} '|' >> $outputFile
  echo '| **BT** |' ${btResults[5]} '|' ${btResults[6]} '|' ${btResults[7]} '|' ${btResults[8]} '|' >> $outputFile
  echo '' >> $outputFile
  echo '#### **`remove`**' >> $outputFile
  echo '' >> $outputFile
  echo '| | 50.0p | 95.0p | 99.0p | 99.9p |' >> $outputFile
  echo '| --- | --- | --- | --- | --- |' >> $outputFile
  echo '| **CM** |' ${cmResults[9]} '|' ${cmResults[10]} '|' ${cmResults[11]} '|'  ${cmResults[12]} '|' >> $outputFile
  echo '| **UM** |' ${umResults[9]} '|' ${umResults[10]} '|' ${umResults[11]} '|' ${umResults[12]} '|' >> $outputFile
  echo '| **AU** |' ${auResults[9]} '|' ${auResults[10]} '|' ${auResults[11]} '|' ${auResults[12]} '|' >> $outputFile
  echo '| **RD** |' ${rdResults[9]} '|' ${rdResults[10]} '|' ${rdResults[11]} '|' ${rdResults[12]} '|' >> $outputFile
  echo '| **BT** |' ${btResults[9]} '|' ${btResults[10]} '|' ${btResults[11]} '|' ${btResults[12]} '|' >> $outputFile
  echo '' >> $outputFile
done
