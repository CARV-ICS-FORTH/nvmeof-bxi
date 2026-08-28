# bench_chroma.py - Updated for Chroma v0.5+ API
import argparse
import time
import numpy as np
import sys
import pysqlite3

sys.modules["sqlite3"] = pysqlite3
import chromadb
from statistics import mean
from tqdm import tqdm
import os

# === CLI Arguments ===
parser = argparse.ArgumentParser(description='Benchmark Chroma with custom persist directory.')
parser.add_argument('-p', '--persist-directory', required=True, help='Path to persist directory')
args = parser.parse_args()

persist_dir = args.persist_directory
os.makedirs(persist_dir, exist_ok=True)

# === Config ===
N = 100_000        # number of vectors to insert
D = 1536           # vector dimension (set to your model dim)
BATCH = 2000       # upsert batch size
QUERY_COUNT = 200  # number of queries to run
K = 10             # neighbors to return

# === Initialize Chroma Persistent Client ===
client = chromadb.PersistentClient(path=persist_dir)

# === Create / Reset Collection ===
collection_name = "bench_collection"
if collection_name in [c.name for c in client.list_collections()]:
    client.delete_collection(name=collection_name)
col = client.create_collection(name=collection_name, metadata={"dim": D})

# === Generate and Insert Vectors ===
def gen_vectors(total, dim):
    for i in range(0, total, BATCH):
        size = min(BATCH, total - i)
        yield np.random.random((size, dim)).astype(np.float32), [f"id_{i+j}" for j in range(size)]

print("Starting inserts...")
t0 = time.perf_counter()
total_inserted = 0
for vecs, ids in gen_vectors(N, D):
    col.upsert(ids=ids, embeddings=vecs.tolist())
    total_inserted += len(ids)
t1 = time.perf_counter()
insert_time = t1 - t0
print(f"Inserted {total_inserted} vectors in {insert_time:.2f}s -> {total_inserted / insert_time:.1f} vec/s")

# === Warmup Queries ===
print("Warming up queries...")
for _ in range(10):
    q = np.random.random((D,)).astype(np.float32).tolist()
    col.query(query_embeddings=[q], n_results=K)

# === Measured Queries ===
print("Running measured queries...")
latencies = []
for _ in tqdm(range(QUERY_COUNT)):
    q = np.random.random((D,)).astype(np.float32).tolist()
    t0 = time.perf_counter()
    col.query(query_embeddings=[q], n_results=K)
    t1 = time.perf_counter()
    latencies.append((t1 - t0) * 1000.0)  # ms

latencies.sort()

def pct(p):
    i = int(len(latencies) * p / 100)
    return latencies[min(i, len(latencies) - 1)]

print("Query latency ms (approx):")
print(f"  mean: {mean(latencies):.2f} ms")
print(f"  p50:  {pct(50):.2f} ms")
print(f"  p95:  {pct(95):.2f} ms")
print(f"  p99:  {pct(99):.2f} ms")
