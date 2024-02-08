import dpkt
# from dpkt.ip import IP, IP_PROTO_UDP
# from dpkt.udp import UDP
from dpkt.utils import mac_to_str, inet_to_str
import rdma_header
import roce_packet
import utils
import tqdm
import pickle
import sys
from pathlib import Path

def get_client_map(client_traces, log_len):
    #remove the client with the smallest trace
    min_trace = log_len
    min_client = None
    for c in client_traces:
        if len(client_traces[c])<min_trace:
            min_trace = len(client_traces[c])
            min_client = c
    client_traces.pop(min_client)

    ## at this point we can get the list of clients that we want
    client_map = {}
    i=0
    for c in client_traces:
        client_map[c] = i
        i=i+1
    return client_map

def get_queue_pairs(client_traces, server_traces):
    seq={}
    max_depth = 15
    for c in client_traces:
        for i in range(len(client_traces[c])):
            if i > max_depth:
                break
            seq[client_traces[c][i].rdma_bth.get_psn()] = client_traces[c][i].rdma_bth.get_dest_qp()
    ids = 0
    sqps = {}
    rqps = {}
    for c in server_traces:
        for i in range(len(server_traces[c])):
            if i > max_depth:
                break
            if server_traces[c][i].rdma_bth.get_psn() in seq:
                rqps[server_traces[c][i].rdma_bth.get_dest_qp()] = ids
                sqps[seq[server_traces[c][i].rdma_bth.get_psn()]] = ids
                ids = ids+1
                break
    print("send qp", sqps)
    print("rec qp", rqps)
    return (sqps, rqps) 


def get_log_key(client_traces):
    read_opcode =12
    _, trace = client_traces.popitem()
    key_traces = {}
    for p in trace:
        if p.rdma_bth.opcode == read_opcode:
            key = utils.bytes_to_int(p.rdma_reth.get_remote_key())
            if key not in key_traces:
                key_traces[key] = []
            key_traces[key].append(p)
    #only keep the longest key trace
    max_length = 0
    max_key = None
    for k in key_traces:
        if len(key_traces[k])>max_length:
            max_length = len(key_traces[k])
            max_key = k
    return max_key

def get_min_vaddr(client_traces, rkey):
    min_vaddr = None
    read_opcode = 12
    for c in client_traces:
        for p in client_traces[c]:
            if p.rdma_bth.opcode == read_opcode and utils.bytes_to_int(p.rdma_reth.get_remote_key()) == rkey:
                vaddr = utils.bytes_to_int(p.rdma_reth.get_virtual_addr())
                if min_vaddr == None or vaddr < min_vaddr:
                    min_vaddr = vaddr
    return min_vaddr

def get_log_info(pcap):
    print("priming for log parse")
    i=0
    send_traces = {}
    rec_traces = {}
    max_size = 10000
    for ts, buf in tqdm.tqdm(pcap):
        if i>max_size:
            break
        pkt = roce_packet.RRoCEPacket(buf)
        qp = pkt.rdma_bth.get_dest_qp()
        dest_ip = pkt.get_dst_ip()
        # print("Packet",i,qp,dest_ip)
        if qp not in send_traces and dest_ip == "192.168.1.12":
            send_traces[qp] = []
        elif qp not in rec_traces and dest_ip == "192.168.1.13":
            rec_traces[qp] = []
        if dest_ip == "192.168.1.12":
            send_traces[qp].append(pkt)
        elif dest_ip == "192.168.1.13":
            rec_traces[qp].append(pkt)
        i=i+1

    ## at this point in time we have collected a small subset of the trace
    sqp_id, rqp_id = get_queue_pairs(send_traces, rec_traces)
    cm = get_client_map(send_traces, max_size)
    rkey = get_log_key(send_traces)
    min_vaddr = get_min_vaddr(send_traces, rkey)
    print("[primed] Clients %d rkey %d log vaddr %d" % (len(cm), rkey, min_vaddr))
    # print(cm,rkey,min_vaddr)
    return (sqp_id,rqp_id,cm,rkey,min_vaddr)
    



usage = "Usage: %s <pcap file>" % sys.argv[0]
if len(sys.argv) != 2:
    print(usage)
    sys.exit(1)
#read in pcap from the command line
pcapfile = sys.argv[1]
try:
    f = open(pcapfile, 'rb')
    pcap = dpkt.pcap.Reader(f)
except:
    print("Error reading pcap file %s" % pcapfile)
    print("Usage: %s <pcap file>" % sys.argv[0])
    sys.exit(1)


output_filename = Path(pcapfile).stem + ".log"
sqp_id, rqp_id, cm, rkey, min_vaddr = get_log_info(pcap)
trace = []

print("Parsing log")
sopcode_to_marker = {10: "W", 12: "R"}
ropcode_to_marker = {13: "RRF", 14: "RRM", 15: "RRL", 16: "RR", 17: "A"}
read_opcode = 12
write_opcode = 10

i=0
for ts, buf in tqdm.tqdm(pcap):
    pkt = roce_packet.RRoCEPacket(buf)
    # print(pkt)
    qp = pkt.rdma_bth.get_dest_qp()

    # print(pkt.rdma_bth.opcode)

    dest_ip = pkt.get_dst_ip()
    if qp in sqp_id  and \
        pkt.rdma_bth.opcode in sopcode_to_marker and \
        utils.bytes_to_int(pkt.rdma_reth.get_remote_key()) == rkey and \
        dest_ip == "192.168.1.12":

        id = sqp_id[qp]
        marker = sopcode_to_marker[pkt.rdma_bth.opcode]
        seq = pkt.rdma_bth.get_psn()
        vaddr = utils.bytes_to_int(pkt.rdma_reth.get_virtual_addr())
        size = pkt.rdma_reth.get_dma_length()
        tup = (marker,id,seq,vaddr-min_vaddr,size)
        trace.append(tup)
    elif qp in rqp_id and \
        pkt.rdma_bth.opcode in ropcode_to_marker and \
        dest_ip == "192.168.1.13":

        marker = ropcode_to_marker[pkt.rdma_bth.opcode]
        id = rqp_id[qp]
        seq = pkt.rdma_bth.get_psn()
        size = pkt.udp.ulen
        tup = (marker,id,seq, 0, size)
        trace.append(tup)

    i=i+1

    if i > 100000:
        break


print("log parsed successfully!")

#pickel and write out the file
with open(output_filename, 'wb') as f:
    pickle.dump(trace, f)
print("log written to file %s" % output_filename)

