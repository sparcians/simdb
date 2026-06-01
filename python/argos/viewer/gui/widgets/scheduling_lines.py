import wx, copy, re
from collections import OrderedDict
from viewer.gui.view_settings import DirtyReasons
from viewer.gui.widgets.grid import Grid
from functools import partial

class SchedulingLinesWidget(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent)
        self.frame = frame
        self.num_ticks_before = 5
        self.num_ticks_after = 25
        self.show_detailed_queue_packets = True
        self.caption_mgr = CaptionManager(frame.simhier)
        self.tracked_annos = {}

        self.grid = None
        self.info = None
        self.gear_btn = None
        self.rasterizers = {}
        self.grid = None

        self.info = wx.StaticText(self, label='Drag queues from the NavTree to create scheduling lines.', size=(600,18))
        self.info.SetFont(wx.Font(14, wx.FONTFAMILY_DEFAULT, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL))

        vsizer = wx.BoxSizer(wx.VERTICAL)
        vsizer.AddStretchSpacer()
        vsizer.Add(self.info, 1, wx.ALL | wx.CENTER | wx.EXPAND, 5)
        vsizer.AddStretchSpacer()

        cursor = frame.db.cursor()
        cmd = 'SELECT SerializationCID,MaxSize FROM QueueMaxSizes'

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
            self.gear_btn = wx.BitmapButton(self, bitmap=self.frame.CreateResourceBitmap('gear.png'), pos=(5,5))
            self.gear_btn.Bind(wx.EVT_BUTTON, self.__EditWidget)
            self.gear_btn.SetToolTip('Edit widget settings')

        self.frame.view_settings.SetDirty(reason=DirtyReasons.SchedulingLinesWidgetChanged)

    def UpdateWidgetData(self):
        if not self.grid and not self.info and not self.gear_btn:
            return

        self.__Refresh()

    def GetCurrentViewSettings(self):
        settings = {}
        settings['regexes'] = self.caption_mgr.GetElemPathRegexReplacements(as_list=True)
        settings['num_ticks_before'] = self.num_ticks_before
        settings['num_ticks_after'] = self.num_ticks_after
        settings['show_detailed_queue_packets'] = self.show_detailed_queue_packets
        settings['tracked_annos'] = copy.deepcopy(self.tracked_annos)
        return settings
    
    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        dirty = self.caption_mgr.GetElemPathRegexReplacements(as_list=True) != settings['regexes'] or \
                self.num_ticks_before != settings['num_ticks_before'] or \
                self.num_ticks_after != settings['num_ticks_after'] or \
                self.show_detailed_queue_packets != settings['show_detailed_queue_packets'] or \
                self.tracked_annos != settings['tracked_annos']

        if not dirty:
            return

        self.caption_mgr.SetElemPathRegexReplacements(settings['regexes'])
        self.num_ticks_before = settings['num_ticks_before']
        self.num_ticks_after = settings['num_ticks_after']
        self.show_detailed_queue_packets = settings['show_detailed_queue_packets']
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

    def __Refresh(self):
        if len(self.caption_mgr.GetAllMatchingElemPaths()) > 0:
            if self.info:
                self.info.Hide()

            self.SetBackgroundColour('white')
            self.__RegenerateSchedulingLinesGrid()
            self.__RasterizeAllCells()

            if not self.gear_btn:
                self.gear_btn = wx.BitmapButton(self, bitmap=self.frame.CreateResourceBitmap('gear.png'), pos=(5,5))
                self.gear_btn.Bind(wx.EVT_BUTTON, self.__EditWidget)
                self.gear_btn.SetToolTip('Edit widget settings')
        else:
            self.__ShowUsageInfo()

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

        self.info = wx.StaticText(self, label='Drag queues from the NavTree to create scheduling lines.', size=(600,18))
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

    def __RegenerateSchedulingLinesGrid(self):
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

        self.grid = Grid(self, self.frame, num_rows, num_cols, cell_font=font8, label_font=font10, cell_selection_allowed=False)
        self.grid.GetGridWindow().Bind(wx.EVT_MOTION, self.__OnGridMouseMotion)
        self.grid.GetGridWindow().Bind(wx.EVT_RIGHT_DOWN, self.__OnGridRightClick)
        self.grid.EnableGridLines(False)
        self.grid.SetLabelBackgroundColour('white')

        self.gear_btn = wx.BitmapButton(self, bitmap=self.frame.CreateResourceBitmap('gear.png'), pos=(5,5))
        self.gear_btn.Bind(wx.EVT_BUTTON, self.__EditWidget)

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
        for elem_path in self.caption_mgr.GetAllMatchingElemPaths():
            start_time = self.frame.widget_renderer.tick - self.num_ticks_before
            end_time = self.frame.widget_renderer.tick + self.num_ticks_after

            vals = self.frame.data_retriever.Unpack(elem_path, (start_time, end_time))
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
        if data_vals is None:
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

        self.UpdateWidgetData()

    def __GoToNextCycleWhereDifferent(self, evt, elem_path, bin_idx):
        print ('TODO: Go to next cycle where different')

    def __GoToPrevCycleWhereDifferent(self, evt, elem_path, bin_idx):
        print ('TODO: Go to previous cycle where different')

    def __EditWidget(self, evt):
        dlg = SchedulingLinesCustomizationDialog(self, self.caption_mgr, self.num_ticks_before, self.num_ticks_after, self.show_detailed_queue_packets)
        result = dlg.ShowModal()
        dlg.Destroy()

        if result == wx.ID_OK:
            self.ApplyViewSettings({'regexes': dlg.GetElementPathCaptionRegexes(as_list=True),
                                    'num_ticks_before': dlg.GetNumTicksBefore(),
                                    'num_ticks_after': dlg.GetNumTicksAfter(),
                                    'show_detailed_queue_packets': dlg.ShowDetailedQueuePackets(),
                                    'tracked_annos': copy.deepcopy(self.tracked_annos)})

class SchedulingLinesCustomizationDialog(wx.Dialog):
    def __init__(self, parent, caption_mgr, num_ticks_before, num_ticks_after, show_detailed_queue_packets):
        super().__init__(parent, title="Customize Scheduling Lines")

        self.caption_mgr = copy.deepcopy(caption_mgr)
        self.show_detailed_queue_packets = show_detailed_queue_packets
        self.ok_btn = None

        self.move_up_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_GO_UP, wx.ART_BUTTON))
        self.move_up_btn.Bind(wx.EVT_BUTTON, self.__MoveSelectedElemUp)

        self.move_down_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_GO_DOWN, wx.ART_BUTTON))
        self.move_down_btn.Bind(wx.EVT_BUTTON, self.__MoveSelectedElemDown)

        self.remove_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_DELETE, wx.ART_BUTTON))
        self.remove_btn.Bind(wx.EVT_BUTTON, self.__RemoveSelectedElems)
        self.remove_btn.Disable()

        edit_btns_sizer = wx.BoxSizer(wx.VERTICAL)
        edit_btns_sizer.Add(self.move_up_btn, 0, wx.ALL, 5)
        edit_btns_sizer.Add(self.move_down_btn, 0, wx.ALL, 5)
        edit_btns_sizer.Add(self.remove_btn, 0, wx.ALL, 5)

        self.element_path_regexes_list_ctrl = wx.ListCtrl(self, style=wx.LC_REPORT)
        self.element_path_regexes_list_ctrl.InsertColumn(0, "Path Regex")
        self.element_path_regexes_list_ctrl.InsertColumn(1, "Caption")
        self.element_path_regexes_list_ctrl.Bind(wx.EVT_LIST_ITEM_SELECTED, self.__OnListCtrlItemSelected)
        self.element_path_regexes_list_ctrl.Bind(wx.EVT_LIST_ITEM_DESELECTED, self.__OnListCtrlItemSelected)
        self.element_path_regexes_list_ctrl.Bind(wx.EVT_LEFT_DCLICK, self.__OnListCtrlItemDClicked)
        self.element_path_regexes_list_ctrl.Bind(wx.EVT_LIST_END_LABEL_EDIT, self.__ValidateRegexSettings)

        element_path_caption_regexes = self.caption_mgr.GetElemPathRegexReplacements()
        idx = 0
        for path_regex, caption_replacements in element_path_caption_regexes.items():
            self.element_path_regexes_list_ctrl.InsertItem(idx, path_regex)
            self.element_path_regexes_list_ctrl.SetItem(idx, 1, caption_replacements)

            if len(element_path_caption_regexes) == 1:
                self.move_up_btn.Disable()
                self.move_down_btn.Disable()
            else:
                idx += 1

        hsizer = wx.BoxSizer(wx.HORIZONTAL)
        hsizer.Add(self.element_path_regexes_list_ctrl, 1, wx.ALL | wx.EXPAND, 5)
        hsizer.Add(edit_btns_sizer)

        self.ok_btn = wx.Button(self, wx.ID_OK)
        self.cancel_btn = wx.Button(self, wx.ID_CANCEL)

        exit_btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        exit_btn_sizer.Add(self.ok_btn, 0, wx.ALL | wx.EXPAND, 5)
        exit_btn_sizer.Add(self.cancel_btn, 0, wx.ALL | wx.EXPAND, 5)

        num_ticks_before_min_val = 1
        num_ticks_before_max_val = 25
        num_ticks_after_min_val = 5
        num_ticks_after_max_val = 100

        num_ticks_before_info_label = wx.StaticText(self, label='Cycles to show before current tick:')
        num_ticks_after_info_label = wx.StaticText(self,  label='Cycles to show after current tick:')

        self.num_ticks_before_value_label = wx.StaticText(self, label=str(num_ticks_before))
        self.num_ticks_after_value_label = wx.StaticText(self, label=str(num_ticks_after))

        num_ticks_before_min_val_text_label = wx.StaticText(self, label=str(num_ticks_before_min_val))
        num_ticks_before_max_val_text_label = wx.StaticText(self, label=str(num_ticks_before_max_val))
        num_ticks_after_min_val_text_label = wx.StaticText(self, label=str(num_ticks_after_min_val))
        num_ticks_after_max_val_text_label = wx.StaticText(self, label=str(num_ticks_after_max_val))

        self.num_ticks_before_slider = wx.Slider(self, minValue=num_ticks_before_min_val, maxValue=num_ticks_before_max_val, value=num_ticks_before)
        self.num_ticks_after_slider = wx.Slider(self, minValue=num_ticks_after_min_val, maxValue=num_ticks_after_max_val, value=num_ticks_after)

        self.num_ticks_before_slider.Bind(wx.EVT_SCROLL, self.__OnNumTicksBeforeSliderChanged)
        self.num_ticks_after_slider.Bind(wx.EVT_SCROLL, self.__OnNumTicksAfterSliderChanged)

        w,h = self.num_ticks_before_slider.GetSize()
        self.num_ticks_before_slider.SetMinSize((300, h))

        w,h = self.num_ticks_after_slider.GetSize()
        self.num_ticks_after_slider.SetMinSize((300, h))

        num_ticks_sizer = wx.FlexGridSizer(rows=2, cols=6, hgap=0, vgap=0)
        num_ticks_sizer.Add(num_ticks_before_info_label)
        num_ticks_sizer.Add(self.num_ticks_before_value_label, 0, wx.LEFT, 20)
        num_ticks_sizer.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.LEFT | wx.EXPAND, 20)
        num_ticks_sizer.Add(num_ticks_before_min_val_text_label, 0, wx.LEFT, 20)
        num_ticks_sizer.Add(self.num_ticks_before_slider, 0, wx.TOP | wx.EXPAND, -7)
        num_ticks_sizer.Add(num_ticks_before_max_val_text_label)

        num_ticks_sizer.Add(num_ticks_after_info_label)
        num_ticks_sizer.Add(self.num_ticks_after_value_label, 0, wx.LEFT, 20)
        num_ticks_sizer.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.LEFT | wx.EXPAND, 20)
        num_ticks_sizer.Add(num_ticks_after_min_val_text_label, 0, wx.LEFT, 20)
        num_ticks_sizer.Add(self.num_ticks_after_slider, 0, wx.TOP | wx.EXPAND, -7)
        num_ticks_sizer.Add(num_ticks_after_max_val_text_label)

        show_detailed_queue_packets_checkbox = wx.CheckBox(self, label='Show detailed queue packets')
        show_detailed_queue_packets_checkbox.SetValue(show_detailed_queue_packets)
        show_detailed_queue_packets_checkbox.Bind(wx.EVT_CHECKBOX, self.__OnShowDetailedQueuePacketsChanged)

        regex_example_label = wx.StaticText(self, label='Regex examples:')
        regex_example_text = wx.StaticText(self, label='top.cpu.core([0-9]+).rob.stats.num_insts_retired')
        regex_example_text2 = wx.StaticText(self, label='top.cpu.core0.rob.stats.ipc')

        caption_example_label = wx.StaticText(self, label='Caption examples:')
        caption_example_text = wx.StaticText(self, label='NumInstsRetired\\1')
        caption_example_text2 = wx.StaticText(self, label='IPC')

        font = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        regex_example_label.SetFont(font.Bold())
        regex_example_text.SetFont(font)
        regex_example_text2.SetFont(font)
        caption_example_label.SetFont(font.Bold())
        caption_example_text.SetFont(font)
        caption_example_text2.SetFont(font)
        self.element_path_regexes_list_ctrl.SetFont(font)

        dc = wx.ScreenDC()
        dc.SetFont(self.element_path_regexes_list_ctrl.GetFont())
        col0_width = max([dc.GetTextExtent(s)[0] for s in element_path_caption_regexes.keys()]) + 50
        col1_width = max([dc.GetTextExtent(s)[0] for s in element_path_caption_regexes.values()]) + 50

        col0_width = max(col0_width, dc.GetTextExtent(regex_example_text.GetLabel())[0])
        col0_width += dc.GetTextExtent('([0-9]+)')[0]

        hgap = col0_width - dc.GetTextExtent(regex_example_text.GetLabel())[0]
        example_sizer = wx.FlexGridSizer(rows=3, cols=2, hgap=hgap, vgap=0)
        example_sizer.Add(regex_example_label)
        example_sizer.Add(caption_example_label)
        example_sizer.Add(regex_example_text)
        example_sizer.Add(caption_example_text)
        example_sizer.Add(regex_example_text2)
        example_sizer.Add(caption_example_text2)

        vsizer = wx.BoxSizer(wx.VERTICAL)
        vsizer.Add(hsizer, 1, wx.ALL | wx.EXPAND, 5)
        vsizer.Add(example_sizer, 0, wx.ALL | wx.EXPAND, 5)
        vsizer.AddSpacer(10)
        vsizer.Add(num_ticks_sizer, 0, wx.ALL | wx.EXPAND, 5)
        vsizer.Add(show_detailed_queue_packets_checkbox, 0, wx.ALL | wx.EXPAND, 5)
        vsizer.Add(exit_btn_sizer, 0, wx.ALL | wx.EXPAND, 5)

        self.errors_label = wx.StaticText(self, label='No issues found', size=(col0_width,-1))
        hsizer2 = wx.BoxSizer(wx.HORIZONTAL)
        hsizer2.Add(vsizer)
        hsizer2.AddSpacer(10)
        hsizer2.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.EXPAND)
        hsizer2.AddSpacer(10)
        hsizer2.Add(self.errors_label)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(hsizer2, 1, wx.ALL | wx.EXPAND, 5)
        self.SetSizer(sizer)
        self.Layout()

        self.element_path_regexes_list_ctrl.SetColumnWidth(0, col0_width)
        self.element_path_regexes_list_ctrl.SetColumnWidth(1, col1_width)

        dlg_width  = col0_width + col1_width
        dlg_min_width = dlg_width + hsizer.CalcMin()[0] + 50
        dlg_min_height = sizer.CalcMin()[1] + 75
        self.SetSize((dlg_min_width, dlg_min_height))
        self.Layout()

    @property
    def frame(self):
        return self.GetParent().frame

    def GetNumTicksBefore(self):
        return self.num_ticks_before_slider.GetValue()

    def GetNumTicksAfter(self):
        return self.num_ticks_after_slider.GetValue()

    def ShowDetailedQueuePackets(self):
        return self.show_detailed_queue_packets
    
    def GetElementPathCaptionRegexes(self, as_list=False):
        regexes = OrderedDict()
        for row in range(self.element_path_regexes_list_ctrl.GetItemCount()):
            path_regex = self.element_path_regexes_list_ctrl.GetItemText(row, 0)
            caption_replacements = self.element_path_regexes_list_ctrl.GetItemText(row, 1)
            regexes[path_regex] = caption_replacements

        if as_list:
            return list(regexes.items())

        return regexes

    def __OnListCtrlItemSelected(self, evt):
        selected_elem_idxs = self.__GetListCtrlSelectedItemIdxs()
        if len(selected_elem_idxs) == 0:
            self.move_up_btn.Disable()
            self.move_down_btn.Disable()
            self.remove_btn.Disable()
            return

        if len(selected_elem_idxs) != 1:
            self.move_up_btn.Disable()
            self.move_down_btn.Disable()

            if len(selected_elem_idxs) > 0:
                self.remove_btn.Enable()
            else:
                self.remove_btn.Disable()

            return

        selected_elem_idx = selected_elem_idxs[0]
        if selected_elem_idx == wx.NOT_FOUND:
            self.move_up_btn.Disable()
            self.move_down_btn.Disable()
            self.remove_btn.Disable()
            return
        else:
            self.remove_btn.Enable()

            if selected_elem_idx == 0:
                self.move_up_btn.Disable()
            else:
                self.move_up_btn.Enable()

            if selected_elem_idx == self.element_path_regexes_list_ctrl.GetItemCount() - 1:
                self.move_down_btn.Disable()
            else:
                self.move_down_btn.Enable()

    def __MoveSelectedElemUp(self, evt):
        selected_elem_idxs = self.__GetListCtrlSelectedItemIdxs()
        assert len(selected_elem_idxs) == 1
        selected_elem_idx = selected_elem_idxs[0]
        assert selected_elem_idx > 0

        orig_top_idx = selected_elem_idx-1
        orig_bottom_idx = selected_elem_idx

        orig_top_item_col0_text = self.element_path_regexes_list_ctrl.GetItemText(orig_top_idx, 0)
        orig_top_item_col1_text = self.element_path_regexes_list_ctrl.GetItemText(orig_top_idx, 1)

        orig_bottom_item_col0_text = self.element_path_regexes_list_ctrl.GetItemText(orig_bottom_idx, 0)
        orig_bottom_item_col1_text = self.element_path_regexes_list_ctrl.GetItemText(orig_bottom_idx, 1)

        self.element_path_regexes_list_ctrl.SetItem(orig_top_idx, 0, orig_bottom_item_col0_text)
        self.element_path_regexes_list_ctrl.SetItem(orig_top_idx, 1, orig_bottom_item_col1_text)

        self.element_path_regexes_list_ctrl.SetItem(orig_bottom_idx, 0, orig_top_item_col0_text)
        self.element_path_regexes_list_ctrl.SetItem(orig_bottom_idx, 1, orig_top_item_col1_text)

        self.element_path_regexes_list_ctrl.Select(orig_bottom_idx, False)
        self.element_path_regexes_list_ctrl.Select(orig_top_idx, True)

        element_path_caption_regexes = self.GetElementPathCaptionRegexes(as_list=True)
        tmp = element_path_caption_regexes[orig_top_idx]
        element_path_caption_regexes[orig_top_idx] = element_path_caption_regexes[orig_bottom_idx]
        element_path_caption_regexes[orig_bottom_idx] = tmp
        self.caption_mgr.SetElemPathRegexReplacements(element_path_caption_regexes)

    def __MoveSelectedElemDown(self, evt):
        selected_elem_idxs = self.__GetListCtrlSelectedItemIdxs()
        assert len(selected_elem_idxs) == 1
        selected_elem_idx = selected_elem_idxs[0]
        assert selected_elem_idx < self.element_path_regexes_list_ctrl.GetItemCount() - 1

        orig_top_idx = selected_elem_idx
        orig_bottom_idx = selected_elem_idx+1

        orig_top_item_col0_text = self.element_path_regexes_list_ctrl.GetItemText(orig_top_idx, 0)
        orig_top_item_col1_text = self.element_path_regexes_list_ctrl.GetItemText(orig_top_idx, 1)

        orig_bottom_item_col0_text = self.element_path_regexes_list_ctrl.GetItemText(orig_bottom_idx, 0)
        orig_bottom_item_col1_text = self.element_path_regexes_list_ctrl.GetItemText(orig_bottom_idx, 1)

        self.element_path_regexes_list_ctrl.SetItem(orig_top_idx, 0, orig_bottom_item_col0_text)
        self.element_path_regexes_list_ctrl.SetItem(orig_top_idx, 1, orig_bottom_item_col1_text)

        self.element_path_regexes_list_ctrl.SetItem(orig_bottom_idx, 0, orig_top_item_col0_text)
        self.element_path_regexes_list_ctrl.SetItem(orig_bottom_idx, 1, orig_top_item_col1_text)

        self.element_path_regexes_list_ctrl.Select(orig_top_idx, False)
        self.element_path_regexes_list_ctrl.Select(orig_bottom_idx, True)

        element_path_caption_regexes = self.GetElementPathCaptionRegexes(as_list=True)
        tmp = element_path_caption_regexes[orig_top_idx]
        element_path_caption_regexes[orig_top_idx] = element_path_caption_regexes[orig_bottom_idx]
        element_path_caption_regexes[orig_bottom_idx] = tmp
        self.caption_mgr.SetElemPathRegexReplacements(element_path_caption_regexes)

    def __RemoveSelectedElems(self, evt):
        selected_elem_idxs = self.__GetListCtrlSelectedItemIdxs()
        selected_elem_idxs.reverse()

        for i in selected_elem_idxs:
            self.element_path_regexes_list_ctrl.DeleteItem(i)

        for i in range(self.element_path_regexes_list_ctrl.GetItemCount()):
            self.element_path_regexes_list_ctrl.Select(i, False)

        element_path_caption_regexes = self.GetElementPathCaptionRegexes()
        self.caption_mgr.SetElemPathRegexReplacements(element_path_caption_regexes)
        self.remove_btn.Disable()

    def __GetListCtrlSelectedItemIdxs(self):
        idxs = []
        item_count = self.element_path_regexes_list_ctrl.GetItemCount()

        for i in range(item_count):
            if self.element_path_regexes_list_ctrl.GetItemState(i, wx.LIST_STATE_SELECTED):
                idxs.append(i)

        return idxs

    def __OnShowDetailedQueuePacketsChanged(self, evt):
        self.show_detailed_queue_packets = evt.IsChecked()

    def __OnListCtrlItemDClicked(self, evt):
        # Get the mouse position in the list control
        mouse_x, mouse_y = evt.GetPosition()

        hit = self.element_path_regexes_list_ctrl.HitTest((mouse_x, mouse_y))
        if not hit or hit[0] == -1:
            return
        
        row = hit[0]
        if mouse_x < self.element_path_regexes_list_ctrl.GetColumnWidth(0):
            col = 0
        elif mouse_x > self.element_path_regexes_list_ctrl.GetColumnWidth(0):
            col = 1
        else:
            return

        row_rect = self.element_path_regexes_list_ctrl.GetItemRect(row)
        if col == 0:
            cell_rect = wx.Rect(row_rect.GetLeft(),
                                row_rect.GetTop(),
                                self.element_path_regexes_list_ctrl.GetColumnWidth(0),
                                row_rect.GetHeight())
        else:
            cell_rect = wx.Rect(row_rect.GetLeft() + self.element_path_regexes_list_ctrl.GetColumnWidth(0),
                                row_rect.GetTop(),
                                self.element_path_regexes_list_ctrl.GetColumnWidth(1),
                                row_rect.GetHeight())

        pos = cell_rect.GetTopLeft()
        size = cell_rect.GetSize()
        self.__EditListCtrlCell(row, col, pos, size)

    def __EditListCtrlCell(self, row, col, text_ctrl_pos, text_ctrl_size):
        # Get the current value of the cell
        current_text = self.element_path_regexes_list_ctrl.GetItemText(row, col)

        # Create a text control to edit the cell
        self.text_ctrl = wx.TextCtrl(self, value=current_text, style=wx.TE_PROCESS_ENTER, pos=text_ctrl_pos, size=text_ctrl_size)

        # Callback when editing the cell
        self.text_ctrl.Bind(wx.EVT_TEXT, lambda evt: self.__OnListCtrlEdit(evt, row, col, current_text))

        # Callbacks when finished editing the cell
        self.text_ctrl.Bind(wx.EVT_TEXT_ENTER, lambda evt: self.__OnListCtrlEditComplete(evt, row, col, current_text))
        self.text_ctrl.Bind(wx.EVT_KILL_FOCUS, lambda evt: self.__OnListCtrlEditComplete(evt, row, col, current_text))

        # Focus the text control right away
        self.text_ctrl.SetFocus()

    def __OnListCtrlEdit(self, evt, row, col, current_text):
        if col == 0:
            regex = self.text_ctrl.GetValue()
            try:
                re.compile(regex)
                self.text_ctrl.SetBackgroundColour(wx.WHITE)
                evt.Skip()
            except:
                self.text_ctrl.SetBackgroundColour((255, 192, 203)) # Pink
        else:
            regex = self.element_path_regexes_list_ctrl.GetItemText(row, 0)
            caption = self.text_ctrl.GetValue()
            valid = False

            for elem_path in self.frame.simhier.GetContainerElemPaths():
                try:
                    re.compile(regex)
                    re.sub(regex, caption, elem_path)
                    self.text_ctrl.SetBackgroundColour(wx.WHITE)
                    evt.Skip()
                    valid = True
                    break
                except:
                    pass

            if not valid:
                self.text_ctrl.SetBackgroundColour((255, 192, 203)) # Pink

    def __OnListCtrlEditComplete(self, evt, row, col, orig_text):
        if not self.text_ctrl:
            return

        self.text_ctrl.Unbind(wx.EVT_TEXT_ENTER)
        self.text_ctrl.Unbind(wx.EVT_KILL_FOCUS)
        evt.Skip()

        new_value = self.text_ctrl.GetValue()

        def DestroyTextCtrl(text_ctrl):
            text_ctrl.Destroy()
            text_ctrl = None

        wx.CallAfter(DestroyTextCtrl, self.text_ctrl)

        def SetListCtrlItem(list_ctrl, row, col, text):
            list_ctrl.SetItem(row, col, text)

        if self.text_ctrl.GetBackgroundColour() == (255, 192, 203):
            # The regex or caption is invalid. Revert to the original text.
            wx.CallAfter(SetListCtrlItem, self.element_path_regexes_list_ctrl, row, col, orig_text)
        else:
            # The regex or caption is valid. Update the list control.
            wx.CallAfter(SetListCtrlItem, self.element_path_regexes_list_ctrl, row, col, new_value)

        wx.CallAfter(self.__ValidateRegexSettings, None)

    def __OnNumTicksBeforeSliderChanged(self, evt):
        self.num_ticks_before_value_label.SetLabel(str(self.num_ticks_before_slider.GetValue()))

    def __OnNumTicksAfterSliderChanged(self, evt):
        self.num_ticks_after_value_label.SetLabel(str(self.num_ticks_after_slider.GetValue()))

    def __ValidateRegexSettings(self, evt):
        if not self.ok_btn:
            return

        orig_regex_replacements = self.caption_mgr.GetElemPathRegexReplacements()
        validate_regex_replacements = OrderedDict()
        for row in range(self.element_path_regexes_list_ctrl.GetItemCount()):
            path_regex = self.element_path_regexes_list_ctrl.GetItemText(row, 0)
            caption_replacements = self.element_path_regexes_list_ctrl.GetItemText(row, 1)
            validate_regex_replacements[path_regex] = caption_replacements

        self.caption_mgr.SetElemPathRegexReplacements(validate_regex_replacements)

        error_msgs = []
        for row in range(self.element_path_regexes_list_ctrl.GetItemCount()):
            path_regex = self.element_path_regexes_list_ctrl.GetItemText(row, 0)
            caption_replacements = self.element_path_regexes_list_ctrl.GetItemText(row, 1)

            try:
                re.compile(path_regex)
            except:
                error_msgs.append('Invalid regex in row {}:\n  {}'.format(row, path_regex))
                continue

            replacements_valid = False
            for elem_path in self.frame.simhier.GetContainerElemPaths():
                try:
                    re.sub(path_regex, caption_replacements, elem_path)
                    replacements_valid = True
                    break
                except:
                    continue

            if not replacements_valid:
                error_msgs.append('Invalid replacements in row {}:\n  {}'.format(row, caption_replacements))

        caption_prefixes = set()
        for elem_path in self.frame.simhier.GetContainerElemPaths():
            caption_prefix = self.caption_mgr.GetCaptionPrefix(elem_path)
            if caption_prefix is None:
                continue

            if caption_prefix in caption_prefixes:
                error_msgs.append('Duplicate caption prefix found: {}'.format(caption_prefix))
            else:
                caption_prefixes.add(caption_prefix)

        if len(error_msgs) > 0:
            self.ok_btn.Disable()
            self.caption_mgr.SetElemPathRegexReplacements(orig_regex_replacements)
            self.errors_label.SetLabel('\n'.join(error_msgs))
            self.errors_label.SetForegroundColour(wx.RED)

            current_font = self.errors_label.GetFont()

            # Create a new font based on the current one but bold
            bold_font = wx.Font(current_font.GetPointSize(),
                                current_font.GetFamily(),
                                current_font.GetStyle(),
                                wx.FONTWEIGHT_BOLD)
            
            self.errors_label.SetFont(bold_font)
        else:
            self.ok_btn.Enable()
            self.errors_label.SetLabel('No issues found')
            self.errors_label.SetForegroundColour(wx.BLACK)

            current_font = self.errors_label.GetFont()

            # Create a new font based on the current one but without bold
            normal_font = wx.Font(current_font.GetPointSize(),
                                  current_font.GetFamily(),
                                  current_font.GetStyle(),
                                  wx.FONTWEIGHT_NORMAL)
            
            self.errors_label.SetFont(normal_font)

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
        elem_paths = []
        for elem_path in self.simhier.GetContainerElemPaths():
            for regex, _ in self.regex_replacements_by_elem_path_regex.items():
                if elem_path == regex:
                    elem_paths.append(elem_path)
                    break

                if re.compile(regex).match(elem_path):
                    elem_paths.append(elem_path)
                    break

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
