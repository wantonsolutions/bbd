import sys
import pickle
from pathlib import Path
from cycler import cycler
import numpy as np


def load_log():
    usage = "Usage: %s <log file>" % sys.argv[0]
    if len(sys.argv) != 2:
        print(usage)
        sys.exit(1)
    #read in pcap from the command line
    logfile = sys.argv[1]

    #try to unpickle the file
    try:
        with open(logfile, 'rb') as f:
            log = pickle.load(f)
    except:
        print("Error: unable to open file")
        sys.exit(1)
    

    output_filename = Path(logfile).stem
    return log, output_filename



log, output_file  = load_log()

cache_size = 32
storage_size = 960
max_logsize = 2**32


cache = [None]*cache_size
cache_hash = {}
cache_index = 0
hits = 0
miss = 0
invalidates = 0
revalidates = 0
highest = 0
max_clients = 12
epochs = [0] * max_clients
positions = [0] * max_clients

dirty = 1
clean = 0
for i, l in enumerate(log):
    op, id, seq, vaddr, size = l

    # if op == "RR":
    if id == 0:
        print(op, id, seq, vaddr, size)
    #Round the vaddr out to the storage block size


    if op == "R":
        #this line should be removed for reads
        vaddr = vaddr - (vaddr % storage_size)
        #check if the vaddr is in the cache
        if vaddr in cache_hash and cache[cache_hash[vaddr]] == (vaddr, clean):
            hits += 1
        else:
            miss += 1

        #set the vaddr to clean again if we read a vale back
        if vaddr in cache_hash and cache[cache_hash[vaddr]] == (vaddr, dirty):
            cache[cache_hash[vaddr]] = (vaddr, clean)
            revalidates += 1

        #check the epoch of the vaddr
        if positions[id] > vaddr + 100000: #the 1000 is a heuristic to check for rollover
            epochs[id]+=1
        positions[id] = vaddr



        #epoch positions is for checking which id is the furthest
        unwraped_log_location = (epochs[id] * max_logsize) + vaddr
        if unwraped_log_location > highest:
            highest = unwraped_log_location
            if cache[cache_index] != None:
                #delete the virtual address
                del cache_hash[cache[cache_index][0]]

            #set the new cache entry as clean 
            cache[cache_index] = (vaddr, clean)
            cache_hash[vaddr] = cache_index
            cache_index = (cache_index + 1) % cache_size
    elif op == "W":

        #We need to round down on the write so we can check if the vaddr is in the cache
        cache_vaddr = vaddr - (vaddr % storage_size)

        if cache_vaddr in cache_hash:
            # print("Write Invalidate")
            cache[cache_hash[cache_vaddr]] = (cache_vaddr, dirty)
            invalidates += 1

print("Hits", hits)
print("Miss", miss)
print("Invalidates", invalidates)
print("Revalidates", revalidates)
hr = round(hits/(hits+miss),2)
ir = round(invalidates/(hits+miss),2)
rr = round(revalidates/(hits+miss),2)
print("Hit Ratio:",hr)
print("Invalidate Ratio", ir)
print("Revalidate Ratio", rr)

# plot out the cache

import matplotlib.pyplot as plt
import numpy as np
x = {}
y = {}
low = 0
measures = len(log)
low = 50000
measures = 1000
for i, l in enumerate(log):
    op, id, vaddr, size = l
    if id not in x:
        x[id] = []
        y[id] = []

    if i > low:
        vaddr = vaddr - (vaddr % storage_size)
        x[id].append(i)
        y[id].append(vaddr)
    
    if i > low + measures:
        break

# get colormap
cmap=plt.cm.gist_rainbow
# build cycler with 5 equally spaced colors from that colormap
c = cycler('color', cmap(np.linspace(0,1,len(x))) )
# supply cycler to the rcParam
plt.rcParams["axes.prop_cycle"] = c

for id in x:
    plt.scatter(x[id], y[id], s=2, label=id)
plt.legend()
plt.savefig(output_file + ".png")