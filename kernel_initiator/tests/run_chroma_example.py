# run_chroma_example.py (updated for modern Chroma)
import argparse
import os
import chromadb

parser = argparse.ArgumentParser(description='Run Chroma with XFS-mounted persist dir.')
parser.add_argument('-p', '--persist-directory', required=True, help='Path to persist directory')
args = parser.parse_args()

persist_dir = args.persist_directory
os.makedirs(persist_dir, exist_ok=True)

# Use new PersistentClient API
client = chromadb.PersistentClient(path=persist_dir)

# Reset collection if exists
if "test_collection" in [c.name for c in client.list_collections()]:
    client.delete_collection("test_collection")

col = client.create_collection("test_collection", metadata={"description": "test"})
print(f"[OK] Chroma started with persist dir: {persist_dir}")
