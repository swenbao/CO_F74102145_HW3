#!/usr/bin/bash

>output"$1".txt
make $1
for block in $(seq 3 6)
do
    for set in $(seq 0 $((6-$block)))
    do
        way=$((6-$set-$block))
        
        #change config and print config
        printf "[cache]\nSet = $((2**$set))\nWay = $((2**$way))\nBlockSize = $((2**$block))\nPolicy = \"$1\"" > config.conf
#        echo $set $way $block        
        #make score
        make test 2>&1 | grep -A 3 ==== >> output"$1".txt
    done
done
