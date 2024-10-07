"""
Script reads a timestamped edge list format, deduplicates entries with the same src and dst values,
 deduplicates entries with exchanged src and dst values, sorts the edges by timestamp and outputs the same edge list without the timestamps.

The first argument should be the path to the input edge list with the extension .tel. The output will be written to the 
same path but with the extension .el.

The script assumes that the input format is '<src>\t<dst>\t<weight>\t<timstamp>' with one edge per line.
The output format is '<src> <dst>'.
"""

import sys
import os

def parse_edge(line):
    src, dst, _, timestamp = line.split()
    return (int(src), int(dst), int(timestamp))

input_path = sys.argv[1]
assert(input_path.endswith('.tel'))

output_path = input_path.replace('.tel', '.el')

edges = []
dedup_set = set()
with open(input_path, 'r') as input_file:
    for line in input_file:
        if line.startswith('#') or line.startswith('%'):
            continue
        src, dst, timestamp = parse_edge(line)
        if src > dst:
            dst, src = src, dst
        if src == dst:
            continue   # We do not allow self edges
        if (src, dst) not in dedup_set:
            dedup_set.add((src, dst))
            edges.append(parse_edge(line))

edges = sorted(edges, key=lambda e: e[2])

# On my system open(file, 'w') appends to the end of the file, let's avoid this.
if os.path.exists(output_path):
    os.remove(output_path)

with open(output_path, 'w') as output_file:
    for e in edges:
        output_file.write("%s %s\n" % (e[0], e[1]))
