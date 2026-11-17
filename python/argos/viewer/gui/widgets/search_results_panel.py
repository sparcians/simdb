import wx
from viewer.gui.font_settings import FontRole

class SearchResultsPanel(wx.Panel):
    """Panel shown above the playback bar that lists search results.

    It is hidden until results are available, and the user can hide it
    again with the 'X' button in the top-right corner.
    """

    COLUMNS = ('', 'tick', 'cyc', 'clock', 'path', 'caption', 'source', 'data')
    DB_UNITS = ('ticks', 'cycles')
    TAG_SIZE = (24, 16)

    def __init__(self, frame):
        super().__init__(frame)
        self.frame = frame
        self.SetBackgroundColour('light gray')

        self.title_text = wx.StaticText(self, label='Search results')
        self.title_text.SetFont(frame.TrackFont(self.title_text, FontRole.SEARCH_TITLEBAR,
                                                family=wx.FONTFAMILY_DEFAULT, weight=wx.FONTWEIGHT_BOLD))

        self.refresh_btn = frame.CreateTrackedButton(self, label='Refresh', size=(70, 22))
        self.refresh_btn.SetToolTip('Rerun the search')
        self.refresh_btn.Bind(wx.EVT_BUTTON, self.__OnRefresh)
        self._rerun_search = None

        # Called after the panel is hidden, e.g. so widgets can drop their search highlights.
        self.on_hide = None

        # Database search. validate_db_range(start, end, unit) returns an error string
        # (or None if valid), on_search_db(start, end, unit) runs the search, and
        # on_go_to_db_result(tick) is called when a database result is double-clicked.
        self.validate_db_range = None
        self.on_search_db = None
        self.on_go_to_db_result = None

        self.db_start = wx.TextCtrl(self, size=(80, 22))
        self.db_start.SetHint('start')
        self.db_end = wx.TextCtrl(self, size=(80, 22))
        self.db_end.SetHint('end')
        self.db_unit = wx.ComboBox(self, choices=list(self.DB_UNITS), style=wx.CB_READONLY, size=(105, 22))
        self.db_unit.SetSelection(0)
        self.db_search_btn = frame.CreateTrackedButton(self, label='Search DB', size=(80, 22))
        for ctrl in (self.db_start, self.db_end):
            ctrl.Bind(wx.EVT_TEXT, self.__ValidateDbRange)
        self.db_unit.Bind(wx.EVT_COMBOBOX, self.__ValidateDbRange)
        self.db_search_btn.Bind(wx.EVT_BUTTON, self.__OnSearchDb)

        self.search_text = wx.StaticText(self, label='')

        self.close_btn = frame.CreateTrackedButton(self, label='Close', size=(70, 22))
        self.close_btn.SetToolTip('Close search results')
        self.close_btn.Bind(wx.EVT_BUTTON, lambda evt: self.HidePanel())

        self.list_ctrl = wx.ListCtrl(self, style=wx.LC_REPORT | wx.LC_SINGLE_SEL)
        mono_font = frame.TrackFont(self, FontRole.SEARCH_RESULTS, family=wx.FONTFAMILY_TELETYPE,
                                    apply=SearchResultsPanel._ApplyResultsFont)
        self.list_ctrl.SetFont(mono_font)
        self.search_text.SetFont(mono_font)

        # Time taken by the last search, right-aligned and sized for the widest common value.
        self.elapsed_text = wx.StaticText(self, label='', style=wx.ALIGN_RIGHT)
        self.elapsed_text.SetFont(mono_font)
        self.elapsed_text.SetForegroundColour(wx.Colour(0, 128, 0))
        self.elapsed_text.SetToolTip('Time taken by the last search')
        self.__SizeElapsedText(mono_font)
        for name in self.COLUMNS:
            self.list_ctrl.AppendColumn(name)
        self.list_ctrl.Bind(wx.EVT_LIST_ITEM_ACTIVATED, self.__OnItemActivated)

        # Rows are the widget results followed by the database results.
        self._widget_entries = []
        self._db_entries = []
        self._db_searched = False
        self._row_entries = []

        db_sizer = wx.BoxSizer(wx.HORIZONTAL)
        db_sizer.Add(self.db_start, 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 3)
        db_sizer.Add(self.db_end, 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 3)
        db_sizer.Add(self.db_unit, 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 3)
        db_sizer.Add(self.db_search_btn, 0, wx.ALIGN_CENTER_VERTICAL)

        header = wx.BoxSizer(wx.HORIZONTAL)
        header.Add(self.title_text, 0, wx.ALIGN_CENTER_VERTICAL | wx.LEFT, 5)
        header.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.EXPAND | wx.ALL, 4)
        header.Add(self.search_text, 1, wx.ALIGN_CENTER_VERTICAL | wx.LEFT, 4)
        header.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.EXPAND | wx.ALL, 4)
        header.Add(db_sizer, 0, wx.ALIGN_CENTER_VERTICAL)
        header.Add(self.elapsed_text, 0, wx.ALIGN_CENTER_VERTICAL | wx.LEFT, 6)
        header.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.EXPAND | wx.ALL, 4)
        header.Add(self.refresh_btn, 0, wx.ALL, 2)
        header.Add(self.close_btn, 0, wx.ALL, 2)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(header, 0, wx.EXPAND)
        sizer.Add(self.list_ctrl, 1, wx.EXPAND)
        self.SetSizer(sizer)
        self.SetMinSize((-1, 150))

        self.Hide()

    def ShowResults(self, entries: list, search_text: str = '', rerun_search=None):
        """Show one row per entry, where each entry is a
        (tick, cyc, clock, path, caption, source, data, color, tag) tuple.

        'data' is a list of (name, value) pairs. It is shown as 'name(value)' pairs
        padded so that the same field lines up across rows with the same field names.
        'color' and 'tag' are the row's auto-color and auto-tag, shown in the first column.

        'search_text' describes what was searched for, and 'rerun_search' is called
        by the Refresh button to run the same search again.

        This replaces everything shown, including any database results.
        """
        self.search_text.SetLabel(search_text)
        self.Layout()
        self._rerun_search = rerun_search
        self.refresh_btn.Enable(rerun_search is not None)

        self._widget_entries = list(entries)
        self._db_entries = []
        self._db_searched = False
        self.__Render()
        self.__SetVisible(True)

    def SetDatabaseResults(self, entries: list):
        """Replaces the database results (same entry format) shown after the widget results."""
        self._db_entries = list(entries)
        self._db_searched = True
        self.__Render()

    def HasDatabaseResults(self):
        # True once a database search has run, even if it found nothing.
        return self._db_searched

    @staticmethod
    def FormatElapsed(seconds: float):
        # 0.39s, 1.84s, 12.9s, 1m8s. Rounding is checked first so that e.g. 9.996
        # becomes 10.0s rather than a 6 character 10.00s.
        if round(seconds, 2) < 10:
            return f'{seconds:.2f}s'
        if round(seconds, 1) < 60:
            return f'{seconds:.1f}s'
        minutes, secs = divmod(int(round(seconds)), 60)
        return f'{minutes}m{secs}s'

    def SetElapsedTime(self, seconds: float):
        self.elapsed_text.SetLabel(self.FormatElapsed(seconds))
        self.Layout()

    def SetDatabaseRange(self, start: str, end: str, unit: str):
        self.db_start.ChangeValue(start)
        self.db_end.ChangeValue(end)
        self.db_unit.SetStringSelection(unit)
        self.__ValidateDbRange(None)

    def GetDatabaseRange(self):
        return (self.db_start.GetValue().strip(), self.db_end.GetValue().strip(),
                self.db_unit.GetStringSelection())

    def __ValidateDbRange(self, event):
        # Both inputs are validated together (e.g. 'start' must not be after 'end'),
        # so the button is only enabled when the pair makes sense.
        error = self.validate_db_range(*self.GetDatabaseRange()) if self.validate_db_range else None
        self.db_search_btn.Enable(error is None)
        if error:
            self.db_search_btn.SetToolTip(error)
        else:
            self.db_search_btn.UnsetToolTip()

    def __OnSearchDb(self, event):
        if self.on_search_db:
            self.on_search_db(*self.GetDatabaseRange())

    def __OnItemActivated(self, event):
        row = event.GetIndex()
        if 0 <= row < len(self._row_entries):
            is_db, entry = self._row_entries[row]
            if is_db and self.on_go_to_db_result:
                self.on_go_to_db_result(entry[0])

    def __Render(self):
        rows = [(False, e) for e in self._widget_entries] + [(True, e) for e in self._db_entries]
        self._row_entries = rows

        # Rows with the same field names are assumed to share a struct type, and are
        # aligned together (same approach as the detailed packet column in SchedulingLinesWidget).
        max_lens = {}
        for _, entry in rows:
            fields = tuple(name for name, _ in entry[6])
            lens = max_lens.setdefault(fields, {})
            for name, value in entry[6]:
                lens[name] = max(lens.get(name, 0), len(value))

        self.list_ctrl.DeleteAllItems()
        # SetImageList() doesn't take ownership, so the list must outlive this call.
        self.image_list = wx.ImageList(*self.TAG_SIZE)
        image_list = self.image_list
        image_idxs = {}
        self.list_ctrl.SetImageList(image_list, wx.IMAGE_LIST_SMALL)

        for _, entry in rows:
            lens = max_lens[tuple(name for name, _ in entry[6])]
            data = ' ' + ' '.join(f'{name}({value})' + ' ' * (lens[name] - len(value))
                                  for name, value in entry[6])

            color, tag = entry[7], entry[8]
            key = (wx.Colour(color).GetRGB(), tag)
            if key not in image_idxs:
                image_idxs[key] = image_list.Add(self.__CreateTagBitmap(color, tag))

            row = self.list_ctrl.InsertItem(self.list_ctrl.GetItemCount(), '', image_idxs[key])
            for col, value in enumerate(entry[:6] + (data,), start=1):
                self.list_ctrl.SetItem(row, col, str(value))

        self.__FitColumns()

        self.title_text.SetLabel(f'Search results ({len(rows)})')
        self.Layout()

    def HidePanel(self):
        self.__SetVisible(False)
        if self.on_hide:
            self.on_hide()

    def _ApplyResultsFont(self, font):
        self.list_ctrl.SetFont(font)
        self.search_text.SetFont(font)
        self.elapsed_text.SetFont(font)
        self.__SizeElapsedText(font)

        # The tag bitmaps and column widths were built for the old font.
        if self._row_entries:
            self.__Render()

    def __SizeElapsedText(self, font):
        dc = wx.ClientDC(self)
        dc.SetFont(font)
        self.elapsed_text.SetMinSize((dc.GetTextExtent('9.99s')[0], -1))

    def __OnRefresh(self, event):
        if self._rerun_search:
            self._rerun_search()

    def __SetVisible(self, visible: bool):
        if self.IsShown() != visible:
            self.Show(visible)

        # Re-layout the parent so the panel's space is given/returned to the inspector.
        self.GetParent().Layout()

    def __CreateTagBitmap(self, color, tag: str):
        # wx.ListCtrl can't colour individual cells, so the auto-color/tag is drawn
        # into a bitmap shown as the first column's image.
        width, height = self.TAG_SIZE
        bitmap = wx.Bitmap(width, height)
        dc = wx.MemoryDC(bitmap)
        dc.SetBackground(wx.Brush(wx.Colour(color)))
        dc.Clear()
        dc.SetPen(wx.BLACK_PEN)
        dc.SetBrush(wx.TRANSPARENT_BRUSH)
        dc.DrawRectangle(0, 0, width, height)
        dc.SetFont(self.list_ctrl.GetFont())
        dc.SetTextForeground(wx.BLACK)
        text_width, text_height = dc.GetTextExtent(tag)
        dc.DrawText(tag, (width - text_width) // 2, (height - text_height) // 2)
        dc.SelectObject(wx.NullBitmap)
        return bitmap

    def __FitColumns(self):
        # Rows don't change once populated, so size each column to the wider of
        # its contents and its header.
        for col in range(len(self.COLUMNS)):
            self.list_ctrl.SetColumnWidth(col, wx.LIST_AUTOSIZE)
            content_width = self.list_ctrl.GetColumnWidth(col)
            self.list_ctrl.SetColumnWidth(col, wx.LIST_AUTOSIZE_USEHEADER)
            header_width = self.list_ctrl.GetColumnWidth(col)
            self.list_ctrl.SetColumnWidth(col, max(content_width, header_width))
