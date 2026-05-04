import sqlite3
from typing import Dict, Iterator, List, Optional
from viewer.model.tiny_strings import TinyStrings

class DataTypeInspector:
    """Loads Argos data-type metadata written by CollectionPipeline / DataTypeSerializer."""

    class DataTypeNode:
        __slots__ = (
            "node_id",
            "schema_id",
            "parent_id",
            "kind",
            "name",
            "description",
            "type_name",
            "enum_backing",
            "special_formatter",
            "children",
            "enum_members",
        )

        def __init__(
            self,
            node_id,
            schema_id,
            parent_id,
            kind,
            name,
            description,
            type_name,
            enum_backing,
            special_formatter,
        ):
            # type: (Optional[int], int, Optional[int], str, str, str, str, str, str) -> None
            self.node_id = node_id
            self.schema_id = schema_id
            self.parent_id = parent_id
            self.kind = kind
            self.name = name
            self.description = description
            self.type_name = type_name
            self.enum_backing = enum_backing
            self.special_formatter = special_formatter
            self.children = []  # type: List[DataTypeInspector.DataTypeNode]
            # Populated for Kind == "enum": member name -> numeric value (parsed from DB string)
            self.enum_members = {}  # type: Dict[str, int]

        def __repr__(self):
            return (
                f"DataTypeNode(id={self.node_id!r}, kind={self.kind!r}, "
                f"name={self.name!r}, type_name={self.type_name!r}, n_children={len(self.children)})"
            )

    def __init__(self, db_file):
        self._conn = sqlite3.connect(db_file)
        self._schemas = {}
        self._nodes_by_id = {}
        self._top_by_schema = {}
        self._root_views = []
        self._deserializers_by_typename = {}
        self._tiny_strings = TinyStrings(db_file)
        self._load()

    @property
    def connection(self):
        # type: () -> sqlite3.Connection
        return self._conn

    def close(self):
        # type: () -> None
        self._conn.close()

    def _load(self):
        # type: () -> None
        cur = self._conn.cursor()
        cur.execute("SELECT Id, RootTypeName FROM DataTypeSchemas ORDER BY Id")
        for sid, root_name in cur.fetchall():
            self._schemas[int(sid)] = str(root_name)

        cur.execute(
            """
            SELECT Id, SchemaId, ParentId, Kind, Name, Description, TypeName, EnumBacking, SpecialFormatters
            FROM DataTypeNodes
            ORDER BY Id
            """
        )
        raw_rows = cur.fetchall()

        enum_rows = {}  # type: Dict[int, List[tuple]]
        cur.execute(
            "SELECT EnumNodeId, MemberName, MemberValue FROM DataTypeEnumMembers ORDER BY Id"
        )
        for enum_node_id, member_name, member_value in cur.fetchall():
            eid = int(enum_node_id)
            enum_rows.setdefault(eid, []).append((str(member_name), str(member_value)))

        self._nodes_by_id.clear()
        self._top_by_schema = {sid: [] for sid in self._schemas}

        for row in raw_rows:
            nid, sid, parent_id, kind, name, desc, type_name, enum_back, special_formatter = row
            node = DataTypeInspector.DataTypeNode(
                int(nid),
                int(sid),
                int(parent_id) if parent_id is not None else None,
                str(kind),
                str(name) if name is not None else "",
                str(desc) if desc is not None else "",
                str(type_name) if type_name is not None else "",
                str(enum_back) if enum_back is not None else "",
                str(special_formatter) if special_formatter is not None else "",
            )
            self._nodes_by_id[node.node_id] = node
            if kind == "enum" and node.node_id in enum_rows:
                for mname, mval in enum_rows[node.node_id]:
                    node.enum_members[mname] = self._parse_enum_value(mval)

        for node in self._nodes_by_id.values():
            pid = node.parent_id
            if pid == 0 or pid is None:
                self._top_by_schema.setdefault(node.schema_id, []).append(node)
            else:
                parent = self._nodes_by_id.get(int(pid))
                if parent is not None:
                    parent.children.append(node)
                else:
                    raise ValueError(
                        f"DataTypeNodes.Id={node.node_id} references missing ParentId={pid}"
                    )

        for node in self._nodes_by_id.values():
            node.children.sort(key=lambda n: n.node_id or 0)

        for tops in self._top_by_schema.values():
            tops.sort(key=lambda n: n.node_id or 0)

        self._root_views = []
        for sid in sorted(self._schemas.keys()):
            root_name = self._schemas[sid]
            view = DataTypeInspector.DataTypeNode(
                None,
                sid,
                None,
                "schema",
                "",
                "",
                root_name,
                "",
                "",
            )
            view.children = list(self._top_by_schema.get(sid, []))
            self._root_views.append(view)

    @staticmethod
    def _parse_enum_value(raw):
        # type: (str) -> int
        raw = raw.strip()
        if raw.startswith("0x") or raw.startswith("0X"):
            return int(raw, 16)
        return int(raw, 0)

    def _iter_nodes(self):
        # type: () -> Iterator[DataTypeInspector.DataTypeNode]
        yield from self._nodes_by_id.values()

    def GetRoots(self):
        # type: () -> List[DataTypeInspector.DataTypeNode]
        """One synthetic node per schema row (``DataTypeSchemas``), with top-level fields as ``children``."""
        return list(self._root_views)

    def GetRootDefn(self, dtype_name):
        # type: (str) -> Optional[DataTypeInspector.DataTypeNode]
        """Return the synthetic schema root whose ``type_name`` matches ``DataTypeSchemas.RootTypeName``."""
        for view in self._root_views:
            if view.type_name == dtype_name:
                return view
        return None

    def GetStructDefn(self, struct_name):
        # type: (str) -> Optional[DataTypeInspector.DataTypeNode]
        """First ``Kind == 'struct'`` node whose ``TypeName`` matches *struct_name*."""
        for node in self._iter_nodes():
            if node.kind == "struct" and node.type_name == struct_name:
                return node
        return None

    def GetEnumMap(self, enum_name):
        # type: (str) -> Optional[Dict[str, int]]
        """Member map for ``Kind == 'enum'`` where ``TypeName`` or field ``Name`` matches *enum_name*."""
        for node in self._iter_nodes():
            if node.kind != "enum":
                continue
            if node.type_name == enum_name or node.name == enum_name:
                return dict(node.enum_members)
        return None

    def GetEnumBackingKind(self, enum_name):
        # type: (str) -> Optional[Dict[str, int]]
        """Member map for ``Kind == 'enum'`` where ``TypeName`` or field ``Name`` matches *enum_name*."""
        for node in self._iter_nodes():
            if node.kind != "enum":
                continue
            if node.type_name == enum_name or node.name == enum_name:
                return node.enum_backing
        return None

    def GetSimpleTypeSpecialFormatter(self, type_name):
        # type: (str) -> str
        """First ``Kind == 'pod'`` node whose ``TypeName`` matches *type_name* with non-empty ``SpecialFormatters``."""
        for node in self._iter_nodes():
            if node.kind != "pod":
                continue
            if node.type_name != type_name:
                continue
            if node.special_formatter:
                return node.special_formatter
        return ""

    def GetDeserializer(self, dtype_name, expect_exists=True):
        if dtype_name in self._deserializers_by_typename:
            return self._deserializers_by_typename[dtype_name]

        from viewer.model.data_deserializers import CreateDeserializer
        deserializer = CreateDeserializer(self, dtype_name, self._tiny_strings)

        if deserializer:
            self._deserializers_by_typename[dtype_name] = deserializer
            return deserializer

        if expect_exists:
            raise Exception(f'Unable to create deserializer for data type: {dtype_name}')

        return None
