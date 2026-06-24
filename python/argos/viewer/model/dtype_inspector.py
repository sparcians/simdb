import sqlite3
from typing import Dict, Iterator, List, Optional
from viewer.model.tiny_strings import TinyStrings
from viewer.model.data_deserializers import SimpleDeserializer

class DataTypeInspector:
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
            "effective_color_key",
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
            effective_color_key="",
        ):
            # type: (Optional[int], int, Optional[int], str, str, str, str, str, str, str) -> None
            self.node_id = node_id
            self.schema_id = schema_id
            self.parent_id = parent_id
            self.kind = kind
            self.name = name
            self.description = description
            self.type_name = type_name
            self.enum_backing = enum_backing
            self.special_formatter = special_formatter
            self.effective_color_key = effective_color_key
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
        self._effective_color_keys_by_schema = {}
        self._effective_color_keys_by_root_type = {}
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
        cur.execute("SELECT CID,TypeName FROM CollectableTreeNodes")
        self._dtypes_by_cid = {cid:type_name for cid,type_name in cur.fetchall()}

        cur.execute("SELECT Id, RootTypeName FROM DataTypeSchemas ORDER BY Id")
        for sid, root_name in cur.fetchall():
            sid = int(sid)
            root_name = str(root_name)
            self._schemas[sid] = root_name
            self._effective_color_keys_by_schema[sid] = ""
            self._effective_color_keys_by_root_type[root_name] = ""

        cur.execute("SELECT DISTINCT(SchemaId) FROM DataTypeNodes WHERE Name='DID'")
        for sid in cur.fetchall():
            sid = sid[0]
            root_name = self._schemas[sid]
            self._effective_color_keys_by_schema[sid] = "DID"
            self._effective_color_keys_by_root_type[root_name] = "DID"

        cur.execute(
            """
            SELECT Id, SchemaId, Name, TypeName, FormatStr
            FROM DataTypeNodes
            ORDER BY Id
            """
        )
        raw_rows = cur.fetchall()

        enum_defns = {}
        enum_backings = {}

        cur.execute(
            """
            SELECT ce.EnumName, ce.EnumIntTypeName, em.MemberName, em.MemberValueStr
            FROM CollectedEnums ce
            JOIN EnumMembers em ON em.EnumID = ce.Id
            """
        )
        for enum_name, int_type_name, member_name, member_value_str in cur.fetchall():
            if enum_name not in enum_defns:
                enum_defns[enum_name] = {}
            enum_defns[enum_name][member_name] = int(member_value_str)
            enum_backings[enum_name] = int_type_name

        self._nodes_by_id.clear()
        self._top_by_schema = {sid: [] for sid in self._schemas}

        for row in raw_rows:
            nid, sid, name, type_name, special_formatter = row
            enum_back = None
            if type_name in SimpleDeserializer.CONVERTERS:
                kind = "pod"
            elif type_name == "string":
                kind = "pod"
            elif type_name != "dynamic":
                kind = "enum"
                if type_name not in enum_defns:
                    # All this means is that we never ended up collecting
                    # anything that uses this enum. All of the enum int:str
                    # mappings are figured out only when first seen during
                    # collection.
                    enum_defns[type_name] = {}
                else:
                    enum_back = enum_backings.get(type_name)

            node = DataTypeInspector.DataTypeNode(
                nid,
                sid,
                None,
                kind,
                name,
                None,
                type_name,
                enum_back,
                special_formatter,
                "")

            self._nodes_by_id[node.node_id] = node
            if kind == "enum":
                node.enum_members = enum_defns[type_name]

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
                self._effective_color_keys_by_schema.get(sid, ""),
            )
            view.children = list(self._top_by_schema.get(sid, []))
            self._root_views.append(view)

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

        return self.GetRootDefn(struct_name)

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
        """First ``Kind == 'pod'`` node whose ``TypeName`` matches *type_name* with non-empty ``FormatStr``."""
        for node in self._iter_nodes():
            if node.kind != "pod":
                continue
            if node.type_name != type_name:
                continue
            if node.special_formatter:
                return node.special_formatter
        return ""

    def GetDataTypeForCollectionID(self, cid):
        return self._dtypes_by_cid.get(cid)

    def GetEffectiveColorKey(self, dtype_name):
        # type: (str) -> str
        """Return DataTypeSchemas.EffectiveColorKey for a root type, or ""."""
        return self._effective_color_keys_by_root_type.get(dtype_name, "")

    def GetDeserializer(self, dtype_name, expect_exists=True):
        if dtype_name in self._deserializers_by_typename:
            return self._deserializers_by_typename[dtype_name]

        from viewer.model.data_deserializers import CreateDeserializer
        deserializer = CreateDeserializer(self, dtype_name, self._tiny_strings)

        if deserializer:
            self._deserializers_by_typename[dtype_name] = deserializer
            return deserializer

        return None
