# Argos collection-record blob decoding.
from collections import OrderedDict
import copy

# POD types whose ``CONVERTERS`` yield integers suitable for ``hex()`` display.
_INTEGRAL_DISPLAY_DTYPES = frozenset(
    {
        "signed char",
        "unsigned char",
        "short",
        "unsigned short",
        "int",
        "unsigned int",
        "long",
        "unsigned long",
        "long long",
        "unsigned long long",
    }
)


def _make_simple_output_formatter(dtype_name, special_formatter):
    # type: (str, str) -> object
    fmt = (special_formatter or "").strip().lower()
    if fmt not in ("hex", "oct"):
        return lambda x: x
    if dtype_name not in _INTEGRAL_DISPLAY_DTYPES:
        return lambda x: x

    if fmt == "hex":

        def _fmt_hex(val):
            if isinstance(val, int) and not isinstance(val, bool):
                return hex(val)
            return val

        return _fmt_hex

    def _fmt_oct(val):
        if isinstance(val, int) and not isinstance(val, bool):
            return oct(val)
        return val

    return _fmt_oct


UNPACK_FORMATS = {
    'char':           'b',
    'signed char':    'b',
    'unsigned char':  'B',
    'short':          'h',
    'unsigned short': 'H',
    'int':            'i',
    'unsigned int':   'I',
    'long':           'q',
    'unsigned long':  'Q',
    'double':         'd',
    'float':          'f',
    'bool':           'B'
}

# Helper class used to minimize the number of byte array copies
# we make during deserialization.
import struct
class ByteBuffer:
    def __init__(self, data_bytes):
        assert isinstance(data_bytes, bytes)
        self._read_idx = 0
        self._end_idx = len(data_bytes)
        self._data_bytes = data_bytes

    def Read(self, fmt):
        if fmt in UNPACK_FORMATS:
            fmt = UNPACK_FORMATS[fmt]
        nbytes = struct.calcsize(fmt)
        assert self._read_idx + nbytes <= self._end_idx

        val_bytes = self._data_bytes[self._read_idx : self._read_idx + nbytes]
        val = struct.unpack(fmt, val_bytes)
        if len(fmt) == 1:
            val = val[0]

        self._read_idx += nbytes
        return val

    def Extract(self, num_bytes):
        assert self._read_idx + num_bytes <= self._end_idx
        extracted_bytes = self._data_bytes[self._read_idx : self._read_idx + num_bytes]
        self._read_idx += num_bytes
        return extracted_bytes

    def Jump(self, num_bytes):
        assert self._read_idx + num_bytes <= self._end_idx
        self._read_idx += num_bytes

    def Done(self):
        assert self._read_idx <= self._end_idx
        return self._read_idx == self._end_idx

    @classmethod
    def CreateFrom(cls, obj):
        if isinstance(obj, cls):
            return obj
        else:
            return cls(obj)

def CreateDeserializer(inspector, dtype_name, tiny_strings=None):
    # Extract sparse/contig and capacity from a data type name
    def GetContainerMeta(dtype_name):
        def GetMeta(which):
            key = f'_{which}_capacity'
            idx = dtype_name.find(key)
            if idx != -1:
                base_dtype_name = dtype_name[:idx]
                capacity = dtype_name.replace(base_dtype_name + key, '')
                capacity = int(capacity)
                return (base_dtype_name, capacity)

            return None

        meta = GetMeta('sparse')
        if meta:
            return (meta[0], meta[1], 'sparse')
        if not meta:
            meta = GetMeta('contig')
            if meta:
                return (meta[0], meta[1], 'contig')

        return None

    # Simple types (non-enum)
    if dtype_name in UNPACK_FORMATS:
        special = (
            inspector.GetSimpleTypeSpecialFormatter(dtype_name) if inspector is not None else ""
        )
        return SimpleDeserializer(dtype_name, special)

    # String types (collected as uint32_t)
    if dtype_name == 'string':
        return StringDeserializer(tiny_strings)

    # Container types
    container_meta = GetContainerMeta(dtype_name)
    if container_meta:
        dtype_name, capacity, sparse_mode = container_meta
        bin_deserializer = inspector.GetDeserializer(dtype_name)
        if sparse_mode == 'sparse':
            return SparseContainerDeserializer(bin_deserializer, capacity)
        else:
            return ContigContainerDeserializer(bin_deserializer, capacity)

    # Struct types
    struct_defn = inspector.GetStructDefn(dtype_name)
    if struct_defn is not None:
        return StructDeserializer(dtype_name, struct_defn, inspector, tiny_strings)

    # Enum types
    enum_map = inspector.GetEnumMap(dtype_name)
    if enum_map:
        backing_kind = inspector.GetEnumBackingKind(dtype_name)
        if backing_kind in SimpleDeserializer.CONVERTERS:
            val_deserializer = SimpleDeserializer(backing_kind, "")
            return EnumDeserializer(val_deserializer, enum_map)

    return None

# This class deserializes non-enum POD types.
class SimpleDeserializer:
    CONVERTERS = {
        'char':           lambda x: str(x),
        'signed char':    lambda x: int(x),
        'unsigned char':  lambda x: int(x),
        'short':          lambda x: int(x),
        'unsigned short': lambda x: int(x),
        'int':            lambda x: int(x),
        'unsigned int':   lambda x: int(x),
        'long':           lambda x: int(x),
        'unsigned long':  lambda x: int(x),
        'double':         lambda x: float(x),
        'float':          lambda x: float(x),
        'bool':           lambda x: bool(x)
    }

    NUM_BYTES = {
        'char':           1,
        'signed char':    1,
        'unsigned char':  1,
        'short':          2,
        'unsigned short': 2,
        'int':            4,
        'unsigned int':   4,
        'long':           8,
        'unsigned long':  8,
        'double':         8,
        'float':          4,
        'bool':           1
    }

    def __init__(self, dtype_name, special_formatter=""):
        # type: (str, str) -> None
        self._fmt = UNPACK_FORMATS[dtype_name]
        self._converter = self.CONVERTERS[dtype_name]
        self._num_bytes = self.NUM_BYTES[dtype_name]
        self._formatter = _make_simple_output_formatter(dtype_name, special_formatter or "")

    def GetNumBytes(self):
        return self._num_bytes

    def Deserialize(self, data_bytes):
        buf = ByteBuffer.CreateFrom(data_bytes)
        val = buf.Read(self._fmt)
        val = self._converter(val)
        return self._formatter(val)

# This class deserializes string types.
class StringDeserializer:
    def __init__(self, tiny_strings):
        assert tiny_strings is not None
        self._tiny_strings = tiny_strings

    def GetNumBytes(self):
        # Always stored as uint32_t
        return 4

    def Deserialize(self, data_bytes):
        buf = ByteBuffer.CreateFrom(data_bytes)
        string_id = buf.Read('I')
        return self._tiny_strings.GetString(string_id, must_exist=True)

# This class deserializes enum types.
class EnumDeserializer:
    def __init__(self, val_deserializer, enum_map):
        self._val_deserializer = val_deserializer

        self._enum_map = {}
        for e_name, e_int in enum_map.items():
            assert isinstance(e_name, str)
            assert isinstance(e_int, int)
            self._enum_map[e_int] = e_name

        self._enum_map = {k:v for v,k in enum_map.items()}

    def GetNumBytes(self):
        # Defer to the int deserializer
        return self._val_deserializer.GetNumBytes()

    def Deserialize(self, data_bytes):
        enum_val = self._val_deserializer.Deserialize(data_bytes)
        enum_val = int(enum_val)
        return self._enum_map[enum_val]

# This class deserializes contiguous container types.
class ContigContainerDeserializer:
    def __init__(self, bin_deserializer, capacity):
        self._bin_deserializer = bin_deserializer
        self._capacity = capacity

    def GetCapacity(self):
        return self._capacity

    def GetAllFieldNames(self):
        return self._bin_deserializer.GetAllFieldNames()

    def GetVisibleFieldNames(self):
        # TODO cnyce
        return self.GetAllFieldNames()

    def GetBinNumBytes(self):
        return self._bin_deserializer.GetNumBytes()

    def Deserialize(self, data_bytes):
        buf = ByteBuffer.CreateFrom(data_bytes)

        # First 2 bytes always give the container size
        size = buf.Read('H')
        assert size <= self._capacity

        # Create the container
        container = []
        while len(container) < size:
            bin_val = self._bin_deserializer.Deserialize(buf)
            container.append(bin_val)

        return container

# This class deserializes sparse container types.
class SparseContainerDeserializer:
    def __init__(self, bin_deserializer, capacity):
        self._bin_deserializer = bin_deserializer
        self._capacity = capacity

    def GetCapacity(self):
        return self._capacity

    def GetAllFieldNames(self):
        return self._bin_deserializer.GetAllFieldNames()

    def GetVisibleFieldNames(self):
        # TODO cnyce
        return self.GetAllFieldNames()

    def GetBinNumBytes(self):
        # Account for uint16_t bin index for each element
        return self._bin_deserializer.GetNumBytes() + 2

    def Deserialize(self, data_bytes):
        buf = ByteBuffer.CreateFrom(data_bytes)

        # First 2 bytes always give the container size
        size = buf.Read('H')
        assert size <= self._capacity

        # Create the container; recall that sparse containers
        # store None wherever there is no data
        container = [None] * self._capacity

        # Read each element (bin), noting that each one is
        # preceeded by a uint16_t which gives the bin idx.
        while size > 0:
            bin_idx = buf.Read('H')
            bin_val = self._bin_deserializer.Deserialize(buf)
            container[bin_idx] = bin_val
            size -= 1

        return container

# This class deserializes struct types.
class StructDeserializer:
    def __init__(self, struct_name, struct_defn, inspector, tiny_strings):
        self.struct_name = struct_name
        self._field_deserializers = []
        self._flattened_field_names = []
        StructDeserializer.__RecurseFindCollectableFields(
            struct_defn, inspector, tiny_strings, self._field_deserializers, self._flattened_field_names
        )
        assert self._field_deserializers
        assert self._flattened_field_names
        self._visible_field_names = self._flattened_field_names

    def GetAllFieldNames(self):
        return copy.deepcopy(self._flattened_field_names)

    def GetVisibleFieldNames(self):
        return copy.deepcopy(self._visible_field_names)

    def SetVisibleFieldNames(self, field_names):
        # type: (list) -> None
        if not field_names:
            return
        allowed = set(self._flattened_field_names)
        visible = []
        for field_name in field_names:
            if field_name in allowed and field_name not in visible:
                visible.append(field_name)
        if visible:
            self._visible_field_names = visible

    def GetNumBytes(self):
        num_bytes = 0
        for field_name, deserializer in self._field_deserializers:
            num_bytes += deserializer.GetNumBytes()
        return num_bytes

    def Deserialize(self, data_bytes):
        buf = ByteBuffer.CreateFrom(data_bytes)
        deserialized = []

        for field_name, deserializer in self._field_deserializers:
            deserialized.append((field_name, deserializer.Deserialize(buf)))

        return deserialized

    @staticmethod
    def __RecurseFindCollectableFields(
        struct_defn, inspector, tiny_strings, field_deserializers, flattened_field_names
    ):
        for field in struct_defn.children:
            if field.name and field.kind != 'struct':
                flattened_field_names.append(field.name)
            if field.kind == 'pod' and field.type_name != 'string':
                deserializer = SimpleDeserializer(
                    field.type_name, field.special_formatter or ""
                )
                field_deserializers.append((field.name, deserializer))
            elif field.type_name == 'string':
                deserializer = StringDeserializer(tiny_strings)
                field_deserializers.append((field.name, deserializer))
            elif field.kind == 'enum':
                deserializer = CreateDeserializer(inspector, field.type_name)
                field_deserializers.append((field.name, deserializer))
            elif field.kind == 'struct':
                StructDeserializer.__RecurseFindCollectableFields(
                    field, inspector, tiny_strings, field_deserializers, flattened_field_names
                )
            else:
                raise ValueError(f'Unknown field data type: {field.kind}')
