import zlib, copy, sqlite3
from viewer.gui.view_settings import DirtyReasons
from viewer.model.data_deserializers import ByteBuffer
from viewer.model.data_deserializers import SimpleDeserializer
from viewer.model.data_deserializers import StringDeserializer
from viewer.model.data_deserializers import EnumDeserializer
from viewer.model.data_deserializers import ContigContainerDeserializer
from viewer.model.data_deserializers import SparseContainerDeserializer
from viewer.model.data_deserializers import StructDeserializer
from viewer.model.collection_replayers import CollectionReplaySession

class DataRetriever:
    def __init__(self, frame, db_path, simhier, dtype_inspector):
        self.frame = frame
        self._db = sqlite3.connect(db_path)
        self._replay_session = CollectionReplaySession(db_path, dtype_inspector)
        self.simhier = simhier
        self.dtype_inspector = dtype_inspector
        self.cursor = self._db.cursor()
        cursor = self.cursor

        cursor.execute('SELECT Heartbeat FROM CollectionGlobals')
        self._heartbeat = cursor.fetchone()[0]

        cursor.execute('SELECT DISTINCT(Timestamp) FROM Timestamps ORDER BY Timestamp ASC')
        self._time_vals = [row[0] for row in cursor.fetchall()]

        for i,t in enumerate(self._time_vals):
            if isinstance(t, str):
                self._time_vals[i] = int(t)

        self._displayed_columns_by_elem_path = {}
        self._auto_colorize_column_by_elem_path = {}
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

        self.ResetToDefaultViewSettings(update_widgets=False)

    def IsDevDebug(self):
        return self.frame.dev_debug

    def GetCurrentViewSettings(self):
        settings = {}
        assert set(self._displayed_columns_by_elem_path.keys()) == set(self._auto_colorize_column_by_elem_path.keys())

        for elem_path, displayed_columns in self._displayed_columns_by_elem_path.items():
            assert len(displayed_columns) > 0
            settings[elem_path] = {'auto_colorize_column': None}
            settings[elem_path]['displayed_columns'] = copy.deepcopy(displayed_columns)

        for elem_path, auto_colorize_column in self._auto_colorize_column_by_elem_path.items():
            assert elem_path in settings
            settings[elem_path]['auto_colorize_column'] = None if not auto_colorize_column else auto_colorize_column

        return settings

    def ApplyViewSettings(self, settings):
        self._displayed_columns_by_elem_path = {}
        self._auto_colorize_column_by_elem_path = {}

        for elem_path, struct_settings in settings.items():
            displayed_columns = struct_settings['displayed_columns']
            auto_colorize_column = struct_settings['auto_colorize_column']
            self._displayed_columns_by_elem_path[elem_path] = copy.deepcopy(displayed_columns)
            self._auto_colorize_column_by_elem_path[elem_path] = auto_colorize_column

        for elem_path in self.simhier.GetContainerElemPaths():
            struct_name, struct_deserializer = self.__GetStructViewMeta(elem_path)
            if struct_name is None or struct_deserializer is None:
                continue
            visible = self._displayed_columns_by_elem_path.get(elem_path)
            if visible:
                struct_deserializer.SetVisibleFieldNames(visible)
            ac = self._auto_colorize_column_by_elem_path.get(elem_path)
            if ac not in struct_deserializer.GetVisibleFieldNames():
                self._auto_colorize_column_by_elem_path[elem_path] = None

        self.frame.inspector.RefreshWidgetsOnAllTabs()

    def GetCurrentUserSettings(self):
        # All our settings are in the user settings and do not affect the view file
        return {}

    def ApplyUserSettings(self, settings):
        # All our settings are in the user settings and do not affect the view file
        pass

    def ResetToDefaultViewSettings(self, update_widgets=True):
        self._auto_colorize_column_by_elem_path = {}
        self._displayed_columns_by_elem_path = {}
        for elem_path in self.simhier.GetContainerElemPaths():
            struct_name, struct_deserializer = self.__GetStructViewMeta(elem_path)
            if struct_name is None or struct_deserializer is None:
                continue

            all_columns = struct_deserializer.GetAllFieldNames()
            hidden_csv = self.simhier.GetMetaAtPath(elem_path, 'ArgosDefaultHiddenColumns') or ''
            hidden_set = {x.strip() for x in hidden_csv.split(',') if x.strip()}
            visible = [c for c in all_columns if c not in hidden_set]
            if not visible:
                visible = all_columns
            struct_deserializer.SetVisibleFieldNames(visible)
            self._displayed_columns_by_elem_path[elem_path] = list(visible)

            default_color_col = self.dtype_inspector.GetEffectiveColorKey(struct_name)
            if default_color_col not in visible:
                default_color_col = None
            self._auto_colorize_column_by_elem_path[elem_path] = default_color_col

        if update_widgets:
            self.frame.inspector.RefreshWidgetsOnAllTabs()

    def SetVisibleFieldNames(self, elem_path, field_names):
        struct_name, struct_deserializer = self.__GetStructViewMeta(elem_path)
        if struct_name is None or struct_deserializer is None:
            return
        assert elem_path in self._displayed_columns_by_elem_path

        if self._displayed_columns_by_elem_path[elem_path] == field_names:
            return

        auto_colorize_col = self.GetAutoColorizeColumn(elem_path)
        if auto_colorize_col not in field_names:
            self.SetAutoColorizeColumn(elem_path, None)

        struct_deserializer.SetVisibleFieldNames(field_names)
        self._displayed_columns_by_elem_path[elem_path] = struct_deserializer.GetVisibleFieldNames()
        self.frame.inspector.RefreshWidgetsOnAllTabs()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.QueueTableDispColsChanged)

    def SetAutoColorizeColumn(self, elem_path, field_name):
        struct_name, struct_deserializer = self.__GetStructViewMeta(elem_path)
        if struct_name is None or struct_deserializer is None:
            return
        assert elem_path in self._auto_colorize_column_by_elem_path

        if field_name is not None and field_name not in struct_deserializer.GetVisibleFieldNames():
            return

        if self._auto_colorize_column_by_elem_path[elem_path] == field_name:
            return

        self._auto_colorize_column_by_elem_path[elem_path] = field_name
        self.frame.inspector.RefreshWidgetsOnAllTabs()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.QueueTableAutoColorizeChanged)

    def GetAutoColorizeColumn(self, elem_path):
        struct_name, _ = self.__GetStructViewMeta(elem_path)
        if struct_name is None:
            return None
        return self._auto_colorize_column_by_elem_path.get(elem_path)

    def __GetStructViewMeta(self, elem_path):
        deserializer = self.GetDeserializer(elem_path)
        if isinstance(deserializer, (ContigContainerDeserializer, SparseContainerDeserializer)):
            deserializer = deserializer._bin_deserializer
        if isinstance(deserializer, StructDeserializer):
            return deserializer.struct_name, deserializer
        return None, None

    def GetDeserializer(self, elem_path):
        dtype = self._dtypes_by_elem_path[elem_path]
        return self.dtype_inspector.GetDeserializer(dtype)

    def GetIterableSizesByCollectionID(self, time_val):
        if self._cached_utiliz_time_val is not None and time_val == self._cached_utiliz_time_val:
            return self._cached_utiliz_sizes

        cids = self.simhier.GetContainerIDs()
        cids_data_dicts = self._replay_session.GetDataValueAtTime(cids, time_val)

        sizes_by_cid = {}
        for cid, elems in cids_data_dicts.items():
            elems = [e for e in elems if e is not None]
            sizes_by_cid[cid] = len(elems)

        self._cached_utiliz_time_val = time_val
        self._cached_utiliz_sizes = sizes_by_cid
        return sizes_by_cid

    def Unpack(self, elem_path, time_range):
        if not type(time_range) in (list, tuple):
            time_range = [time_range]

        time_range = list(time_range)
        for i,t in enumerate(time_range):
            if isinstance(t, str):
                t = int(t)
                time_range[i] = t

        if len(time_range) == 1:
            time_range = [time_range, time_range]

        cid = self.simhier.GetCollectionID(elem_path)
        unpacked = {
            'TimeVals': [],
            'DataVals': []
        }

        lo, hi = time_range[0], time_range[1]
        sample_times = {t for t in self._time_vals if lo <= t <= hi}
        sample_times.add(lo)
        if hi != lo:
            sample_times.add(hi)

        for time_point in sorted(sample_times):
            data_at_this_time = self._replay_session.GetDataValueAtTime(cid, time_point)
            unpacked['TimeVals'].append(time_point)
            unpacked['DataVals'].append(data_at_this_time)

        return unpacked

    def GetAllTimeVals(self):
        return copy.deepcopy(self._time_vals)
