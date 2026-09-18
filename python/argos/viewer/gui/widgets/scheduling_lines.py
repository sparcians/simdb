import wx, copy, re, os
from collections import OrderedDict
from viewer.gui.view_settings import DirtyReasons
from viewer.gui.widgets.grid import Grid

class SchedulingLinesWidget(wx.Panel):
    DEFAULT_TICKS_BEFORE = 10
    DEFAULT_TICKS_AFTER = 10
    DEFAULT_SHOW_DETAILS = True
    DEFAULT_HIDE_EMPTY_ROWS = True
    DEFAULT_ENABLE_TOOLTIPS = True
    DEFAULT_SHOW_DID = False
    DEFAULT_MINIMIZE_GRID_CELLS = False

    def __init__(self, parent, frame, elem_paths=None, num_samples_before=DEFAULT_TICKS_BEFORE, num_samples_after=DEFAULT_TICKS_AFTER, show_details=DEFAULT_SHOW_DETAILS, hide_empty_rows=DEFAULT_HIDE_EMPTY_ROWS, enable_tooltips=DEFAULT_ENABLE_TOOLTIPS, show_did=DEFAULT_SHOW_DID, minimize_grid_cells=DEFAULT_MINIMIZE_GRID_CELLS):
        super().__init__(parent)
        self.frame = frame
        self.num_samples_before = num_samples_before
        self.num_samples_after = num_samples_after
        self.show_detailed_queue_packets = show_details
        self.hide_empty_rows = hide_empty_rows
        self.enable_tooltips = enable_tooltips
        self.show_did = show_did
        self.minimize_grid_cells = minimize_grid_cells
        self.caption_mgr = CaptionManager(frame.simhier)
        self.tracked_annos = {}
        self.grid = None
        self.rasterizers = {}
        self.scalar_row_by_elem_path = {}
        self.scalar_elem_paths = set(frame.simhier.GetScalarStatsElemPaths()) | set(frame.simhier.GetScalarStructsElemPaths())

        cursor = frame.db.cursor()
        cmd = 'SELECT CID,MaxSize FROM QueueMaxSizes'

        cursor.execute(cmd)
        self.queue_max_sizes_by_collection_id = {}
        for collection_id,max_size in cursor.fetchall():
            self.queue_max_sizes_by_collection_id[collection_id] = max_size

        # Scalars are single-value collectables, not queue bins. They still have
        # a collection ID and should be treated as having data for display purposes,
        # even though they do not participate in queue-capacity logic.
        for elem_path in self.scalar_elem_paths:
            collection_id = frame.simhier.GetCollectionID(elem_path)
            if collection_id is not None:
                self.queue_max_sizes_by_collection_id[collection_id] = 1

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

    def AddElement(self, elem_path):
        self.__AddElement(elem_path)
        self.__Refresh()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.SchedulingLinesWidgetChanged)

    def SetElements(self, elem_paths):
        self.caption_mgr.ClearSelections()
        for elem_path in elem_paths:
            self.__AddElement(elem_path)

        self.__Refresh()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.SchedulingLinesWidgetChanged)

    def UpdateWidgetData(self, regenerate_grid=False):
        if not self.grid:
            return

        self.__Refresh(regenerate_grid)

    def GetCurrentViewSettings(self):
        settings = {}

        # TODO cnyce: The regex replacements are from an older design which no longer applies.
        # We should clean up the CaptionManager to just use a flat list of elem paths.
        settings['displayed_elems'] = self.__GetDisplayedElemPaths()
        settings['custom_captions'] = self.caption_mgr.GetCustomCaptions()
        settings['num_samples_before'] = self.num_samples_before
        settings['num_samples_after'] = self.num_samples_after
        settings['show_detailed_queue_packets'] = self.show_detailed_queue_packets
        settings['hide_empty_rows'] = self.hide_empty_rows
        settings['enable_tooltips'] = self.enable_tooltips
        settings['show_did'] = self.show_did
        settings['minimize_grid_cells'] = self.minimize_grid_cells
        settings['tracked_annos'] = copy.deepcopy(self.tracked_annos)
        return settings
    
    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        custom_captions = settings.get('custom_captions', {})
        dirty = self.__GetDisplayedElemPaths() != settings['displayed_elems'] or \
                self.caption_mgr.GetCustomCaptions() != custom_captions or \
                self.num_samples_before != settings['num_samples_before'] or \
                self.num_samples_after != settings['num_samples_after'] or \
                self.show_detailed_queue_packets != settings['show_detailed_queue_packets'] or \
                self.hide_empty_rows != settings['hide_empty_rows'] or \
                self.enable_tooltips != settings['enable_tooltips'] or \
                self.show_did != settings['show_did'] or \
                self.minimize_grid_cells != settings['minimize_grid_cells'] or \
                self.tracked_annos != settings['tracked_annos']

        if not dirty:
            return

        self.caption_mgr.SetElemPathRegexReplacements(settings['displayed_elems'])
        self.caption_mgr.SetCustomCaptions(custom_captions)
        self.num_samples_before = settings['num_samples_before']
        self.num_samples_after = settings['num_samples_after']
        self.show_detailed_queue_packets = settings['show_detailed_queue_packets']
        self.hide_empty_rows = settings['hide_empty_rows']
        self.enable_tooltips = settings['enable_tooltips']
        self.show_did = settings['show_did']
        self.minimize_grid_cells = settings['minimize_grid_cells']
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

            current_tick = self.frame.widget_renderer.tick
            elem_paths = self.caption_mgr.GetAllMatchingElemPaths()
            self._caption_elem_paths = elem_paths

            # Paths that don't exist in the simulation hierarchy (e.g. typed in
            # manually and never collected) can't be queried for data; they are
            # still shown as a row, just without any data to rasterize.
            known_elem_paths = [p for p in elem_paths if self.__IsKnownElemPath(p)]

            self._ranges = self.frame.data_retriever.UnpackElementData(
                current_tick,
                known_elem_paths,
                self.num_samples_before,
                self.num_samples_after,
            )
            known_elem_paths = set(known_elem_paths)
            self._layouts_by_elem_path = {}
            for elem_path in elem_paths:
                if elem_path in known_elem_paths:
                    layout = self.__BuildRowLayout(elem_path)
                    self._layouts_by_elem_path[elem_path] = [
                        segment
                        for segment in layout
                        if not self.__IsSegmentHidden(elem_path, segment)
                    ]
                else:
                    self._layouts_by_elem_path[elem_path] = [{'kind': 'bad_path'}]

            self.SetBackgroundColour('white')
            self.__RegenerateSchedulingLinesGrid(new_grid)
            self.__RasterizeAllCells()

            # Restore the scroll position after the new grid has been laid out
            # and auto-sized (which establishes its scroll range).
            if saved_view_start is not None:
                #wx.CallAfter(self.grid.Scroll, saved_view_start[0], saved_view_start[1])
                self.grid.Scroll(saved_view_start[0], saved_view_start[1])

    def __IsKnownElemPath(self, elem_path):
        if elem_path in self.scalar_elem_paths:
            return True
        return self.frame.simhier.GetCollectionID(elem_path) is not None

    def __RegenerateSchedulingLinesGrid(self, new_grid):
        sizer = self.GetSizer()
        if self.grid:
            sizer.Detach(self.grid)
            self.grid.Destroy()
            self.grid = None

        self._struct_dtypes_by_row = {}
        num_rows = 0
        self._bad_path_rows = set()
        self._bad_path_elem_path_by_row = {}
        for elem_path in self.caption_mgr.GetAllMatchingElemPaths():
            collection_id = self.frame.simhier.GetCollectionID(elem_path)
            layout = self._layouts_by_elem_path[elem_path]

            dtype = self.frame.dtype_inspector.GetDataTypeForCollectionID(collection_id) or ''
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
                if segment['kind'] == 'bad_path':
                    self._bad_path_rows.add(row)
                    self._bad_path_elem_path_by_row[row] = elem_path

            num_rows += len(layout)

        # The number of columns can be calculated as:
        #  1. Start with the sum of self.num_samples_before and self.num_samples_after (A)
        #  2. Add 2 to (A) to account for the element paths column (captions)
        #     and the current-cycle timeline column
        #  3. If self.show_detailed_queue_packets is True, add 3 to (A) to account for:
        #     a. A column to add a separator between the summary and detailed sections
        #     b. A column to duplicate the element paths column (captions)
        #     c. A column to show the stringified packet data e.g. "IntVal(4) DoubleVal(3.14)"

        num_cols = self.num_samples_before + self.num_samples_after + 2
        if self.show_detailed_queue_packets:
            num_cols += 2

        # Create 8-point monospace font for the grid cells
        font8 = wx.Font(8, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)

        # Create 10-point font for the grid column labels
        font10 = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)

        cell_font = font8 if self.minimize_grid_cells else font10
        label_font = font8 if self.minimize_grid_cells else font10

        if new_grid or self.grid is None:
            self.grid = Grid(self, self.frame, num_rows, num_cols, cell_font=cell_font, label_font=label_font, cell_selection_allowed=False)
        else:
            self.grid.SetLabelFont(label_font)
            for row in range(num_rows):
                for col in range(num_cols):
                    self.grid.SetCellFont(row, col, cell_font)
        self.grid.GetGridWindow().Bind(wx.EVT_MOTION, self.__OnGridMouseMotion)
        self.grid.EnableGridLines(False)
        self.grid.SetLabelBackgroundColour('white')
        self.grid.UsePaddingEverywhere(not self.minimize_grid_cells)

        current_cycle = self.frame.playback_bar.GetCurrentCycle()
        sample_time_vals = sorted({
            int(time_val)
            for elem_data in self._ranges.values()
            for time_val in elem_data['TimeVals']
        })
        self._sample_col_by_time = {
            time_val: col + 1
            for col, time_val in enumerate(sample_time_vals)
        }
        range_cycles = list(range(
            current_cycle - self.num_samples_before,
            current_cycle + self.num_samples_after + 1,
        ))
        col_labels = []
        for col in range(1, self.num_samples_before + self.num_samples_after + 2):
            label_idx = col - 1
            if label_idx < len(range_cycles):
                cycle_offset = label_idx - self.num_samples_before
                label = str(current_cycle) if cycle_offset == 0 else f'{cycle_offset:+d}'
                self.grid.SetColLabelValue(col, label)
                col_labels.append(label)
            else:
                self.grid.SetColLabelValue(col, '')

        if self.show_detailed_queue_packets:
            detailed_pkt_col = self.num_samples_before + self.num_samples_after + 3
            self.grid.SetColLabelValue(detailed_pkt_col - 1, '')
            if current_cycle in range_cycles:
                self.grid.SetColLabelValue(detailed_pkt_col, str(current_cycle))
                col_labels.append(str(current_cycle))
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

        if sizer is None:
            sizer = wx.BoxSizer(wx.VERTICAL)

            gear_btn, clear_btn, split_lr, split_tb, maximize_btn = self.frame.CreateWidgetStandardButtons(
                self, self.__EditWidget, 'Edit widget settings')

            btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
            btn_sizer.Add(gear_btn, 0, wx.TOP | wx.RIGHT | wx.LEFT, 5)
            btn_sizer.Add(clear_btn, 0, wx.TOP | wx.RIGHT, 5)
            btn_sizer.Add(split_lr, 0, wx.TOP | wx.RIGHT, 5)
            btn_sizer.Add(split_tb, 0, wx.TOP | wx.RIGHT, 5)
            btn_sizer.Add(maximize_btn, 0, wx.TOP, 5)
            sizer.Add(btn_sizer, 0, wx.BOTTOM, 5)
            self.SetSizer(sizer)

        sizer.Add(self.grid, 0, wx.EXPAND)

        self.grid.ClearGrid()

        # Mark the data cells of rows for unrecognized ("bad") paths with an X
        # rather than trying to rasterize data that doesn't exist.
        max_data_col = self.grid.GetNumberCols() - 1
        if self.show_detailed_queue_packets:
            max_data_col -= 2
        for row in self._bad_path_rows:
            for col in range(1, max_data_col+1):
                self.grid.SetCellDrawX(row, col, True)

        current_cycle_col = self.num_samples_before + 1

        # Draw thick black lines on both sides of the current cycle.
        for row in range(num_rows):
            if current_cycle_col is not None:
                self.__AddCellBorderSides(row, current_cycle_col, wx.LEFT | wx.RIGHT)

        self.__DrawElementSeparatorBorders()

        self.__SetElementCaptions(0)

    def __AddCellBorderSides(self, row, col, border_side):
        current_width = self.grid.GetCellBorderWidth(row, col)
        current_side = self.grid.GetCellBorderSide(row, col) if current_width else 0
        self.grid.SetCellBorder(row, col, max(current_width, 1), current_side | border_side)

    def __DrawElementSeparatorBorders(self):
        elem_paths = self.caption_mgr.GetAllMatchingElemPaths()
        if len(elem_paths) < 2:
            return

        row_offset = 0
        for elem_path in elem_paths[:-1]:
            row_offset += len(self._layouts_by_elem_path[elem_path])
            separator_row = row_offset - 1
            for col in range(self.grid.GetNumberCols()):
                self.__AddCellBorderSides(separator_row, col, wx.BOTTOM)

    def __ScalarValueToString(self, value):
        if value is None:
            return ''
        if isinstance(value, (list, tuple)):
            parts = []
            for key, val in value:
                if key == 'DID' and not self.show_did:
                    continue
                parts.append(f'{key}({val})')
            return ' '.join(parts)
        return str(value)

    def __RasterizeAllCells(self):
        if self.enable_tooltips and self._bad_path_rows:
            max_data_col = self.grid.GetNumberCols() - 1
            if self.show_detailed_queue_packets:
                max_data_col -= 2
            db_name = os.path.basename(self.frame.db_path)
            for row in self._bad_path_rows:
                elem_path = self._bad_path_elem_path_by_row[row]
                tooltip = f'{elem_path} not in {db_name}'
                for col in range(1, max_data_col+1):
                    self.grid.SetCellToolTip(row, col, tooltip)

        for elem_path, vals in self._ranges.items():
            if elem_path in self.scalar_elem_paths:
                row = self.scalar_row_by_elem_path.get(elem_path)
                if row is None:
                    continue

                selected_clock = self.frame.playback_bar.clock_combobox.GetValue()
                clock_period = int(self.frame.playback_bar.clock_periods[selected_clock])
                current_cycle = self.frame.playback_bar.GetCurrentCycle()
                detailed_pkt_col = self.num_samples_before + self.num_samples_after + 3 if self.show_detailed_queue_packets else -1

                for i, value in enumerate(vals['DataVals']):
                    if value is None:
                        continue

                    time_val = vals['TimeVals'][i]
                    time_cycle = int(time_val) // clock_period
                    text = self.__ScalarValueToString(value)

                    auto_colorize_column = self.frame.data_retriever.GetAutoColorizeColumn(elem_path)
                    auto_colorize_key = None
                    if isinstance(value, (list, tuple)) and auto_colorize_column is not None:
                        for key, keyval in value:
                            if key == auto_colorize_column:
                                auto_colorize_key = keyval
                                break

                    if auto_colorize_key is None:
                        auto_colorize_key = text if text else str(value)

                    auto_label = self.frame.widget_renderer.GetAutoTag(auto_colorize_key)
                    auto_color = self.frame.widget_renderer.GetAutoColor(auto_colorize_key)

                    for col in range(self.grid.GetNumberCols()):
                        if not self.grid.IsColShown(col):
                            break
                        if self._sample_col_by_time.get(time_val) == col:
                            self.grid.SetCellValue(row, col, auto_label)
                            self.grid.SetCellBackgroundColour(row, col, auto_color)
                            if self.enable_tooltips:
                                self.grid.SetCellToolTip(row, col, text)
                            break

                    if detailed_pkt_col != -1 and time_cycle == current_cycle:
                        self.grid.SetCellValue(row, detailed_pkt_col, text)
                        self.grid.SetCellAlignment(row, detailed_pkt_col, wx.ALIGN_CENTER_VERTICAL)
                        self.grid.SetCellBackgroundColour(row, detailed_pkt_col, auto_color)
                        if self.enable_tooltips:
                            self.grid.SetCellToolTip(row, detailed_pkt_col, text)
                continue

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
            col = self.num_samples_before + self.num_samples_after + 3

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

                if not parts:
                    # This is a scalar value, not a struct
                    return ' ' + label

                return ' ' + ' '.join(parts)

            labels = [self.grid.GetCellValue(row,col).strip() for row in range(self.grid.GetNumberRows())]
            labels = [label.replace('\t', ' ') for label in labels]
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

        for row in range(self.grid.GetNumberRows()):
            for col in range(1, self.grid.GetNumberCols()):
                if self.grid.GetCellValue(row, col).strip() == '' and self.grid.GetCellBackgroundColour(row, col) == (255, 255, 255):
                    self.grid.SetCellBackgroundColour(row, col, (240,240,240))

        self.grid.AutoSize()
        if self.minimize_grid_cells:
            self.__SetMinimizedRowHeights()
        self.Layout()
        self.Update()
        self.Refresh()

    def __RerouteUnpackedDataToRasterizer(self, time_val, elem_path, bin_idx, annos):
        key = (elem_path, bin_idx)
        if key in self.rasterizers:
            self.rasterizers[key].Draw(elem_path, bin_idx, time_val, annos)

    def __SetMinimizedRowHeights(self):
        dc = wx.ScreenDC()
        dc.SetFont(wx.Font(8, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL))
        _, height = dc.GetTextExtent('Ag')
        self.grid.SetRowMinimalAcceptableHeight(height)
        self.grid.SetDefaultRowSize(height, True)
        for row in range(self.grid.GetNumberRows()):
            self.grid.SetRowMinimalHeight(row, height)
            self.grid.SetRowSize(row, height)

    def __SetElementCaptions(self, col):
        if col == 0:
            self.rasterizers = {}

        font_size = 8 if self.minimize_grid_cells else 10
        font = wx.Font(font_size, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
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

        max_num_chars = max([len(caption) for caption in captions], default=0)

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
            row = row_offset + i
            self.grid.SetCellValue(row, col, caption)

            if segment['kind'] == 'bin':
                bin_idx = segment['bin']
                self.rasterizers[(elem_path, bin_idx)] = Rasterizer(
                    self.frame, self.grid, self, elem_path, bin_idx, row, detailed_pkt_col)
            elif segment['kind'] == 'scalar':
                self.scalar_row_by_elem_path[elem_path] = row

        return len(layout)

    def __BuildRowLayout(self, elem_path):
        if elem_path in self.scalar_elem_paths:
            return [{'kind': 'scalar'}]

        collection_id = self.frame.simhier.GetCollectionID(elem_path)
        num_bins = self.frame.simhier.GetCapacityByCollectionID(collection_id)
        max_size = self.queue_max_sizes_by_collection_id.get(collection_id, 0)

        # hide_empty_rows caps the row count at the queue's overall max size ever
        # reached; otherwise every bin up to the full capacity is shown.
        upper = max_size if self.hide_empty_rows else num_bins
        if upper == 0:
            return [{'kind': 'no_data'}]

        return [{'kind': 'bin', 'bin': bin_idx} for bin_idx in range(upper - 1, -1, -1)]

    def __IsSegmentHidden(self, elem_path, segment):
        if segment['kind'] != 'bin':
            return False
        segment_key = self.__SegmentElemPathTooltip(elem_path, segment)
        return self.caption_mgr.GetCustomCaption(segment_key) == '<hide>'

    def __FormatSegmentCaption(self, elem_path, segment, elem_paths=None):
        if elem_paths is None:
            elem_paths = self._caption_elem_paths

        segment_key = self.__SegmentElemPathTooltip(elem_path, segment)
        custom_caption = self.caption_mgr.GetCustomCaption(segment_key)
        if custom_caption is not None:
            return custom_caption

        if segment['kind'] == 'scalar':
            return self.caption_mgr.GetCaptionPrefix(elem_path)
        if segment['kind'] == 'no_data':
            return '{}(no data)'.format(self.caption_mgr.GetCaptionPrefix(elem_path))
        if segment['kind'] == 'bad_path':
            return self.caption_mgr.GetCaptionPrefix(elem_path)
        return self.caption_mgr.GetCaption(elem_path, segment['bin'])

    def __GetDisplayedElemPaths(self):
        regexes = self.caption_mgr.GetElemPathRegexReplacements(as_list=True)
        displayed_elems = [r[0] for r in regexes]
        return displayed_elems

    def __GetCaptionColumnTooltip(self, elem_path, segment, caption):
        full_tooltip = self.__SegmentElemPathTooltip(elem_path, segment)
        if caption.rstrip() == full_tooltip:
            return None

        if self.enable_tooltips:
            return full_tooltip
        return None

    def __SegmentElemPathTooltip(self, elem_path, segment):
        if segment['kind'] == 'scalar':
            return elem_path
        if segment['kind'] in ('no_data', 'bad_path'):
            return elem_path
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
        self.custom_captions = {}

    def SetCustomCaption(self, segment_key, caption):
        self.custom_captions[segment_key] = caption

    def RemoveCustomCaption(self, segment_key):
        self.custom_captions.pop(segment_key, None)

    def RemoveContainerCustomCaptions(self, elem_path):
        self.RemoveCustomCaption(elem_path)
        keys_to_remove = [
            k for k in self.custom_captions
            if k == elem_path or k.startswith(f"{elem_path}[")
        ]
        for k in keys_to_remove:
            self.custom_captions.pop(k, None)

    def GetCustomCaption(self, segment_key):
        return self.custom_captions.get(segment_key)

    def GetCustomCaptions(self):
        return copy.deepcopy(self.custom_captions)

    def SetCustomCaptions(self, custom_captions):
        if isinstance(custom_captions, list):
            self.custom_captions = dict(custom_captions)
        elif isinstance(custom_captions, dict):
            self.custom_captions = copy.deepcopy(custom_captions)
        else:
            self.custom_captions = {}

    def ClearCustomCaptions(self):
        self.custom_captions = {}

    def SetElemPathRegexReplacement(self, elem_path_regex, regex_replacement):
        self.regex_replacements_by_elem_path_regex[elem_path_regex] = regex_replacement

    def SetElemPathRegexReplacements(self, regex_replacements_by_elem_path_regex):
        if isinstance(regex_replacements_by_elem_path_regex, list):
            regex_replacements_by_elem_path_regex = OrderedDict({x:x for x in regex_replacements_by_elem_path_regex})
        elif not isinstance(regex_replacements_by_elem_path_regex, OrderedDict):
            raise TypeError('Must be a list or an OrderedDict, not a regular unordered python dict.')

        self.regex_replacements_by_elem_path_regex = copy.deepcopy(regex_replacements_by_elem_path_regex)

    def GetElemPathRegexReplacements(self, as_list=False):
        d = copy.deepcopy(self.regex_replacements_by_elem_path_regex)
        if as_list:
            return list(d.items())

        return d

    def GetCaption(self, elem_path, bin_idx):
        is_scalar = elem_path in self.simhier.GetScalarStatsElemPaths() or elem_path in self.simhier.GetScalarStructsElemPaths()

        prefix = self.GetCaptionPrefix(elem_path)
        if is_scalar:
            return prefix
        return f'{prefix}[{bin_idx}]'

    def GetCaptionPrefix(self, elem_path):
        # Check custom container-level caption first
        custom_prefix = self.GetCustomCaption(elem_path)
        if custom_prefix is not None:
            return custom_prefix

        # An elem_path that was added as-is (not a regex covering other paths)
        # registers itself as its own key/replacement. That exact self-match
        # must win before scanning other entries' regexes below, otherwise an
        # unrelated elem_path that happens to be a literal prefix of this one
        # (e.g. "top.sqb" vs "top.sqb_age_ordered") can match first, since "."
        # in a regex matches any character, not just a literal dot.
        if elem_path in self.regex_replacements_by_elem_path_regex:
            replacements = self.regex_replacements_by_elem_path_regex[elem_path]
            if replacements == elem_path:
                return elem_path
            return replacements

        for regex, replacements in self.regex_replacements_by_elem_path_regex.items():
            if re.compile(regex).match(elem_path):
                return re.sub(regex, replacements, elem_path)

        return elem_path

    def GetAllMatchingElemPaths(self):
        # The display order follows the regex OrderedDict, but scalar leaves and
        # queue containers are both valid collectables. We therefore match against
        # the full item set rather than only container paths.
        item_elem_paths = self.simhier.GetItemElemPaths()

        elem_paths = []
        seen = set()
        for regex, _ in self.regex_replacements_by_elem_path_regex.items():
            if regex in item_elem_paths:
                if regex not in seen:
                    elem_paths.append(regex)
                    seen.add(regex)
                continue

            compiled = re.compile(regex)
            matched_any = False
            for elem_path in item_elem_paths:
                if elem_path in seen:
                    continue

                if compiled.match(elem_path):
                    elem_paths.append(elem_path)
                    seen.add(elem_path)
                    matched_any = True

            # Not a known elem path and doesn't match anything in the hierarchy either;
            # show it as-is (e.g. a bad/unknown path typed in manually) instead of
            # silently dropping it.
            if not matched_any and regex not in seen:
                elem_paths.append(regex)
                seen.add(regex)

        return elem_paths

    def GetRegex(self, elem_path):
        for regex, _ in self.regex_replacements_by_elem_path_regex.items():
            if re.compile(regex).match(elem_path):
                return regex

        return None

    def GetMatchingElemPaths(self, regex):
        elem_paths = []
        for elem_path in self.simhier.GetItemElemPaths():
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
        selected_clock = self.frame.playback_bar.clock_combobox.GetValue()
        clock_period = int(self.frame.playback_bar.clock_periods[selected_clock])
        time_cycle = int(time_val) // clock_period

        for col in range(self.grid.GetNumberCols()):
            if not self.grid.IsColShown(col):
                break

            if self.widget._sample_col_by_time.get(time_val) == col:
                self.grid.SetCellValue(self.row, col, auto_label)
                self.grid.SetCellBackgroundColour(self.row, col, auto_color)
                if self.widget.enable_tooltips:
                    self.grid.SetCellToolTip(self.row, col, stringized_tooltip)

                border_width = 1 if show_border else self.grid.GetCellBorderWidth(self.row, col)
                border_side = wx.ALL if show_border else self.grid.GetCellBorderSide(self.row, col)
                self.grid.SetCellBorder(self.row, col, border_width, border_side)
                break

        if self.detailed_pkt_col != -1 and time_cycle == self.frame.playback_bar.GetCurrentCycle():
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
