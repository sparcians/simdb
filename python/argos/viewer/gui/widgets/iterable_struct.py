import wx, wx.grid
from collections.abc import Iterable
from viewer.gui.dialogs.widget_data_selections import WidgetDataSelectionsDlg

class IterableStruct(wx.Panel):
    def __init__(self, parent, frame, elem_path):
        super(IterableStruct, self).__init__(parent)
        self.SetBackgroundColour('white')
        self.frame = frame
        self.elem_path = elem_path
        self.deserializer = frame.data_retriever.GetDeserializer(elem_path)
        all_field_names = self.deserializer.GetAllFieldNames()
        self.capacity = frame.simhier.GetCapacityByElemPath(elem_path)
        num_rows = self.capacity
        num_cols = len(all_field_names)
        if 'DID' in all_field_names:
            num_cols -= 1
        assert len(all_field_names) > 0

        self.grid = wx.grid.Grid(self)
        self.grid.CreateGrid(num_rows, num_cols, wx.grid.Grid.GridSelectNone)
        self.grid.EnableEditing(False)
        self.grid.SetLabelBackgroundColour('white')
        self.__SyncGridViewSettings()

        mono10 = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        self.grid.SetDefaultCellFont(mono10)
        self.grid.SetLabelFont(mono10)

        for i in range(self.capacity):
            self.grid.SetRowLabelValue(i, str(i))

        self.utiliz_elem = UtilizElement(self, frame, self.capacity)
        location_elem = wx.StaticText(self, label=elem_path)

        font = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        location_elem.SetFont(font)

        gear_btn, clear_btn, split_lr, split_tb, maximize_btn = frame.CreateWidgetStandardButtons(
            self, self.__EditWidget, 'Edit widget settings')

        row1 = wx.BoxSizer(wx.HORIZONTAL)
        row1.Add(gear_btn, 0, wx.TOP | wx.RIGHT, 5)
        row1.Add(clear_btn, 0, wx.TOP | wx.RIGHT, 5)
        row1.Add(split_lr, 0, wx.TOP | wx.RIGHT, 5)
        row1.Add(split_tb, 0, wx.TOP | wx.RIGHT, 5)
        row1.Add(maximize_btn, 0, wx.TOP | wx.RIGHT, 5)
        row1.AddSpacer(5)
        row1.Add(self.utiliz_elem, 0, wx.TOP, 7)
        row1.AddSpacer(5)
        row1.Add(location_elem, 0, wx.EXPAND | wx.TOP, 7)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(row1, 0, wx.ALL, 5)
        sizer.Add(self.grid, 0, wx.EXPAND | wx.LEFT | wx.TOP, 5)
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

        auto_colorize_col_name = 'DID' if 'DID' in self.visible_field_names else self.visible_field_names[0]
        self.__ClearGrid()

        if len(queue_data['DataVals']) and isinstance(queue_data['DataVals'][0], Iterable):
            for idx, row_data in enumerate(queue_data['DataVals'][0]):
                if row_data is None:
                    continue

                write_col = 0
                for read_col, field_name in enumerate(self.visible_field_names):
                    if field_name == 'DID':
                        continue

                    self.grid.SetCellValue(idx, write_col, str(row_data[read_col][1]))

                    color_keyval = None
                    for key, keyval in row_data:
                        if key == auto_colorize_col_name:
                            color_keyval = keyval
                            break

                    assert color_keyval is not None
                    color = widget_renderer.GetAutoColor(color_keyval)
                    self.grid.SetCellBackgroundColour(idx, write_col, color)
                    write_col += 1

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
        i = 0
        for field_name in self.visible_field_names:
            if field_name != 'DID':
                self.grid.SetColLabelValue(i, field_name)
                i += 1

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
            if len(selected) == 0:
                wx.MessageBox('No data selected', 'Error', wx.OK | wx.ICON_ERROR)
                selected = None
            else:
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
