import wx, copy, re
from collections import OrderedDict
from viewer.gui.view_settings import DirtyReasons
from viewer.gui.widgets.grid import Grid
from functools import partial

class SchedulingLinesWidget(wx.Panel):
    DEFAULT_TICKS_BEFORE = 10
    DEFAULT_TICKS_AFTER = 10
    DEFAULT_SHOW_DETAILS = True
    DEFAULT_HIDE_EMPTY_ROWS = True
    DEFAULT_SHOW_FULL_PATHS = False
    DEFAULT_ENABLE_TOOLTIPS = False
    DEFAULT_SHOW_DID = False

    def __init__(self, parent, frame, elem_paths=None, num_ticks_before=DEFAULT_TICKS_BEFORE, num_ticks_after=DEFAULT_TICKS_AFTER, show_details=DEFAULT_SHOW_DETAILS, hide_empty_rows=DEFAULT_HIDE_EMPTY_ROWS, show_full_paths=DEFAULT_SHOW_FULL_PATHS, enable_tooltips=DEFAULT_ENABLE_TOOLTIPS, show_did=DEFAULT_SHOW_DID):
        super().__init__(parent)
        self.frame = frame
        self.num_ticks_before = num_ticks_before
        self.num_ticks_after = num_ticks_after
        self.show_detailed_queue_packets = show_details
        self.hide_empty_rows = hide_empty_rows
        self.show_full_paths = show_full_paths
        self.enable_tooltips = enable_tooltips
        self.show_did = show_did
        self.caption_mgr = CaptionManager(frame.simhier)
        self.tracked_annos = {}
        self.grid = None
        self.rasterizers = {}

        cursor = frame.db.cursor()
        cmd = 'SELECT CID,MaxSize FROM QueueMaxSizes'

        cursor.execute(cmd)
        self.queue_max_sizes_by_collection_id = {}
        for collection_id,max_size in cursor.fetchall():
            self.queue_max_sizes_by_collection_id[collection_id] = max_size

        if elem_paths:
            self.SetElements(elem_paths)

    @staticmethod
    def GetRootDataTypeName(frame, elem_path):
        collection_id = frame.simhier.GetCollectionID(elem_path)
        dtype = frame.dtype_inspector.GetDataTypeForCollectionID(collection_id)
        if not dtype:
            return None

        idx = dtype.find('_sparse_capacity')
        if idx != -1:
            return dtype[:idx]

        idx = dtype.find('_contig_capacity')
        if idx != -1:
            return dtype[:idx]

        return dtype

    @classmethod
    def ElemPathHasDidField(cls, frame, elem_path):
        dtype = cls.GetRootDataTypeName(frame, elem_path)
        if not dtype:
            return False
        return frame.dtype_inspector.GetEffectiveColorKey(dtype) == 'DID'

    @classmethod
    def AnyElemPathHasDidField(cls, frame, elem_paths):
        return any(cls.ElemPathHasDidField(frame, elem_path) for elem_path in elem_paths)

    def GetWidgetCreationString(self):
        return 'Scheduling Lines'

    def GetErrorIfDroppedNodeIncompatible(self, elem_path):
        simhier = self.frame.simhier
        is_timeseries = elem_path in simhier.GetScalarStatsElemPaths()
        is_container = elem_path in simhier.GetContainerElemPaths()

        if not is_container:
            msg = 'Only leaf nodes that are containers (queues) can be dropped here. '
            msg += 'This node represents a scalar stat (timeseries).' if is_timeseries else 'This node represents a struct.'
            return msg, 'Incompatible Node'

        if elem_path in self.caption_mgr.GetAllMatchingElemPaths():
            return 'This queue is already being displayed.', 'Duplicate Queue'
        
        if self.grid:
            existing_captions = set()
            for row in range(self.grid.GetNumberRows()):
                existing_captions.add(self.grid.GetCellValue(row, 0).rstrip())

            todo_captions = self.__GetCaptionsForElement(elem_path, self.__GetCaptionElemPathsIncluding(elem_path))
            for caption in todo_captions:
                if caption in existing_captions:
                    msg = 'Adding this to the Scheduling Lines widget would result in a duplicate caption(s). '
                    msg += 'You need to open the widget settings dialog and adjust the regexes.'
                    return msg, 'Duplicate Caption'

        return None

    def AddElement(self, elem_path):
        self.__AddElement(elem_path)
        self.__Refresh()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.SchedulingLinesWidgetChanged)

    def SetElements(self, elem_paths):
        # Filter out all the collectables that did not collect any data
        warning = []
        elem_paths_with_data = []
        for elem_path in elem_paths:
            elem_cid = self.frame.simhier.GetCollectionID(elem_path)
            has_data = self.queue_max_sizes_by_collection_id.get(elem_cid, 0) > 0
            if has_data:
                elem_paths_with_data.append(elem_path)
            else:
                if not warning:
                    warning.append('No data collected and will not be displayed:')
                warning.append('  - ' + elem_path)

        if warning:
            warning = '\n'.join(warning)
            wx.MessageBox(warning, 'Warning', wx.OK | wx.ICON_WARNING)

        self.caption_mgr.ClearSelections()
        for elem_path in elem_paths_with_data:
            self.__AddElement(elem_path)

        self.__Refresh()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.SchedulingLinesWidgetChanged)

    def UpdateWidgetData(self, regenerate_grid=False):
        if not self.grid:
            return

        self.__Refresh(regenerate_grid)

    def GetCurrentViewSettings(self):
        settings = {}
        settings['regexes'] = self.caption_mgr.GetElemPathRegexReplacements(as_list=True)
        settings['num_ticks_before'] = self.num_ticks_before
        settings['num_ticks_after'] = self.num_ticks_after
        settings['show_detailed_queue_packets'] = self.show_detailed_queue_packets
        settings['hide_empty_rows'] = self.hide_empty_rows
        settings['show_full_paths'] = self.show_full_paths
        settings['enable_tooltips'] = self.enable_tooltips
        settings['show_did'] = self.show_did
        settings['tracked_annos'] = copy.deepcopy(self.tracked_annos)
        return settings
    
    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        dirty = self.caption_mgr.GetElemPathRegexReplacements(as_list=True) != settings['regexes'] or \
                self.num_ticks_before != settings['num_ticks_before'] or \
                self.num_ticks_after != settings['num_ticks_after'] or \
                self.show_detailed_queue_packets != settings['show_detailed_queue_packets'] or \
                self.hide_empty_rows != settings['hide_empty_rows'] or \
                self.show_full_paths != settings['show_full_paths'] or \
                self.enable_tooltips != settings['enable_tooltips'] or \
                self.show_did != settings['show_did'] or \
                self.tracked_annos != settings['tracked_annos']

        if not dirty:
            return

        self.caption_mgr.SetElemPathRegexReplacements(settings['regexes'])
        self.num_ticks_before = settings['num_ticks_before']
        self.num_ticks_after = settings['num_ticks_after']
        self.show_detailed_queue_packets = settings['show_detailed_queue_packets']
        self.hide_empty_rows = settings['hide_empty_rows']
        self.show_full_paths = settings['show_full_paths']
        self.enable_tooltips = settings['enable_tooltips']
        self.show_did = settings['show_did']
        self.tracked_annos = settings['tracked_annos']

        self.__Refresh()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.SchedulingLinesWidgetChanged)

    def __AddElement(self, elem_path):
        assert elem_path not in self.caption_mgr.GetAllMatchingElemPaths()
            
        # The default behavior is to take an element path like this:
        #   top.cpu.core0.rob.stats.num_insts_retired
        #
        # And use the caption replacement:
        #   NumInstsRetired
        #
        # Which results in captions like this (assume queue has capacity of 4):
        #
        #   NumInstsRetired[3]
        #   NumInstsRetired[2]
        #   NumInstsRetired[1]
        #   NumInstsRetired[0]
        #
        # A complete example might be to also use the core index in the caption,
        # which would change the regex to:
        #
        #   top.cpu.core([0-9]+).rob.stats.num_insts_retired
        #
        # And use the caption replacement:
        #
        #   NumInstsRetired\1
        #
        # Which results in captions like this (assume queue has capacity of 4):
        #
        #   NumInstsRetired0[3]
        #   NumInstsRetired0[2]
        #   NumInstsRetired0[1]
        #   NumInstsRetired0[0]
        #
        #   NumInstsRetired1[3]
        #   NumInstsRetired1[2]
        #   NumInstsRetired1[1]
        #   NumInstsRetired1[0]
        #
        # The user can adjust these settings in the widget settings dialog.
        self.caption_mgr.SetElemPathRegexReplacement(elem_path, elem_path)

    def __Refresh(self, new_grid=True):
        if len(self.caption_mgr.GetAllMatchingElemPaths()) > 0:
            # Preserve the scrollbar position across the grid regeneration below.
            # The old grid is destroyed and a brand-new one is created, which
            # would otherwise reset the scroll position back to the top.
            saved_view_start = self.grid.GetViewStart() if self.grid else None

            start_time = self.frame.widget_renderer.tick - self.num_ticks_before
            end_time = self.frame.widget_renderer.tick + self.num_ticks_after
            elem_paths = self.caption_mgr.GetAllMatchingElemPaths()
            self._caption_elem_paths = elem_paths
            self._ranges = self.frame.data_retriever.UnpackRange(start_time, end_time, elem_paths)
            self._bins_with_data_by_elem_path = self.__GetBinsWithDataByElemPath(self._ranges, elem_paths)
            self._layouts_by_elem_path = {}
            for elem_path in elem_paths:
                bins_with_data = self._bins_with_data_by_elem_path[elem_path]
                self._layouts_by_elem_path[elem_path] = self.__BuildRowLayout(elem_path, bins_with_data)

            self.SetBackgroundColour('white')
            self.__RegenerateSchedulingLinesGrid(new_grid)
            self.__RasterizeAllCells()

            # Restore the scroll position after the new grid has been laid out
            # and auto-sized (which establishes its scroll range).
            if saved_view_start is not None:
                #wx.CallAfter(self.grid.Scroll, saved_view_start[0], saved_view_start[1])
                self.grid.Scroll(saved_view_start[0], saved_view_start[1])

    def __RegenerateSchedulingLinesGrid(self, new_grid):
        if self.grid:
            self.grid.Destroy()
            self.grid = None

        sizer = self.GetSizer()
        if sizer:
            sizer.Clear()

        sizer = wx.BoxSizer(wx.VERTICAL)

        self._struct_dtypes_by_row = {}
        num_rows = 0
        for elem_path in self.caption_mgr.GetAllMatchingElemPaths():
            collection_id = self.frame.simhier.GetCollectionID(elem_path)
            layout = self._layouts_by_elem_path[elem_path]

            dtype = self.frame.dtype_inspector.GetDataTypeForCollectionID(collection_id)
            idx = dtype.find('_sparse_capacity')
            if idx != -1:
                dtype = dtype[:idx]
            else:
                idx = dtype.find('_contig_capacity')
                if idx != -1:
                    dtype = dtype[:idx]

            for i, segment in enumerate(layout):
                row = num_rows + i
                self._struct_dtypes_by_row[row] = dtype

            num_rows += len(layout)

        # The number of columns can be calculated as:
        #  1. Start with the sum of self.num_ticks_before and self.num_ticks_after (A)
        #  2. Add 1 to (A) to account for the element paths column (captions)
        #  3. If self.show_detailed_queue_packets is True, add 3 to (A) to account for:
        #     a. A column to add a separator between the summary and detailed sections
        #     b. A column to duplicate the element paths column (captions)
        #     c. A column to show the stringified packet data e.g. "IntVal(4) DoubleVal(3.14)"

        num_cols = self.num_ticks_before + self.num_ticks_after + 1
        if self.show_detailed_queue_packets:
            num_cols += 2

        # Create 8-point monospace font for the grid cells
        font8 = wx.Font(8, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)

        # Create 10-point font for the grid column labels
        font10 = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)

        if new_grid or self.grid is None:
            self.grid = Grid(self, self.frame, num_rows, num_cols, cell_font=font8, label_font=font10, cell_selection_allowed=False)
        self.grid.GetGridWindow().Bind(wx.EVT_MOTION, self.__OnGridMouseMotion)
        self.grid.EnableGridLines(False)
        self.grid.SetLabelBackgroundColour('white')

        gear_btn, clear_btn, split_lr, split_tb, maximize_btn = self.frame.CreateWidgetStandardButtons(
            self, self.__EditWidget, 'Edit widget settings')

        current_tick = self.frame.widget_renderer.tick
        col_labels = []
        time_vals = self.frame.data_retriever.GetAllTimeVals()
        time_vals = {int(val) for val in time_vals}
        for col in range(1, self.num_ticks_before + self.num_ticks_after + 1):
            tick = current_tick - self.num_ticks_before + col - 1
            if int(tick) in time_vals:
                self.grid.SetColLabelValue(col, str(tick))
                col_labels.append(str(tick))
            else:
                self.grid.SetColLabelValue(col, '')

        if self.show_detailed_queue_packets:
            detailed_pkt_col = self.num_ticks_before + self.num_ticks_after + 2
            self.grid.SetColLabelValue(detailed_pkt_col - 1, '')
            if int(current_tick) in time_vals:
                self.grid.SetColLabelValue(detailed_pkt_col, str(current_tick))
                col_labels.append(str(current_tick))
            else:
                self.grid.SetColLabelValue(detailed_pkt_col, '')

        # Use a DC to get the length of the longest col label
        dc = wx.ScreenDC()
        dc.SetFont(self.grid.GetLabelFont())
        max_col_label_len = max([dc.GetTextExtent(col_label)[0] for col_label in col_labels]) if col_labels else 0
        self.grid.SetColLabelSize(max_col_label_len + 4)

        self.grid.SetColLabelValue(0, '')
        self.grid.SetColLabelTextOrientation(wx.VERTICAL)
        self.grid.HideRowLabels()

        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        btn_sizer.Add(gear_btn, 0, wx.TOP | wx.RIGHT | wx.LEFT, 5)
        btn_sizer.Add(clear_btn, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(split_lr, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(split_tb, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(maximize_btn, 0, wx.TOP, 5)
        sizer.Add(btn_sizer, 0, wx.BOTTOM, 5)

        sizer.Add(self.grid, 0, wx.EXPAND)
        self.SetSizer(sizer)

        self.grid.ClearGrid()

        # Draw a thick black line to mark the current time
        for row in range(num_rows):
            self.grid.SetCellBorder(row, self.num_ticks_before, 1, wx.RIGHT)
            self.grid.SetCellBorder(row, self.num_ticks_before + 1, 1, wx.LEFT)

        self.__SetElementCaptions(0)

    def __RasterizeAllCells(self):
        for elem_path, vals in self._ranges.items():
            time_vals = vals['TimeVals']
            data_vals = vals['DataVals']

            for i, data_dicts in enumerate(data_vals):
                if data_dicts is None:
                    continue

                time_val = time_vals[i]
                for bin_idx, annos in enumerate(data_dicts):
                    self.__RerouteUnpackedDataToRasterizer(time_val, elem_path, bin_idx, annos)

        # Left-justify the detailed packet column
        if self.show_detailed_queue_packets:
            col = self.num_ticks_before + self.num_ticks_after + 2

            def GetMaxFieldVarLengths(strings):
                result = {}

                for row in strings:
                    for field, value in re.findall(r'(\w+)\((.*?)\)', row):
                        result[field] = max(
                            result.get(field, 0),
                            len(value)
                        )

                return result

            def AlignLabel(label, max_varlens_by_field):
                parts = []

                for field, value in re.findall(r'(\w+)\((.*?)\)', label):
                    target = max_varlens_by_field[field]

                    part = f'{field}({value})'

                    # Pad based on value width difference
                    pad = target - len(value)
                    parts.append(part + (' ' * pad))

                return ' '.join(parts)

            labels = [self.grid.GetCellValue(row,col).strip() for row in range(self.grid.GetNumberRows())]
            labels_by_dtype = {}
            for row, dtype in self._struct_dtypes_by_row.items():
                if dtype not in labels_by_dtype:
                    labels_by_dtype[dtype] = []
                labels_by_dtype[dtype].append(labels[row])

            for row, label in enumerate(labels):
                if not self.show_did and 'DID' in label:
                    parts = label.split()
                    new_label_parts = []
                    for p in parts:
                        if p.find('DID(') != 0:
                            new_label_parts.append(p)
                    label = ' '.join(new_label_parts)

                if row in self._struct_dtypes_by_row:
                    row_dtype = self._struct_dtypes_by_row[row]
                    row_align_labels = labels_by_dtype[row_dtype]
                    max_varlens_by_field = GetMaxFieldVarLengths(row_align_labels)
                    label = AlignLabel(label, max_varlens_by_field)

                self.grid.SetCellValue(row, col, label)

        self.grid.AutoSize()
        self.Layout()
        self.Update()
        self.Refresh()

    def __RerouteUnpackedDataToRasterizer(self, time_val, elem_path, bin_idx, annos):
        key = (elem_path, bin_idx)
        if key in self.rasterizers:
            self.rasterizers[key].Draw(elem_path, bin_idx, time_val, annos)

    def __SetElementCaptions(self, col):
        if col == 0:
            self.rasterizers = {}

        font = self.grid.GetLabelFont()
        for row in range(self.grid.GetNumberRows()):
            self.grid.SetCellFont(row, col, font)
            if col > 0:
                self.grid.SetCellFont(row, col+1, font)

        row = 0
        captions = []
        for elem_path in self.caption_mgr.GetAllMatchingElemPaths():
            for segment in self._layouts_by_elem_path[elem_path]:
                caption = self.__FormatSegmentCaption(elem_path, segment)
                tooltip = self.__GetCaptionColumnTooltip(elem_path, segment, caption)

                captions.append(caption)
                if tooltip:
                    self.grid.SetCellToolTip(row, col, tooltip)
                else:
                    self.grid.UnsetCellToolTip(row, col)
                row += 1

        max_num_chars = max([len(caption) for caption in captions])

        if self.show_detailed_queue_packets:
            num_visible_columns = 0
            for i in range(self.grid.GetNumberCols()):
                if self.grid.IsColShown(i):
                    num_visible_columns += 1

            detailed_pkt_col = num_visible_columns - 1
        else:
            detailed_pkt_col = -1

        row_offset = 0
        for elem_path in self.caption_mgr.GetAllMatchingElemPaths():
            row_offset += self.__SetCaptionsForElement(elem_path, row_offset, col, max_num_chars, detailed_pkt_col)

    def __SetCaptionsForElement(self, elem_path, row_offset, col, max_num_chars, detailed_pkt_col):
        layout = self._layouts_by_elem_path[elem_path]

        for i, segment in enumerate(layout):
            caption = self.__FormatSegmentCaption(elem_path, segment)
            caption += ' ' * (max_num_chars - len(caption))
            self.grid.SetCellValue(row_offset + i, col, caption)

            if segment['kind'] == 'bin':
                bin_idx = segment['bin']
                self.rasterizers[(elem_path, bin_idx)] = Rasterizer(
                    self.frame, self.grid, self, elem_path, bin_idx, row_offset + i, detailed_pkt_col)

        return len(layout)

    def __GetCaptionElemPathsIncluding(self, elem_path):
        elem_paths = list(self.caption_mgr.GetAllMatchingElemPaths())
        if elem_path not in elem_paths:
            elem_paths.append(elem_path)
        return elem_paths

    def __GetCaptionsForElement(self, elem_path, elem_paths=None):
        segments = self.__BuildStaticRowLayout(elem_path)
        return [self.__FormatSegmentCaption(elem_path, segment, elem_paths) for segment in segments]

    def __GetBinsWithDataByElemPath(self, ranges, elem_paths):
        bins_with_data_by_elem_path = {}
        for elem_path in elem_paths:
            bins_with_data = set()
            vals = ranges.get(elem_path, {'DataVals': []})
            for data_dicts in vals['DataVals']:
                if data_dicts is None:
                    continue
                for bin_idx, annos in enumerate(data_dicts):
                    if annos is not None:
                        bins_with_data.add(bin_idx)
            bins_with_data_by_elem_path[elem_path] = bins_with_data
        return bins_with_data_by_elem_path

    def __BuildRowLayout(self, elem_path, bins_with_data):
        if self.hide_empty_rows:
            return self.__BuildDynamicRowLayout(elem_path, bins_with_data)
        return self.__BuildStaticRowLayout(elem_path)

    def __BuildStaticRowLayout(self, elem_path):
        collection_id = self.frame.simhier.GetCollectionID(elem_path)
        num_bins = self.frame.simhier.GetCapacityByCollectionID(collection_id)
        max_size = self.queue_max_sizes_by_collection_id[collection_id]

        if max_size == 0:
            return [{'kind': 'no_data'}]

        segments = []
        if max_size < num_bins:
            segments.append({'kind': 'range', 'lo': max_size - 1, 'hi': num_bins - 1})
            for i in range(1, max_size):
                bin_idx = max_size - i - 1
                segments.append({'kind': 'bin', 'bin': bin_idx})
        else:
            for i in range(num_bins):
                bin_idx = num_bins - i - 1
                segments.append({'kind': 'bin', 'bin': bin_idx})
        return segments

    def __BuildDynamicRowLayout(self, elem_path, bins_with_data):
        collection_id = self.frame.simhier.GetCollectionID(elem_path)
        num_bins = self.frame.simhier.GetCapacityByCollectionID(collection_id)

        segments = []
        run_hi = None
        for bin_idx in range(num_bins - 1, -1, -1):
            if bin_idx in bins_with_data:
                if run_hi is not None:
                    segments.append({'kind': 'range', 'lo': bin_idx + 1, 'hi': run_hi})
                    run_hi = None
                segments.append({'kind': 'bin', 'bin': bin_idx})
            elif run_hi is None:
                run_hi = bin_idx

        if run_hi is not None:
            segments.append({'kind': 'range', 'lo': 0, 'hi': run_hi})
        return segments

    def __FormatSegmentCaption(self, elem_path, segment, elem_paths=None):
        if elem_paths is None:
            elem_paths = self._caption_elem_paths

        if segment['kind'] == 'no_data':
            return '{}(no data)'.format(
                self.caption_mgr.GetCaptionPrefix(elem_path, elem_paths, self.show_full_paths))
        if segment['kind'] == 'range':
            caption_prefix = self.caption_mgr.GetCaptionPrefix(elem_path, elem_paths, self.show_full_paths)
            lo = segment['lo']
            hi = segment['hi']
            if lo == hi:
                return '{}[{}]'.format(caption_prefix, lo)
            return '{}[{}-{}]'.format(caption_prefix, lo, hi)
        return self.caption_mgr.GetCaption(elem_path, segment['bin'], elem_paths, self.show_full_paths)

    def __GetCaptionColumnTooltip(self, elem_path, segment, caption):
        full_tooltip = self.__SegmentElemPathTooltip(elem_path, segment)
        if caption.rstrip() == full_tooltip:
            return None

        if not self.show_full_paths:
            return full_tooltip

        if self.enable_tooltips:
            return full_tooltip
        return None

    def __SegmentElemPathTooltip(self, elem_path, segment):
        if segment['kind'] == 'no_data':
            return elem_path
        if segment['kind'] == 'range':
            lo = segment['lo']
            hi = segment['hi']
            if lo == hi:
                return '{}[{}]'.format(elem_path, lo)
            return '{}[{}-{}]'.format(elem_path, lo, hi)
        return '{}[{}]'.format(elem_path, segment['bin'])
    
    def __OnGridMouseMotion(self, evt):
        x, y = self.grid.CalcUnscrolledPosition(evt.GetX(), evt.GetY())
        row, col = self.grid.XYToCell(x, y)

        if col == 0 or self.enable_tooltips:
            tooltip = self.grid.GetCellToolTip(row, col)
        else:
            tooltip = None

        if tooltip:
            self.grid.SetToolTip(tooltip)
        else:
            self.grid.UnsetToolTip()

    def __EditWidget(self, evt):
        widget_container = self.GetParent()
        widget_container.LaunchSchedulingLinesViewer()

class CaptionManager:
    MINIMUM_CAPTION_PATH_PARTS = 2

    def __init__(self, simhier):
        self.simhier = simhier
        self.ClearSelections()

    @classmethod
    def GetMinimumUniqueSuffix(cls, elem_path, elem_paths, min_parts=MINIMUM_CAPTION_PATH_PARTS):
        parts = elem_path.split('.')
        all_parts = [path.split('.') for path in elem_paths]

        for suffix_len in range(1, len(parts) + 1):
            my_suffix = parts[-suffix_len:]
            matches = sum(
                1 for other_parts in all_parts
                if len(other_parts) >= suffix_len and other_parts[-suffix_len:] == my_suffix
            )
            if matches == 1:
                display_len = max(min_parts, suffix_len)
                return '.'.join(parts[-display_len:])

        return elem_path

    @classmethod
    def ApplyPartialPathTooltip(cls, control, full_path, label_text, show_full_paths):
        if not show_full_paths and label_text.rstrip() != full_path:
            control.SetToolTip(full_path)
        else:
            control.UnsetToolTip()

    def ClearSelections(self):
        self.regex_replacements_by_elem_path_regex = OrderedDict()

    def SetElemPathRegexReplacement(self, elem_path_regex, regex_replacement):
        self.regex_replacements_by_elem_path_regex[elem_path_regex] = regex_replacement

    def SetElemPathRegexReplacements(self, regex_replacements_by_elem_path_regex):
        if isinstance(regex_replacements_by_elem_path_regex, list):
            regex_replacements_by_elem_path_regex = OrderedDict(regex_replacements_by_elem_path_regex)
        elif not isinstance(regex_replacements_by_elem_path_regex, OrderedDict):
            raise TypeError('Must be a list or an OrderedDict, not a regular unordered python dict.')

        self.regex_replacements_by_elem_path_regex = copy.deepcopy(regex_replacements_by_elem_path_regex)

    def GetElemPathRegexReplacements(self, as_list=False):
        d = copy.deepcopy(self.regex_replacements_by_elem_path_regex)
        if as_list:
            return list(d.items())

        return d

    def GetCaption(self, elem_path, bin_idx, elem_paths=None, show_full_paths=False):
        if elem_paths is None:
            elem_paths = self.GetAllMatchingElemPaths()

        for regex, replacements in self.regex_replacements_by_elem_path_regex.items():
            if regex == elem_path:
                # No regex was supplied in the settings dialog. The full path was given e.g.
                #   "top.cpu.core0.rob.stats.num_insts_retired"
                # 
                # Instead of something like:
                #   "top.cpu.core([0-9]+).rob.stats.num_insts_retired"
                #
                # We will just return the last part of the path as the caption using
                # heads-up camel case e.g. "NumInstsRetired[3]"
                return self.GetCaptionPrefix(elem_path, elem_paths, show_full_paths) + '[{}]'.format(bin_idx)

            if re.compile(regex).match(elem_path):
                # This matched an elem path e.g.
                #   "top.cpu.core1.rob.stats.num_insts_retired"
                #
                # With a regex e.g.
                #   "top.cpu.core([0-9]+).rob.stats.num_insts_retired"
                #
                # We will return something like "NumInstsRetired1[3]"
                #                                               ^ ^
                #                                               | |
                #                                               | bin index
                #                                               core index
                return re.sub(regex, replacements, elem_path) + '[{}]'.format(bin_idx)

        prefix = elem_path if show_full_paths else self.GetMinimumUniqueSuffix(elem_path, elem_paths)
        return f'{prefix}[{bin_idx}]'
    
    def GetCaptionPrefix(self, elem_path, elem_paths=None, show_full_paths=False):
        if elem_paths is None:
            elem_paths = self.GetAllMatchingElemPaths()

        for regex, replacements in self.regex_replacements_by_elem_path_regex.items():
            if regex == elem_path:
                if replacements == elem_path:
                    if show_full_paths:
                        return elem_path
                    return self.GetMinimumUniqueSuffix(elem_path, elem_paths)
                return replacements

            if re.compile(regex).match(elem_path):
                return re.sub(regex, replacements, elem_path)

        return None

    def GetAllMatchingElemPaths(self):
        # The display order of the queues/rows follows the order of the regex
        # OrderedDict (i.e. the order the user set in the customization dialog),
        # NOT the static order of self.simhier.GetContainerElemPaths(). The regex
        # dict is therefore the outer loop here.
        container_elem_paths = self.simhier.GetContainerElemPaths()

        elem_paths = []
        seen = set()
        for regex, _ in self.regex_replacements_by_elem_path_regex.items():
            # Exact path match first (the regex is a full element path).
            if regex in container_elem_paths and regex not in seen:
                elem_paths.append(regex)
                seen.add(regex)
                continue

            # Otherwise treat it as a regex and append all matching container
            # paths (in stable simhier order within this single regex group).
            compiled = re.compile(regex)
            for elem_path in container_elem_paths:
                if elem_path in seen:
                    continue

                if compiled.match(elem_path):
                    elem_paths.append(elem_path)
                    seen.add(elem_path)

        return elem_paths

    def GetRegex(self, elem_path):
        for regex, _ in self.regex_replacements_by_elem_path_regex.items():
            if re.compile(regex).match(elem_path):
                return regex

        return None

    def GetMatchingElemPaths(self, regex):
        elem_paths = []
        for elem_path in self.simhier.GetContainerElemPaths():
            if re.compile(regex).match(elem_path):
                elem_paths.append(elem_path)

        return elem_paths

class Rasterizer:
    def __init__(self, frame, grid, widget, elem_path, bin_idx, row, detailed_pkt_col):
        self.frame = frame
        self.grid = grid
        self.widget = widget
        self.elem_path = elem_path
        self.bin_idx = bin_idx
        self.row = row
        self.detailed_pkt_col = detailed_pkt_col

    def Draw(self, elem_path, bin_idx, time_val, annos):
        if not annos:
            return

        assert elem_path == self.elem_path
        assert bin_idx == self.bin_idx

        auto_colorize_column = self.frame.data_retriever.GetAutoColorizeColumn(elem_path)
        auto_colorize_key = None
        for key, keyval in annos:
            if key == auto_colorize_column:
                auto_colorize_key = keyval
                break

        assert auto_colorize_key is not None
        auto_color = self.frame.widget_renderer.GetAutoColor(auto_colorize_key)
        auto_label = self.frame.widget_renderer.GetAutoTag(auto_colorize_key)

        anno = []
        for k, v in annos:
            if k == 'DID' and not self.widget.show_did:
                continue
            anno.append('{}({})'.format(k, v))

        stringized_tooltip = '\n'.join(anno)
        stringized_anno = ' '.join(anno)

        tracked_annos = self.widget.tracked_annos
        show_border = auto_colorize_column in tracked_annos and tracked_annos[auto_colorize_column] == auto_colorize_key

        for col in range(self.grid.GetNumberCols()):
            if not self.grid.IsColShown(col):
                break

            col_label = self.grid.GetColLabelValue(col)
            try:
                col_label = int(col_label)
            except:
                continue

            if col_label == int(time_val):
                self.grid.SetCellValue(self.row, col, auto_label)
                self.grid.SetCellBackgroundColour(self.row, col, auto_color)
                if self.widget.enable_tooltips:
                    self.grid.SetCellToolTip(self.row, col, stringized_tooltip)

                border_width = 1 if show_border else self.grid.GetCellBorderWidth(self.row, col)
                border_side = wx.ALL if show_border else self.grid.GetCellBorderSide(self.row, col)
                self.grid.SetCellBorder(self.row, col, border_width, border_side)
                break

        if self.detailed_pkt_col != -1 and time_val == self.frame.widget_renderer.tick:
            def Strip(stringized_anno, string, replace):
                while string in stringized_anno:
                    stringized_anno = stringized_anno.replace(string, replace)
                return stringized_anno

            stringized_anno = Strip(stringized_anno, '((', '(')
            stringized_anno = Strip(stringized_anno, '))', ')')
            self.grid.SetCellValue(self.row, self.detailed_pkt_col, stringized_anno)
            self.grid.SetCellAlignment(self.row, self.detailed_pkt_col, wx.ALIGN_CENTER_VERTICAL)
            self.grid.SetCellBackgroundColour(self.row, self.detailed_pkt_col, auto_color)
            if self.widget.enable_tooltips:
                self.grid.SetCellToolTip(self.row, self.detailed_pkt_col, stringized_tooltip)
            if show_border:
                self.grid.SetCellBorder(self.row, self.detailed_pkt_col, 1, wx.ALL)
