#!/bin/bash -x

program="slogger"

#check that there is a single arguement
if [ $# -ne 1 ]; then
    echo "Usage: $0 -s or -c"
    exit 1
fi

#check that the arguement is either -s or -c
if [ $1 != "-s" ] && [ $1 != "-c" ]; then
    echo "Usage: $0 -s or -c"
    exit 1
fi

#if argument is -s set the program to the server
#else if the argument is -c set the program to the client
if [ $1 == "-s" ]; then
    program="$program"_server
elif [ $1 == "-c" ]; then
    program="$program"_client
else
    echo "Usage: $0 -s or -c"
    exit 1
fi

#sync with the build serve
rsync -a yak-01.sysnet.ucsd.edu:/usr/local/home/ssgrant/bbd/src/bin/ ./bin;
rsync -a yak-01.sysnet.ucsd.edu:/usr/local/home/ssgrant/bbd/src/configs/ ./configs;
rsync -a yak-01.sysnet.ucsd.edu:/usr/local/home/ssgrant/bbd/src/workloads/ ./workloads;

ts='taskset -c 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,63,65,67,69,71,73,75,77,79'

        // #define MTU_SIZE 1024
#run the program
export JE_MALLOC_CONF="narenas:1"
$ts ./bin/"$program"
 
