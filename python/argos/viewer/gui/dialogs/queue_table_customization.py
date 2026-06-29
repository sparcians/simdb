import wx

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
