import zlib, copy
from viewer.gui.view_settings import DirtyReasons
from viewer.model.data_deserializers import ByteBuffer
from viewer.model.data_deserializers import SimpleDeserializer
from viewer.model.data_deserializers import StringDeserializer
from viewer.model.data_deserializers import EnumDeserializer
from viewer.model.data_deserializers import ContigContainerDeserializer
from viewer.model.data_deserializers import SparseContainerDeserializer

class DataRetriever:
    def __init__(self, frame, db, simhier, dtype_inspector):
        self.frame = frame
        self._db = db
        self.simhier = simhier
        self.dtype_inspector = dtype_inspector
        self.cursor = db.cursor()
        cursor = self.cursor

        cursor.execute('SELECT Heartbeat FROM CollectionGlobals')
        self._heartbeat = cursor.fetchone()[0]

        cursor.execute('SELECT DISTINCT(Timestamp) FROM Timestamps ORDER BY Timestamp ASC')
        self._time_vals = [row[0] for row in cursor.fetchall()]

        # Handle uint64_t as string
        for i,t in enumerate(self._time_vals):
            if isinstance(t, str):
                self._time_vals[i] = int(t)

        self._displayed_columns_by_struct_name = {}
        self._auto_colorize_column_by_struct_name = {}
        self._cached_utiliz_sizes = {}
        self._cached_utiliz_time_val = None

        self._cids_by_elem_path = {}
        self._elem_paths_by_cid = {}
        def HandleLeaf(leaf):
            cid = leaf.GetMeta('CID')
            elem_path = leaf.GetPath()
            self._cids_by_elem_path[elem_path] = cid
            self._elem_paths_by_cid[cid] = elem_path

        simhier.GetTree().VisitLeaves(HandleLeaf)

        cursor.execute('SELECT FullPath,TypeName FROM CollectableTreeNodes')
        self._dtypes_by_elem_path = {}
        for elem_path, dtype in cursor.fetchall():
            self._dtypes_by_elem_path[elem_path] = dtype

    def IsDevDebug(self):
        return self.frame.dev_debug

    def GetCurrentViewSettings(self):
        settings = {}
        assert set(self._displayed_columns_by_struct_name.keys()) == set(self._auto_colorize_column_by_struct_name.keys())

        for struct_name,displayed_columns in self._displayed_columns_by_struct_name.items():
            assert len(displayed_columns) > 0
            settings[struct_name] = {'auto_colorize_column':None}
            settings[struct_name]['displayed_columns'] = copy.deepcopy(displayed_columns)
        
        for struct_name,auto_colorize_column in self._auto_colorize_column_by_struct_name.items():
            assert struct_name in settings
            settings[struct_name]['auto_colorize_column'] = None if not auto_colorize_column else auto_colorize_column

        return settings
    
    def ApplyViewSettings(self, settings):
        self._displayed_columns_by_struct_name = {}
        self._auto_colorize_column_by_struct_name = {}

        for struct_name,struct_settings in settings.items():
            displayed_columns = struct_settings['displayed_columns']
            auto_colorize_column = struct_settings['auto_colorize_column']

            self._displayed_columns_by_struct_name[struct_name] = copy.deepcopy(displayed_columns)
            self._auto_colorize_column_by_struct_name[struct_name] = auto_colorize_column

        self.frame.inspector.RefreshWidgetsOnAllTabs()

    def GetCurrentUserSettings(self):
        # All our settings are in the user settings and do not affect the view file
        return {}

    def ApplyUserSettings(self, settings):
        # All our settings are in the user settings and do not affect the view file
        pass

    def ResetToDefaultViewSettings(self, update_widgets=True):
        # TODO cnyce
        return

        self.cursor.execute('SELECT DISTINCT(StructName) FROM StructFields')
        struct_names = []
        for struct_name in self.cursor.fetchall():
            struct_names.append(struct_name[0])

        self._auto_colorize_column_by_struct_name = {}
        self._displayed_columns_by_struct_name = {}

        for struct_name in struct_names:
            cmd = 'SELECT FieldName FROM StructFields WHERE StructName="{}" AND IsAutoColorizeKey=1'.format(struct_name)
            self.cursor.execute(cmd)
            auto_colorize_column = [row[0] for row in self.cursor.fetchall()]
            assert len(auto_colorize_column) <= 1
            if len(auto_colorize_column) == 1:
                self._auto_colorize_column_by_struct_name[struct_name] = auto_colorize_column[0]
            else:
                self._auto_colorize_column_by_struct_name[struct_name] = None

            cmd = 'SELECT FieldName FROM StructFields WHERE StructName="{}" AND IsDisplayedByDefault=1'.format(struct_name)
            self.cursor.execute(cmd)
            displayed_columns = [row[0] for row in self.cursor.fetchall()]
            assert len(displayed_columns) > 0
            self._displayed_columns_by_struct_name[struct_name] = displayed_columns

        if update_widgets:
            self.frame.inspector.RefreshWidgetsOnAllTabs()

    def SetVisibleFieldNames(self, elem_path, field_names):
        # TODO cnyce
        return

        deserializer = self.GetDeserializer(elem_path)
        struct_name = deserializer.struct_name
        assert struct_name in self._displayed_columns_by_struct_name

        if self._displayed_columns_by_struct_name[struct_name] == field_names:
            return

        auto_colorize_col = self.GetAutoColorizeColumn(elem_path)
        if auto_colorize_col not in field_names:
            self.SetAutoColorizeColumn(elem_path, None)

        self._displayed_columns_by_struct_name[struct_name] = copy.deepcopy(field_names)
        self.frame.inspector.RefreshWidgetsOnAllTabs()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.QueueTableDispColsChanged)

    def SetAutoColorizeColumn(self, elem_path, field_name):
        # TODO cnyce
        return

        deserializer = self.GetDeserializer(elem_path)
        struct_name = deserializer.struct_name
        assert struct_name in self._auto_colorize_column_by_struct_name

        if self._auto_colorize_column_by_struct_name[struct_name] == field_name:
            return

        self._auto_colorize_column_by_struct_name[struct_name] = field_name
        self.frame.inspector.RefreshWidgetsOnAllTabs()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.QueueTableAutoColorizeChanged)

    def GetAutoColorizeColumn(self, elem_path):
        # TODO cnyce
        return None

        deserializer = self.GetDeserializer(elem_path)
        struct_name = deserializer.struct_name
        return self._auto_colorize_column_by_struct_name.get(struct_name)

    def GetDeserializer(self, elem_path):
        dtype = self._dtypes_by_elem_path[elem_path]
        return self.dtype_inspector.GetDeserializer(dtype)

    def GetIterableSizesByCollectionID(self, time_val):
        # TODO cnyce: what was this code even doing?
        if self._cached_utiliz_time_val is not None and time_val == self._cached_utiliz_time_val:
            return self._cached_utiliz_sizes

        return {id:0 for id in self.simhier.GetContainerIDs()}

    def Unpack(self, elem_path, time_range):
        

    def GetAllTimeVals(self):
        return copy.deepcopy(self._time_vals)
