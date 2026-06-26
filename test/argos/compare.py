import os, sys, subprocess, sqlite3
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
_ARGOS_ROOT = _REPO_ROOT / "python" / "argos"
if str(_ARGOS_ROOT) not in sys.path:
    sys.path.insert(0, str(_ARGOS_ROOT))

# Compare two databases by deserializing collection data at every time point.
baseline_db = sys.argv[1]
test_db = sys.argv[2]


class _StubInspector:
    def RefreshWidgetsOnAllTabs(self):
        pass


class _StubFrame:
    inspector = _StubInspector()


def _QueryRows(db_path, sql):
    conn = sqlite3.connect(db_path)
    try:
        cur = conn.cursor()
        cur.execute(sql)
        return cur.fetchall()
    finally:
        conn.close()


def ValidateComparableSchemas(db1_path, db2_path):
    db1_path = os.path.abspath(db1_path)
    db2_path = os.path.abspath(db2_path)

    ctn_sql = (
        "SELECT CID, FullPath, ClockID, TypeName "
        "FROM CollectableTreeNodes ORDER BY CID"
    )
    rows1 = _QueryRows(db1_path, ctn_sql)
    rows2 = _QueryRows(db2_path, ctn_sql)
    if rows1 != rows2:
        print("Error: CollectableTreeNodes differ (ignoring ShowInUI)")
        print(f"  baseline rows: {len(rows1)}, test rows: {len(rows2)}")
        for i, (r1, r2) in enumerate(zip(rows1, rows2)):
            if r1 != r2:
                print(f"  first differing row at index {i}: baseline={r1!r}, test={r2!r}")
                break
        sys.exit(1)

    schema_sql = "SELECT Id, RootTypeName FROM DataTypeSchemas ORDER BY Id"
    rows1 = _QueryRows(db1_path, schema_sql)
    rows2 = _QueryRows(db2_path, schema_sql)
    if rows1 != rows2:
        print("Error: DataTypeSchemas differ")
        print(f"  baseline rows: {len(rows1)}, test rows: {len(rows2)}")
        for i, (r1, r2) in enumerate(zip(rows1, rows2)):
            if r1 != r2:
                print(f"  first differing row at index {i}: baseline={r1!r}, test={r2!r}")
                break
        sys.exit(1)

    nodes_sql = (
        "SELECT Id, SchemaId, Name, TypeName, FormatStr "
        "FROM DataTypeNodes ORDER BY Id"
    )
    rows1 = _QueryRows(db1_path, nodes_sql)
    rows2 = _QueryRows(db2_path, nodes_sql)
    if rows1 != rows2:
        print("Error: DataTypeNodes differ")
        print(f"  baseline rows: {len(rows1)}, test rows: {len(rows2)}")
        for i, (r1, r2) in enumerate(zip(rows1, rows2)):
            if r1 != r2:
                print(f"  first differing row at index {i}: baseline={r1!r}, test={r2!r}")
                break
        sys.exit(1)


def GetShowInUIPaths(db_path):
    sql = "SELECT FullPath FROM CollectableTreeNodes WHERE ShowInUI=1"
    return {row[0] for row in _QueryRows(db_path, sql)}


def GetComparisonTickRange(db_path):
    conn = sqlite3.connect(os.path.abspath(db_path))
    try:
        cur = conn.cursor()
        cur.execute(
            "SELECT MIN(CAST(Timestamp AS INTEGER)), MAX(CAST(Timestamp AS INTEGER)) "
            "FROM Timestamps"
        )
        min_tick, max_tick = cur.fetchone()
        return int(min_tick), int(max_tick)
    finally:
        conn.close()


def MakeDataRetriever(db_path):
    from viewer.model.dtype_inspector import DataTypeInspector
    from viewer.model.simhier import SimHierarchy
    from viewer.model.data_retriever import DataRetriever

    db_path = os.path.abspath(db_path)
    dtype_inspector = DataTypeInspector(db_path)
    simhier = SimHierarchy(dtype_inspector.connection, dtype_inspector)
    retriever = DataRetriever(_StubFrame(), db_path, simhier, dtype_inspector)
    return retriever


def SmokeTest(db_file):
    return True


def CompareTickByTick():
    print(f'Comparing {baseline_db} and {test_db}')
    ValidateComparableSchemas(baseline_db, test_db)

    min_tick1, max_tick1 = GetComparisonTickRange(baseline_db)
    min_tick2, max_tick2 = GetComparisonTickRange(test_db)
    start_tick = max(min_tick1, min_tick2)
    end_tick = min(max_tick1, max_tick2)

    if start_tick > end_tick:
        print(
            f'Error: no overlapping time window '
            f'(baseline [{min_tick1}, {max_tick1}], test [{min_tick2}, {max_tick2}])'
        )
        sys.exit(1)

    tick_count = end_tick - start_tick + 1
    print(f'Comparison tick range: [{start_tick}, {end_tick}] ({tick_count} ticks)')

    retriever1 = MakeDataRetriever(baseline_db)
    retriever2 = MakeDataRetriever(test_db)

    elem_paths = sorted(GetShowInUIPaths(baseline_db) & GetShowInUIPaths(test_db))
    valid = set(retriever1.simhier.GetItemElemPaths()) & set(retriever2.simhier.GetItemElemPaths())
    elem_paths = [p for p in elem_paths if p in valid]

    if not elem_paths:
        print('Error: no elem paths with ShowInUI=1 in both databases')
        sys.exit(1)

    print(f'Comparing {len(elem_paths)} elem paths')

    tick = start_tick
    while True:
        all_values1 = retriever1.UnpackRange(tick, tick, elem_paths)
        all_values2 = retriever2.UnpackRange(tick, tick, elem_paths)

        for elem_path in elem_paths:
            values1 = all_values1[elem_path]
            values2 = all_values2[elem_path]
            if values1 != values2:
                print(f'Error at tick {tick}, path {elem_path!r}')
                print(f'  baseline: {values1}')
                print(f'  test:     {values2}')
                sys.exit(1)

        if tick == end_tick:
            break
        tick += 1

    print('Comparison passed.')


success = SmokeTest(baseline_db) and SmokeTest(test_db)
if not success:
    sys.exit(1)
else:
    CompareTickByTick()
