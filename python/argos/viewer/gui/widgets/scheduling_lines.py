import wx, copy, re
from collections import OrderedDict
from viewer.gui.view_settings import DirtyReasons
from viewer.gui.widgets.grid import Grid
from viewer.gui.dialogs.scheduling_lines_customization import SchedulingLinesCustomizationDlg
from functools import partial

class SchedulingLinesWidget(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent)
        self.frame = frame
        self.num_ticks_before = 5
        self.num_ticks_after = 25
        self.show_detailed_queue_packets = True
        self.enable_tooltips = False
        self.caption_mgr = CaptionManager(frame.simhier)
        self.tracked_annos = {}

        self.grid = None
        self.info = None
        self.gear_btn = None
        self.rasterizers = {}
        self.grid = None

        cursor = frame.db.cursor()
        cmd = 'SELECT CID,MaxSize FROM QueueMaxSizes'

        cursor.execute(cmd)
        self.queue_max_sizes_by_collection_id = {}
        for collection_id,max_size in cursor.fetchall():
            self.queue_max_sizes_by_collection_id[collection_id] = max_size

        self.__ShowUsageInfo()

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

            todo_captions = self.__GetCaptionsForElement(elem_path)
            for caption in todo_captions:
                if caption in existing_captions:
                    msg = 'Adding this to the Scheduling Lines widget would result in a duplicate caption(s). '
                    msg += 'You need to open the widget settings dialog and adjust the regexes.'
                    return msg, 'Duplicate Caption'

        return None

    def AddElement(self, elem_path):
        self.__AddElement(elem_path)
        self.__Refresh()

        if not self.gear_btn:
            self.gear_btn = self.frame.CreateSettingsButton(self)
            self.gear_btn.Bind(wx.EVT_BUTTON, self.__EditWidget)
            self.gear_btn.SetToolTip('Edit widget settings')

        self.frame.view_settings.SetDirty(reason=DirtyReasons.SchedulingLinesWidgetChanged)

    def UpdateWidgetData(self, regenerate_grid=False):
        if not self.grid and not self.info and not self.gear_btn:
            return

        self.__Refresh(regenerate_grid)

    def GetCurrentViewSettings(self):
        settings = {}
        settings['regexes'] = self.caption_mgr.GetElemPathRegexReplacements(as_list=True)
        settings['num_ticks_before'] = self.num_ticks_before
        settings['num_ticks_after'] = self.num_ticks_after
        settings['show_detailed_queue_packets'] = self.show_detailed_queue_packets
        settings['enable_tooltips'] = self.enable_tooltips
        settings['tracked_annos'] = copy.deepcopy(self.tracked_annos)
        return settings
    
    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        dirty = self.caption_mgr.GetElemPathRegexReplacements(as_list=True) != settings['regexes'] or \
                self.num_ticks_before != settings['num_ticks_before'] or \
                self.num_ticks_after != settings['num_ticks_after'] or \
                self.show_detailed_queue_packets != settings['show_detailed_queue_packets'] or \
                self.enable_tooltips != settings.get('enable_tooltips', False) or \
                self.tracked_annos != settings['tracked_annos']

        if not dirty:
            return

        self.caption_mgr.SetElemPathRegexReplacements(settings['regexes'])
        self.num_ticks_before = settings['num_ticks_before']
        self.num_ticks_after = settings['num_ticks_after']
        self.show_detailed_queue_packets = settings['show_detailed_queue_packets']
        self.enable_tooltips = settings.get('enable_tooltips', False)
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
        regex_replacement = GetHeadsUpCamelCaseQueueName(elem_path)
        self.caption_mgr.SetElemPathRegexReplacement(elem_path, regex_replacement)

    def __Refresh(self, new_grid=True):
        if len(self.caption_mgr.GetAllMatchingElemPaths()) > 0:
            if self.info:
                self.info.Hide()

            # Preserve the scrollbar position across the grid regeneration below.
            # The old grid is destroyed and a brand-new one is created, which
            # would otherwise reset the scroll position back to the top.
            saved_view_start = self.grid.GetViewStart() if self.grid else None

            self.SetBackgroundColour('white')
            self.__RegenerateSchedulingLinesGrid(new_grid)
            self.__RasterizeAllCells()

            # Restore the scroll position after the new grid has been laid out
            # and auto-sized (which establishes its scroll range).
            if saved_view_start is not None:
                #wx.CallAfter(self.grid.Scroll, saved_view_start[0], saved_view_start[1])
                self.grid.Scroll(saved_view_start[0], saved_view_start[1])

            if not self.gear_btn:
                self.gear_btn = self.frame.CreateSettingsButton(self)
                self.gear_btn.Bind(wx.EVT_BUTTON, self.__EditWidget)
                self.gear_btn.SetToolTip('Edit widget settings')

    def __ShowUsageInfo(self):
        if self.gear_btn:
            self.gear_btn.Destroy()
            self.gear_btn = None

        if self.info:
            self.info.Destroy()
            self.info = None

        if self.grid:
            self.grid.Destroy()
            self.grid = None

        if self.GetSizer():
            self.GetSizer().Clear()

        self.SetBackgroundColour('light gray')

        self.info = wx.StaticText(self, label='Drag queues from the Queues tree to create scheduling lines.')#, size=(600,18))
        self.info.SetFont(wx.Font(14, wx.FONTFAMILY_DEFAULT, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL))

        vsizer = wx.BoxSizer(wx.VERTICAL)
        vsizer.AddStretchSpacer()
        vsizer.Add(self.info, 1, wx.ALL | wx.CENTER | wx.EXPAND, 5)
        vsizer.AddStretchSpacer()

        hsizer = wx.BoxSizer(wx.HORIZONTAL)
        hsizer.AddStretchSpacer()
        hsizer.Add(vsizer, 0, wx.CENTER | wx.EXPAND)
        hsizer.AddStretchSpacer()

        self.SetSizer(hsizer)
        self.Layout()

    def __RegenerateSchedulingLinesGrid(self, new_grid):
        if self.grid:
            self.grid.Destroy()
            self.grid = None

        if self.info:
            self.info.Destroy()
            self.info = None

        if self.gear_btn:
            self.gear_btn.Destroy()
            self.gear_btn = None

        sizer = self.GetSizer()
        if sizer:
            sizer.Clear()

        sizer = wx.BoxSizer(wx.VERTICAL)

        # The number of rows can be calculated as:
        #  1. Go through each element path we are tracking (each is a queue)
        #     a. For each element path, get the number of bins in each queue (A)
        #     b. For each element path, note the maximum number of elements seen in the simulation (B)
        #     c. If A>B, then the number of rows is B+1. Otherwise, the number of rows is A. (C)
        #  >>> The required number of rows is the sum of all (C) values in the (1) loop.

        num_rows = 0
        for elem_path in self.caption_mgr.GetAllMatchingElemPaths():
            collection_id = self.frame.simhier.GetCollectionID(elem_path)
            num_bins = self.frame.simhier.GetCapacityByCollectionID(collection_id) # (A)
            max_size = self.queue_max_sizes_by_collection_id[collection_id]        # (B)
            elem_num_rows = max_size + 1 if max_size < num_bins else num_bins      # (C)
            num_rows += elem_num_rows

        # The number of columns can be calculated as:
        #  1. Start with the sum of self.num_ticks_before and self.num_ticks_after (A)
        #  2. Add 1 to (A) to account for the element paths column (captions)
        #  3. If self.show_detailed_queue_packets is True, add 3 to (A) to account for:
        #     a. A column to add a separator between the summary and detailed sections
        #     b. A column to duplicate the element paths column (captions)
        #     c. A column to show the stringified packet data e.g. "IntVal(4) DoubleVal(3.14)"

        num_cols = self.num_ticks_before + self.num_ticks_after + 1
        if self.show_detailed_queue_packets:
            num_cols += 3

        # Create 8-point monospace font for the grid cells
        font8 = wx.Font(8, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)

        # Create 10-point font for the grid column labels
        font10 = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)

        if new_grid or self.grid is None:
            self.grid = Grid(self, self.frame, num_rows, num_cols, cell_font=font8, label_font=font10, cell_selection_allowed=False)
        self.grid.GetGridWindow().Bind(wx.EVT_MOTION, self.__OnGridMouseMotion)
        self.grid.GetGridWindow().Bind(wx.EVT_RIGHT_DOWN, self.__OnGridRightClick)
        self.grid.EnableGridLines(False)
        self.grid.SetLabelBackgroundColour('white')

        self.gear_btn = self.frame.CreateSettingsButton(self)
        self.gear_btn.Bind(wx.EVT_BUTTON, self.__EditWidget)
        self.gear_btn.SetToolTip('Edit widget settings')

        current_tick = self.frame.widget_renderer.tick
        col_labels = []
        time_vals = self.frame.data_retriever.GetAllTimeVals()
        time_vals = {float(val) for val in time_vals}
        for col in range(1, self.num_ticks_before + self.num_ticks_after + 1):
            tick = current_tick - self.num_ticks_before + col - 1
            if float(tick) in time_vals:
                self.grid.SetColLabelValue(col, str(tick))
                col_labels.append(str(tick))
            else:
                self.grid.SetColLabelValue(col, '')

        # Use a DC to get the length of the longest col label
        dc = wx.ScreenDC()
        dc.SetFont(self.grid.GetLabelFont())
        max_col_label_len = max([dc.GetTextExtent(col_label)[0] for col_label in col_labels]) if col_labels else 0
        self.grid.SetColLabelSize(max_col_label_len + 4)

        self.grid.SetColLabelValue(0, '')
        self.grid.SetColLabelTextOrientation(wx.VERTICAL)
        self.grid.HideRowLabels()

        sizer.Add(self.grid, 0, wx.EXPAND, 5)
        self.SetSizer(sizer)

        self.grid.ClearGrid()

        # Draw a thick black line to mark the current time
        for row in range(num_rows):
            self.grid.SetCellBorder(row, self.num_ticks_before, 1, wx.RIGHT)
            self.grid.SetCellBorder(row, self.num_ticks_before + 1, 1, wx.LEFT)

        self.__SetElementCaptions(0)
        if self.show_detailed_queue_packets:
            self.__SetElementCaptions(self.num_ticks_before + self.num_ticks_after + 2)

            # Clear the column labels for the detailed queue packets section
            for i in range(self.num_ticks_before + self.num_ticks_after + 1, self.grid.GetNumberCols()):
                self.grid.SetColLabelValue(i, '')

    def __RasterizeAllCells(self):
        start_time = self.frame.widget_renderer.tick - self.num_ticks_before
        end_time = self.frame.widget_renderer.tick + self.num_ticks_after
        elem_paths = self.caption_mgr.GetAllMatchingElemPaths()

        ranges = self.frame.data_retriever.UnpackRange(start_time, end_time, elem_paths)
        for elem_path, vals in ranges.items():
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
            col = self.num_ticks_before + self.num_ticks_after + 3
            labels = [self.grid.GetCellValue(row,col) for row in range(self.grid.GetNumberRows())]
            max_num_chars = max([len(label) for label in labels])
            for row in range(self.grid.GetNumberRows()):
                label = self.grid.GetCellValue(row, col).strip()
                label = label.strip() + ' '*(max_num_chars - len(label))
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
            elem_captions = self.__GetCaptionsForElement(elem_path)
            for caption in elem_captions:
                match = re.match(r'(.+)\[(\d+)(?:-(\d+))?\]', caption)
                assert match
                bracket = match.group(2)
                if match.group(3) is not None:
                    bracket += '-' + match.group(3)
                tooltip = elem_path + '[' + bracket + ']'

                captions.append(caption)
                self.grid.SetCellToolTip(row, col, tooltip)
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
        collection_id = self.frame.simhier.GetCollectionID(elem_path)
        num_bins = self.frame.simhier.GetCapacityByCollectionID(collection_id)
        max_size = self.queue_max_sizes_by_collection_id[collection_id]

        if max_size < num_bins:
            caption_prefix = self.caption_mgr.GetCaptionPrefix(elem_path)
            caption = '{}[{}-{}]'.format(caption_prefix, max_size-1, num_bins-1)
            caption += ' '*(max_num_chars - len(caption))
            self.grid.SetCellValue(row_offset, col, caption)

            for i in range(1, max_size):
                bin_idx = max_size - i - 1
                caption = self.caption_mgr.GetCaption(elem_path, bin_idx)
                caption += ' '*(max_num_chars - len(caption))
                self.grid.SetCellValue(row_offset + i, col, caption)
                self.rasterizers[(elem_path, bin_idx)] = Rasterizer(self.frame, self.grid, self, elem_path, bin_idx, row_offset + i, detailed_pkt_col)

            return max_size + 1
        else:
            for i in range(num_bins):
                bin_idx = num_bins - i - 1
                caption = self.caption_mgr.GetCaption(elem_path, bin_idx)
                caption += ' '*(max_num_chars - len(caption))
                self.grid.SetCellValue(row_offset + i, col, caption)
                self.rasterizers[(elem_path, bin_idx)] = Rasterizer(self.frame, self.grid, self, elem_path, bin_idx, row_offset + i, detailed_pkt_col)

            return num_bins

    def __GetCaptionsForElement(self, elem_path):
        collection_id = self.frame.simhier.GetCollectionID(elem_path)
        num_bins = self.frame.simhier.GetCapacityByCollectionID(collection_id)
        max_size = self.queue_max_sizes_by_collection_id[collection_id]

        captions = []
        if max_size < num_bins:
            caption_prefix = self.caption_mgr.GetCaptionPrefix(elem_path)
            captions.append('{}[{}-{}]'.format(caption_prefix, max_size-1, num_bins-1))

            for i in range(1, max_size):
                bin_idx = max_size - i - 1
                caption = self.caption_mgr.GetCaption(elem_path, bin_idx)
                captions.append(caption)
        else:
            for i in range(num_bins):
                bin_idx = num_bins - i - 1
                caption = self.caption_mgr.GetCaption(elem_path, bin_idx)
                captions.append(caption)

        return captions
    
    def __OnGridMouseMotion(self, evt):
        if not self.enable_tooltips:
            self.grid.UnsetToolTip()
            return

        x, y = self.grid.CalcUnscrolledPosition(evt.GetX(), evt.GetY())
        row, col = self.grid.XYToCell(x, y)
        tooltip = self.grid.GetCellToolTip(row, col)

        if tooltip:
            self.grid.SetToolTip(tooltip)
        else:
            self.grid.UnsetToolTip()

    def __OnGridRightClick(self, evt):
        x, y = self.grid.CalcUnscrolledPosition(evt.GetX(), evt.GetY())
        row, col = self.grid.XYToCell(x, y)

        if col == 0:
            return

        if self.show_detailed_queue_packets and col > self.num_ticks_before + self.num_ticks_after:
            return

        # Extract the element path from the row label's tooltip e.g. "top.cpu.core0.rob.stats.num_insts_retired"
        elem_path = self.grid.GetCellToolTip(row, 0)

        # Extract the caption for this row e.g. "NumInstsRetired[3]"
        caption = self.grid.GetCellValue(row, 0)

        # Extract the bin index from the caption
        match = re.match(r'(.+)\[(\d+)\]', caption)

        # Don't do anything when we right-click on a cell for e.g. InstQueue[29-31]
        # as those are just "filler" rows to indicate the full queue capacity (32).
        # There is no data here, so there is no tooltip.
        if not match:
            return

        bin_idx = match.group(2)

        # Remove the [bin_idx] suffix from elem_path
        elem_path = elem_path.replace('[{}]'.format(bin_idx), '')

        # Get the tick for the cell we right-clicked
        cell_tick = float(self.grid.GetColLabelValue(col))

        auto_colorize_column = self.frame.data_retriever.GetAutoColorizeColumn(elem_path)
        unpacked = self.frame.data_retriever.Unpack(elem_path, cell_tick)
        data_vals = unpacked['DataVals'][0]
        if data_vals is None or int(bin_idx) >= len(data_vals):
            return

        bin_data = data_vals[int(bin_idx)]
        if auto_colorize_column not in bin_data:
            return

        auto_colorize_value = bin_data[auto_colorize_column]
        menu_anno = '{}({})'.format(auto_colorize_column, auto_colorize_value)

        menu = wx.Menu()

        if menu_anno not in {'{}({})'.format(k,v) for k,v in self.tracked_annos.items()}:
            opt = menu.Append(wx.ID_ANY, 'Highlight cells with annotation "{}"'.format(menu_anno))
            self.grid.Bind(wx.EVT_MENU, partial(self.__HighlightCellsWithTag, key=auto_colorize_column, value=auto_colorize_value, highlight=True), opt)
        else:
            opt = menu.Append(wx.ID_ANY, 'Unhighlight cells with annotation "{}"'.format(menu_anno))
            self.grid.Bind(wx.EVT_MENU, partial(self.__HighlightCellsWithTag, key=auto_colorize_column, value=auto_colorize_value, highlight=False), opt)

        #opt = menu.Append(wx.ID_ANY, 'Go to next cycle where different')
        #self.grid.Bind(wx.EVT_MENU, partial(self.__GoToNextCycleWhereDifferent, elem_path=elem_path, bin_idx=bin_idx), opt)

        #opt = menu.Append(wx.ID_ANY, 'Go to previous cycle where different')
        #self.grid.Bind(wx.EVT_MENU, partial(self.__GoToPrevCycleWhereDifferent, elem_path=elem_path, bin_idx=bin_idx), opt)

        self.grid.PopupMenu(menu)

    def __HighlightCellsWithTag(self, evt, key, value, highlight):
        if highlight:
            self.tracked_annos[key] = value
            dirty = True
        else:
            if key in self.tracked_annos:
                del self.tracked_annos[key]
                dirty = True
            else:
                dirty = False

        if dirty:
            self.frame.view_settings.SetDirty(reason=DirtyReasons.TrackedPacketChanged)

        self.UpdateWidgetData(True)

    def __GoToNextCycleWhereDifferent(self, evt, elem_path, bin_idx):
        print ('TODO: Go to next cycle where different')

    def __GoToPrevCycleWhereDifferent(self, evt, elem_path, bin_idx):
        print ('TODO: Go to previous cycle where different')

    def __EditWidget(self, evt):
        dlg = SchedulingLinesCustomizationDlg(
            self, self.caption_mgr, self.num_ticks_before, self.num_ticks_after,
            self.show_detailed_queue_packets, self.enable_tooltips,
        )
        result = dlg.ShowModal()
        dlg.Destroy()

        if result == wx.ID_OK:
            self.ApplyViewSettings({'regexes': dlg.GetElementPathCaptionRegexes(as_list=True),
                                    'num_ticks_before': dlg.GetNumTicksBefore(),
                                    'num_ticks_after': dlg.GetNumTicksAfter(),
                                    'show_detailed_queue_packets': dlg.ShowDetailedQueuePackets(),
                                    'enable_tooltips': dlg.EnableTooltips(),
                                    'tracked_annos': copy.deepcopy(self.tracked_annos)})

class CaptionManager:
    def __init__(self, simhier):
        self.simhier = simhier
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

    def GetCaption(self, elem_path, bin_idx):
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
                return self.GetCaptionPrefix(elem_path) + '[{}]'.format(bin_idx)

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

        return GetHeadsUpCamelCaseQueueName(elem_path) + '[{}]'.format(bin_idx)
    
    def GetCaptionPrefix(self, elem_path):
        for regex, replacements in self.regex_replacements_by_elem_path_regex.items():
            if regex == elem_path:
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

def GetHeadsUpCamelCaseQueueName(elem_path):
    parts = elem_path.split('.')
    queue_name = parts[-1]
    parts = queue_name.split('_')

    for i,part in enumerate(parts):
        if len(part) == 1:
            part = part.upper()
        else:
            part = part[0].upper() + part[1:]

        parts[i] = part

    return ''.join(parts)

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
        for k,v in annos:
            anno.append('{}({})'.format(k,v))

        stringized_anno = ' '.join(anno)
        stringized_tooltip = '\n'.join(anno)

        tracked_annos = self.widget.tracked_annos
        show_border = auto_colorize_column in tracked_annos and tracked_annos[auto_colorize_column] == auto_colorize_key

        for col in range(self.grid.GetNumberCols()):
            if not self.grid.IsColShown(col):
                break

            col_label = self.grid.GetColLabelValue(col)
            try:
                col_label = float(col_label)
            except:
                continue

            if col_label == float(time_val):
                self.grid.SetCellValue(self.row, col, auto_label)
                self.grid.SetCellBackgroundColour(self.row, col, auto_color)
                self.grid.SetCellToolTip(self.row, col, stringized_tooltip)

                border_width = 1 if show_border else self.grid.GetCellBorderWidth(self.row, col)
                border_side = wx.ALL if show_border else self.grid.GetCellBorderSide(self.row, col)
                self.grid.SetCellBorder(self.row, col, border_width, border_side)
                break

        if self.detailed_pkt_col != -1 and time_val == self.frame.widget_renderer.tick:
            self.grid.SetCellValue(self.row, self.detailed_pkt_col, stringized_anno)
            self.grid.SetCellBackgroundColour(self.row, self.detailed_pkt_col, auto_color)
            self.grid.SetCellToolTip(self.row, self.detailed_pkt_col, stringized_tooltip)
            if show_border:
                self.grid.SetCellBorder(self.row, self.detailed_pkt_col, 1, wx.ALL)
