import zlib, copy, sqlite3
from viewer.model.dirty_reasons import DirtyReasons
from viewer.model.data_deserializers import ContigContainerDeserializer
from viewer.model.data_deserializers import SparseContainerDeserializer
from viewer.model.data_deserializers import StructDeserializer
from viewer.model.blob_iterator import BlobIterator
from viewer.model.blob_handlers import DataExtractionHandler

class DataRetriever:
    def __init__(self, frame, db_path, simhier, dtype_inspector):
        self.frame = frame
        self._db = sqlite3.connect(db_path)
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
        struct_name, deserializer = self.__GetStructViewMeta(elem_path)
        if struct_name is None:
            return None

        col = self._auto_colorize_column_by_elem_path.get(elem_path)
        if col is None:
            col = deserializer.GetVisibleFieldNames()[0]

        return col

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

    def _has_timestamp_clocks(self):
        self.cursor.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='TimestampClocks'"
        )
        return self.cursor.fetchone() is not None

    def _clock_collected_at_tick(self, clock_id, tick):
        if clock_id is None or not self._has_timestamp_clocks():
            return True

        self.cursor.execute(
            'SELECT 1 FROM Timestamps t '
            'INNER JOIN TimestampClocks tc ON tc.TimestampID = t.Id '
            'WHERE tc.ClockID = ? AND CAST(t.Timestamp AS INTEGER) = ? '
            'LIMIT 1',
            (clock_id, int(tick)),
        )
        return self.cursor.fetchone() is not None

    def _database_has_multiple_clocks(self):
        self.cursor.execute('SELECT COUNT(*) FROM Clocks')
        return int(self.cursor.fetchone()[0]) > 1

    def _clock_ids_for_elem_paths(self, elem_paths):
        if not self._database_has_multiple_clocks():
            return None

        clock_ids = set()
        for elem_path in elem_paths:
            clk_id = self.simhier.GetMetaAtPath(elem_path, 'ClkID')
            if clk_id is not None:
                clock_ids.add(int(clk_id))
        return sorted(clock_ids) if clock_ids else None

    def _clock_id_for_elem_path(self, elem_path):
        if not self._database_has_multiple_clocks():
            return None

        clk_id = self.simhier.GetMetaAtPath(elem_path, 'ClkID')
        return int(clk_id) if clk_id is not None else None

    def GetIterableSizesByCollectionID(self, time_val):
        if self._cached_utiliz_time_val is not None and time_val == self._cached_utiliz_time_val:
            return self._cached_utiliz_sizes

        tick = int(time_val)
        iterator = BlobIterator(self.dtype_inspector, self.simhier)
        handler = DataExtractionHandler(self.simhier)
        clock_ids = self._clock_ids_for_elem_paths(self.simhier.GetContainerElemPaths())
        iterator.Iterate(handler, tick, lookback=True, clock_ids=clock_ids)

        sizes_by_cid = {}
        for cid in self.simhier.GetContainerIDs():
            elems = handler.GetFinalValue(cid)
            sizes_by_cid[cid] = len([e for e in elems if e is not None]) if elems else 0

        self._cached_utiliz_time_val = time_val
        self._cached_utiliz_sizes = sizes_by_cid
        return sizes_by_cid

    def Unpack(self, elem_path, tick):
        iterator = BlobIterator(self.dtype_inspector, self.simhier)
        handler = DataExtractionHandler(self.simhier)
        clock_id = self._clock_id_for_elem_path(elem_path)
        iterator.Iterate(handler, tick, lookback=True, clock_ids=clock_id)

        if handler.HasDataFor(elem_path) and self._clock_collected_at_tick(clock_id, tick):
            unpacked = {
                'TimeVals': [iterator.GetFinalTick()],
                'DataVals': [handler.GetFinalValue(elem_path)]
            }
        else:
            unpacked = {
                'TimeVals': [],
                'DataVals': []
            }

        return unpacked

    def UnpackRange(self, start_tick, end_tick, elem_paths=None):
        if elem_paths is None:
            elem_paths = self.simhier.GetItemElemPaths()
        cids = {self.simhier.GetCollectionID(p) for p in elem_paths}

        # Warm up one heartbeat before the requested start so the value at
        # start_tick is fully reconstructed (same lookback Unpack() uses).
        iterator = BlobIterator(self.dtype_inspector, self.simhier)
        handler = DataExtractionHandler(self.simhier, snapshot_cids=cids)
        clock_ids = self._clock_ids_for_elem_paths(elem_paths)
        iterator.Iterate(handler, [int(start_tick), int(end_tick)], lookback=True, clock_ids=clock_ids)

        unpacked = {}
        for elem_path in elem_paths:
            values_by_tick = handler.GetValuesByTick(elem_path)
            clock_id = self._clock_id_for_elem_path(elem_path)
            time_vals, data_vals = [], []
            for tick in sorted(values_by_tick.keys()):
                if start_tick <= tick <= end_tick and self._clock_collected_at_tick(clock_id, tick):
                    time_vals.append(tick)
                    data_vals.append(values_by_tick[tick])
            unpacked[elem_path] = {'TimeVals': time_vals, 'DataVals': data_vals}

        return unpacked

    def GetAllTimeVals(self):
        return copy.deepcopy(self._time_vals)
