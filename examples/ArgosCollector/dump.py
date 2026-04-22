import os
import sys
import zlib
from pathlib import Path

# Repo root is two levels above this directory.
_REPO_ROOT = Path(__file__).resolve().parents[2]

# Update the path if needed.
_ARGOS_PKG = _REPO_ROOT / "python" / "argos"
if str(_ARGOS_PKG) not in sys.path:
    sys.path.insert(0, str(_ARGOS_PKG))

from viewer.model.data_deserializers import ByteBuffer

# Arguments
import argparse

parser = argparse.ArgumentParser("Collection dumper")
parser.add_argument("--db-file", help="Full/relative path to the database file")
parser.add_argument("--dump-file", help="Output file (prints to stdout if not provided)")
parser.add_argument("--append-dump-file", help="Open dump file in append mode", action="store_true")
parser.add_argument("--quiet", help="Do not print deserialized data to stdout", action="store_true")
args = parser.parse_args()

db_file = args.db_file
assert os.path.exists(db_file)


class FilePrinter:
    def __init__(self, fname, append):
        mode = "a" if append else "w"
        self._out = open(fname, mode)

    def print(self, line):
        self._out.write(line.rstrip("\n") + "\n")


class StdoutPrinter:
    def print(self, line):
        print(line)


printer = FilePrinter(args.dump_file, args.append_dump_file) if args.dump_file else StdoutPrinter()

# Access everything about collected data types
from viewer.model.dtype_inspector import DataTypeInspector

dtype_inspector = DataTypeInspector(db_file)

# CID -> replayer (stateful across time points for minified collectables)
import sqlite3

from collection_replayers import CreateCollectableReplayer

conn = sqlite3.connect(db_file)
cursor = conn.cursor()
cursor.execute("SELECT TypeName,SerializationCID FROM CollectableTreeNodes")

replayers_by_cid = {}
for type_name, cid in cursor.fetchall():
    replayers_by_cid[cid] = CreateCollectableReplayer(cid, type_name, dtype_inspector)


def DumpCollectionAtTime(timestamp_id, time_point):
    cursor.execute(f"SELECT Records FROM CollectionRecords WHERE TimestampID={timestamp_id}")
    rows = cursor.fetchall()[0]
    assert len(rows) == 1

    buf = ByteBuffer(zlib.decompress(rows[0]))

    printer.print(f"At time point {time_point} we have the following values (collected now or carried over unchanged):")

    while not buf.Done():
        cid = int(buf.Read("H"))
        replayer = replayers_by_cid[cid]
        val = replayer.replay_next(buf)
        printer.print(f"CID {cid}: {val}")


# Dump collection at every time point
printer.print("All collectables found in database:\n")

cursor.execute("SELECT Id,Timestamp FROM Timestamps")
for timestamp_id, time_point in cursor.fetchall():
    # Handle uint64_t (stored as strings)
    if isinstance(time_point, str):
        time_point = int(time_point)

    DumpCollectionAtTime(timestamp_id, time_point)
