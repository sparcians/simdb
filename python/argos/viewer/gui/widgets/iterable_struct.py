import wx, wx.grid
from collections.abc import Iterable

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
        gear_btn = wx.BitmapButton(self, bitmap=frame.CreateResourceBitmap('gear.png'), pos=(5,5))
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
        queue_data = self.frame.data_retriever.Unpack(self.elem_path, (tick,tick))

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
        all_field_names = self.deserializer.GetAllFieldNames()
        auto_colorize_col = self.frame.data_retriever.GetAutoColorizeColumn(self.elem_path)
        dlg = QueueTableCustomizationDlg(self,
                                         all_field_names,
                                         self.visible_field_names,
                                         auto_colorize_col)

        if dlg.ShowModal() == wx.ID_OK:
            # Note that the data retriever will update all widgets when these method is called
            self.frame.data_retriever.SetVisibleFieldNames(self.elem_path, dlg.GetDisplayedColumns())
            self.frame.data_retriever.SetAutoColorizeColumn(self.elem_path, dlg.GetAutoColorizeColumn())

        dlg.Destroy()

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

class QueueTableCustomizationDlg(wx.Dialog):
    def __init__(self, widget, all_columns, displayed_columns, auto_colorize_col):
        super().__init__(widget, title='Customize Widget')

        self.checkboxes = []
        self.radio_buttons = []
        self.panel = wx.Panel(self)
        sizer = wx.BoxSizer(wx.VERTICAL)

        tbox = wx.StaticText(self.panel, label='Select columns to display:')
        sizer.Add(tbox, 0, wx.ALL | wx.EXPAND, 5)

        # Create a checkbox for each item
        for s in all_columns:
            hbox = wx.BoxSizer(wx.HORIZONTAL)
            self.radio_buttons.append(wx.RadioButton(self.panel, label='Auto-colorize'))
            self.radio_buttons[-1].SetToolTip('Auto-colorize based on this column')
            hbox.Add(self.radio_buttons[-1], flag=wx.ALIGN_CENTER_VERTICAL)

            if s == auto_colorize_col:
                self.radio_buttons[-1].SetValue(True)
            else:
                self.radio_buttons[-1].SetValue(False)

            checkbox = wx.CheckBox(self.panel, label=s)
            hbox.Add(checkbox, 1, wx.ALL | wx.EXPAND, 5)

            if s in displayed_columns:
                checkbox.SetValue(True)
            else:
                checkbox.SetValue(False)

            self.checkboxes.append(checkbox)
            sizer.Add(hbox, 1, wx.ALL | wx.EXPAND, 5)

        # OK and Cancel buttons
        self._ok_btn = wx.Button(self.panel, wx.ID_OK)
        btn_sizer = wx.StdDialogButtonSizer()
        btn_sizer.AddButton(self._ok_btn)
        btn_sizer.AddButton(wx.Button(self.panel, wx.ID_CANCEL))
        btn_sizer.Realize()

        sizer.Add(btn_sizer, 0, wx.ALIGN_CENTER | wx.ALL, 10)
        self.panel.SetSizerAndFit(sizer)

        # Find the longest string length of all our checkbox labels
        dc = wx.ClientDC(self.panel)
        dc.SetFont(wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL))
        longest_length = 0

        for s in all_columns:
            text_width, _ = dc.GetTextExtent(s)
            longest_length = max(longest_length, text_width)

        w,h = sizer.GetMinSize()
        h += 100
        w = max(w, longest_length + 100)
        self.SetSize((w,h))
        self.Layout()
        self.Refresh()

        for checkbox in self.checkboxes:
            checkbox.Bind(wx.EVT_CHECKBOX, self.__OnCheckbox)

        for radio_button in self.radio_buttons:
            radio_button.Bind(wx.EVT_RADIOBUTTON, self.__OnRadioButtonGroup)

    def GetDisplayedColumns(self):
        # Return a list of selected items
        return [checkbox.GetLabel() for checkbox in self.checkboxes if checkbox.IsChecked()]
    
    def GetAutoColorizeColumn(self):
        for i,radio_btn in enumerate(self.radio_buttons):
            if radio_btn.GetValue():
                return self.checkboxes[i].GetLabel()

        return None

    def __OnCheckbox(self, event):
        any_selected = any(checkbox.IsChecked() for checkbox in self.checkboxes)
        self._ok_btn.Enable(any_selected)

        if not any_selected:
            self._ok_btn.SetToolTip('Select at least one column to display')
            return
        else:
            self._ok_btn.UnsetToolTip()

        for i,checkbox in enumerate(self.checkboxes):
            radio_btn = self.radio_buttons[i]
            if not checkbox.IsChecked() and radio_btn.GetValue():
                self._ok_btn.Enable(False)
                self._ok_btn.SetToolTip('Auto-colorize column must be displayed')
                return

    def __OnRadioButtonGroup(self, event):
        for radio_button in self.radio_buttons:
            radio_button.SetValue(False)

        event.GetEventObject().SetValue(True)

        for i,radio_btn in enumerate(self.radio_buttons):
            if radio_btn == event.GetEventObject():
                self.checkboxes[i].SetValue(True)
                break

        self._ok_btn.Enable(True)
        self._ok_btn.UnsetToolTip()
