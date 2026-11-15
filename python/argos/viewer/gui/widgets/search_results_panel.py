import wx
from viewer.gui.widgets.button import CreateButton

class SearchResultsPanel(wx.Panel):
    """Panel shown above the playback bar that lists search results.

    It is hidden until results are available, and the user can hide it
    again with the 'X' button in the top-right corner.
    """

    COLUMNS = ('', 'tick', 'cyc', 'clock', 'path', 'caption', 'source', 'data')
    TAG_SIZE = (24, 16)

    def __init__(self, parent):
        super().__init__(parent)
        self.SetBackgroundColour('light gray')

        self.title_text = wx.StaticText(self, label='Search results')
        font = self.title_text.GetFont()
        font.SetWeight(wx.FONTWEIGHT_BOLD)
        self.title_text.SetFont(font)

        self.close_btn = CreateButton(self, label='X', size=(36, 22))
        self.close_btn.SetToolTip('Close search results')
        self.close_btn.Bind(wx.EVT_BUTTON, lambda evt: self.HidePanel())

        self.list_ctrl = wx.ListCtrl(self, style=wx.LC_REPORT | wx.LC_SINGLE_SEL)
        mono_font = wx.Font(self.list_ctrl.GetFont().GetPointSize(), wx.FONTFAMILY_TELETYPE,
                            wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        self.list_ctrl.SetFont(mono_font)
        for name in self.COLUMNS:
            self.list_ctrl.AppendColumn(name)

        header = wx.BoxSizer(wx.HORIZONTAL)
        header.Add(self.title_text, 1, wx.ALIGN_CENTER_VERTICAL | wx.LEFT, 5)
        header.Add(self.close_btn, 0, wx.ALL, 2)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(header, 0, wx.EXPAND)
        sizer.Add(self.list_ctrl, 1, wx.EXPAND)
        self.SetSizer(sizer)
        self.SetMinSize((-1, 150))

        self.Hide()

    def ShowResults(self, entries: list):
        """Show one row per entry, where each entry is a
        (tick, cyc, clock, path, caption, source, data, color, tag) tuple.

        'data' is a list of (name, value) pairs. It is shown as 'name(value)' pairs
        padded so that the same field lines up across rows with the same field names.
        'color' and 'tag' are the row's auto-color and auto-tag, shown in the first column.
        """
        # Rows with the same field names are assumed to share a struct type, and are
        # aligned together (same approach as the detailed packet column in SchedulingLinesWidget).
        max_lens = {}
        for entry in entries:
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

        for entry in entries:
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

        self.title_text.SetLabel(f'Search results ({len(entries)})')
        self.__SetVisible(True)

    def HidePanel(self):
        self.__SetVisible(False)

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
