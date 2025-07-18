import json
import re
import sys
from collections import defaultdict

if len(sys.argv) != 2:
    print("Uso: python3 check_allgather_chunks.py <file_schedule.json>")
    sys.exit(1)

json_file = sys.argv[1]

# Carica il file JSON
with open(json_file) as f:
    data = json.load(f)

chunk_paths = data["8-Chunk paths"]

# Struttura: received_chunks[destination][chunk] = set(sorgenti)
received_chunks = defaultdict(lambda: defaultdict(set))

# Pattern per estrarre i valori da ogni chiave
pattern = re.compile(r"Demand at (\d+) for chunk (\d+) from (\d+) met by epoch \d+")

# Rilevamento dinamico dei nodi e chunk
all_gpus = set()
all_chunks = set()

for key in chunk_paths:
    match = pattern.match(key)
    if match:
        dst, chunk, src = map(int, match.groups())
        received_chunks[dst][chunk].add(src)
        all_gpus.update([dst, src])
        all_chunks.add(chunk)

# Verifica
all_ok = True
for dst in sorted(all_gpus):
    for chunk in sorted(all_chunks):
        expected_sources = all_gpus - {dst}
        received = received_chunks[dst][chunk]
        if received != expected_sources:
            all_ok = False
            missing = expected_sources - received
            print(f"GPU {dst} is missing chunk {chunk} from: {sorted(missing)}")
        else:
            print(f"GPU {dst} received chunk {chunk} from all other GPUs.")

if all_ok:
    print("\n✔️ All chunks were correctly received by all GPUs. AllGather is complete.")
else:
    print("\nSome chunks were not received by all GPUs. Check the errors above.")
