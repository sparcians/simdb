import wx, wx.grid
from collections.abc import Iterable
from viewer.gui.dialogs.widget_data_selections import WidgetDataSelectionsDlg

class IterableStruct(wx.Panel):
    def __init__(self, parent, frame, elem_path):
        super(IterableStruct, self).__init__(parent)
        self.frame = frame
        self.elem_path = elem_path
        self.deserializer = frame.data_retriever.GetDeserializer(elem_path)
        all_field_names = self.deserializer.GetAllFieldNames()

        self.capacity = frame.simhier.GetCapacityByElemPath(elem_path)
        self.grid = wx.grid.Grid(self)
        self.grid.CreateGrid(self.capacity, len(all_field_names), wx.grid.Grid.GridSelectNone)
        self.grid.EnableEditing(False)
        self.__SyncGridViewSettings()

        # Create 10-point monospace font for the grid cells
        mono10 = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        self.grid.SetDefaultCellFont(mono10)
        self.grid.SetLabelFont(mono10)

        for i in range(self.capacity):
            self.grid.SetRowLabelValue(i, str(i))

        self.utiliz_elem = UtilizElement(self, frame, self.capacity)
        location_elem = wx.StaticText(self, label=elem_path)

        font = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        location_elem.SetFont(font)

        # Add a gear button (size 16x16) to the left of the time series plot.
        # Clicking the button will open a dialog to change the plot settings.
        # Note that we do not add the button to the sizer since we want to
        # force it to be in the top-left corner of the widget canvas. We do
        # this with the 'pos' argument to the wx.BitmapButton constructor.
        gear_btn = frame.CreateSettingsButton(self)
        gear_btn.Bind(wx.EVT_BUTTON, self.__EditWidget)
        gear_btn.SetToolTip('Edit widget settings')

        row1 = wx.BoxSizer(wx.HORIZONTAL)
        row1.AddSpacer(30)
        row1.Add(self.utiliz_elem, 0, wx.ALL, 5)
        row1.Add(location_elem, 1, wx.EXPAND | wx.ALL, 5)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(row1, 0, wx.EXPAND | wx.ALL, 10)
        sizer.Add(self.grid, 1, wx.EXPAND | wx.ALL, 10)
        self.SetSizer(sizer)
        self.Layout()

    @property
    def visible_field_names(self):
        return self.deserializer.GetVisibleFieldNames()

    def GetWidgetCreationString(self):
        return 'IterableStruct$' + self.elem_path

    def UpdateWidgetData(self):
        # This method can get called when our table settings have changed, so
        # make sure we keep the grid in sync with settings changes.
        self.__SyncGridViewSettings()

        widget_renderer = self.frame.widget_renderer
        tick = widget_renderer.tick
        queue_data = self.frame.data_retriever.Unpack(self.elem_path, tick)

        auto_colorize_col = self.frame.data_retriever.GetAutoColorizeColumn(self.elem_path)
        auto_colorize_col_idx = self.visible_field_names.index(auto_colorize_col) if auto_colorize_col in self.visible_field_names else None

        self.__ClearGrid()

        if len(queue_data['DataVals']) and isinstance(queue_data['DataVals'][0], Iterable):
            for idx, row_data in enumerate(queue_data['DataVals'][0]):
                if row_data is None:
                    continue

                for j, field_name in enumerate(self.visible_field_names):
                    self.grid.SetCellValue(idx, j, str(row_data[j][1]))
                    if auto_colorize_col_idx is not None:
                        auto_colorize_col_name = self.visible_field_names[auto_colorize_col_idx]
                        color_keyval = None
                        for key, keyval in row_data:
                            if key == auto_colorize_col_name:
                                color_keyval = keyval
                                break

                        assert color_keyval is not None
                        color = widget_renderer.GetAutoColor(color_keyval)
                        self.grid.SetCellBackgroundColour(idx, j, color)

            num_rows_shown = len(queue_data['DataVals'][0])
        else:
            num_rows_shown = 0

        for i in range(num_rows_shown):
            self.grid.ShowRow(i)

        for i in range(num_rows_shown, self.capacity):
            self.grid.HideRow(i)

        self.utiliz_elem.UpdateUtilizPct(self.frame.widget_renderer.utiliz_handler.GetUtilizPct(self.elem_path))
        self.grid.AutoSizeColumns()
        self.Layout()

    def GetCurrentViewSettings(self):
        # The view settings that we care about are the auto-colorize column (if any)
        # and the displayed columns. The DataRetriever class handles those settings.
        return {}
    
    def ApplyViewSettings(self, settings):
        # The view settings that we care about are the auto-colorize column (if any)
        # and the displayed columns. The DataRetriever class handles those settings.
        pass

    def GetCurrentUserSettings(self):
        return {}
    
    def __SyncGridViewSettings(self):
        # Remove all column labels
        for i in range(self.grid.GetNumberCols()):
            self.grid.SetColLabelValue(i, '')

        # Only show the visible field names
        for i, field_name in enumerate(self.visible_field_names):
            self.grid.SetColLabelValue(i, field_name)

        # Make sure we are showing the visible columns
        for i in range(len(self.visible_field_names)):
            self.grid.ShowCol(i)

        # Hide any columns that are not visible
        for i in range(len(self.visible_field_names), self.grid.GetNumberCols()):
            self.grid.HideCol(i)

        self.grid.AutoSizeColumns()
        self.Layout()

    def __ClearGrid(self):
        for i in range(self.grid.GetNumberRows()):
            for j in range(self.grid.GetNumberCols()):
                self.grid.SetCellValue(i, j, '')
                self.grid.SetCellBackgroundColour(i, j, wx.WHITE)

    def __EditWidget(self, event):
        assert self.elem_path not in (None, '')
        selected = [self.elem_path]
        dlg = WidgetDataSelectionsDlg(self, self.frame, selected, queues_only=True, single_selection=True)
        result = dlg.ShowModal()

        if result == wx.ID_OK:
            selected = dlg.GetSelectedElemPaths()
            assert len(selected) == 1
            selected = selected[0]
        else:
            selected = None
        dlg.Destroy()

        if selected:
            widget_container = self.GetParent()
            widget = IterableStruct(widget_container, self.frame, selected)
            widget_container.SetWidget(widget)

class UtilizElement(wx.StaticText):
    def __init__(self, parent, frame, capacity):
        super().__init__(parent)
        self.frame = frame
        self.capacity = capacity

        font = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        self.SetFont(font)

    def UpdateUtilizPct(self, utiliz_pct):
        self.SetLabel('{}%'.format(round(utiliz_pct * 100)))
        color = self.frame.widget_renderer.utiliz_handler.ConvertUtilizPctToColor(utiliz_pct)
        self.SetBackgroundColour(color)

        tooltip = 'Utilization: {}% ({}/{} bins filled)'.format(round(utiliz_pct * 100), int(utiliz_pct * self.capacity), self.capacity)
        self.SetToolTip(tooltip)
