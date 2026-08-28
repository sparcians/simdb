import wx
from functools import partial

class ClockPopup(wx.ComboPopup):
    def __init__(self, choices, selection_callback):
        super(ClockPopup, self).__init__()
        self.choices = choices
        self.selection_callback = selection_callback
        self.list_box = None

    def Init(self):
        self.list_box = None

    def Create(self, parent):
        self.list_box = wx.ListBox(parent, choices=self.choices, style=wx.LB_SINGLE)
        self.list_box.Bind(wx.EVT_LISTBOX, self.__OnSelection)
        return True

    def GetControl(self):
        return self.list_box

    def GetStringValue(self):
        selection = self.list_box.GetSelection()
        return self.list_box.GetString(selection) if selection != wx.NOT_FOUND else ''

    def SetStringValue(self, value):
        selection = self.list_box.FindString(value)
        if selection != wx.NOT_FOUND:
            self.list_box.SetSelection(selection)

    def __OnSelection(self, event):
        self.GetComboCtrl().SetValue(self.GetStringValue())
        self.GetComboCtrl().HidePopup()
        self.selection_callback()


class PlaybackBar(wx.Panel):
    def __init__(self, frame):
        super(PlaybackBar, self).__init__(frame, size=(frame.GetSize().width, -1))
        self.SetBackgroundColour('light gray')
        widget_renderer = self.frame.widget_renderer

        cursor = frame.db.cursor()
        cursor.execute('SELECT Name,Period FROM Clocks')
        self.clock_periods = {r[0]:r[1] for r in cursor.fetchall()}
        clk_names = list(self.clock_periods.keys())
        clk_names.sort()
        clk_names.insert(0, '<any clk edge>')

        self.clock_combobox = wx.ComboCtrl(self, value='<any clk edge>', style=wx.CB_READONLY)
        self._selected_clock = '<any clk edge>'
        clock_popup = ClockPopup(clk_names, self.__OnClockSelected)
        self.clock_combobox.SetPopupControl(clock_popup)
        self.clock_combobox.SetPopupMaxHeight(10 * self.clock_combobox.GetCharHeight() + 8)
        text_dc = wx.ClientDC(self.clock_combobox)
        text_dc.SetFont(self.clock_combobox.GetFont())
        choice_width = max(text_dc.GetTextExtent(choice)[0] for choice in clk_names)
        self.clock_combobox.SetMinSize((choice_width + 40, -1))
        self.current_cyc_text = wx.StaticText(self, label='cycle:{}'.format(widget_renderer.tick))
        font = self.current_cyc_text.GetFont()
        font.SetWeight(wx.FONTWEIGHT_BOLD)
        self.current_cyc_text.SetFont(font)
        self.current_tick_text = wx.StaticText(self, label='tick:{}'.format(widget_renderer.tick))
        self.__UpdateTimeLabels(widget_renderer.tick)

        self.minus_30_button = wx.Button(self, label='-30')
        self.minus_10_button = wx.Button(self, label='-10')
        self.minus_3_button = wx.Button(self, label='-3')
        self.minus_1_button = wx.Button(self, label='-1')
        self.plus_1_button = wx.Button(self, label='+1')
        self.plus_3_button = wx.Button(self, label='+3')
        self.plus_10_button = wx.Button(self, label='+10')
        self.plus_30_button = wx.Button(self, label='+30')

        self.cyc_slider = wx.Slider(self, minValue=widget_renderer.start_tick, maxValue=widget_renderer.end_tick, style=wx.SL_HORIZONTAL)

        self.minus_30_button.Bind(wx.EVT_BUTTON, partial(self.__OnStep, step=-30))
        self.minus_10_button.Bind(wx.EVT_BUTTON, partial(self.__OnStep, step=-10))
        self.minus_3_button.Bind(wx.EVT_BUTTON, partial(self.__OnStep, step=-3))
        self.minus_1_button.Bind(wx.EVT_BUTTON, partial(self.__OnStep, step=-1))
        self.plus_1_button.Bind(wx.EVT_BUTTON, partial(self.__OnStep, step=1))
        self.plus_3_button.Bind(wx.EVT_BUTTON, partial(self.__OnStep, step=3))
        self.plus_10_button.Bind(wx.EVT_BUTTON, partial(self.__OnStep, step=10))
        self.plus_30_button.Bind(wx.EVT_BUTTON, partial(self.__OnStep, step=30))

        self.cyc_slider.Bind(wx.EVT_SCROLL, self.__OnCycSlider)

        self.cyc_start_text = wx.StaticText(self, label='start-tick:{}'.format(widget_renderer.start_tick))
        self.cyc_start_text.SetForegroundColour(wx.BLUE)
        self.cyc_start_text.Bind(wx.EVT_LEFT_DOWN, self.__OnCycStart)

        self.cyc_end_text = wx.StaticText(self, label='end-tick:{}'.format(widget_renderer.end_tick))
        self.cyc_end_text.SetForegroundColour(wx.BLUE)
        self.cyc_end_text.Bind(wx.EVT_LEFT_DOWN, self.__OnCycEnd)

        curticks = wx.BoxSizer(wx.VERTICAL)
        curticks.Add(self.current_cyc_text, 1, wx.EXPAND)
        curticks.Add(self.current_tick_text, 1, wx.EXPAND)

        row1 = wx.BoxSizer(wx.HORIZONTAL)
        row1.Add(self.clock_combobox, 0, wx.EXPAND)
        row1.Add(curticks, 0, wx.EXPAND)
        row1.AddStretchSpacer(1)

        nav_sizer = wx.BoxSizer(wx.HORIZONTAL)
        nav_sizer.Add(self.minus_30_button, 0, wx.ALIGN_CENTER_VERTICAL)
        nav_sizer.Add(self.minus_10_button, 0, wx.ALIGN_CENTER_VERTICAL)
        nav_sizer.Add(self.minus_3_button, 0, wx.ALIGN_CENTER_VERTICAL)
        nav_sizer.Add(self.minus_1_button, 0, wx.ALIGN_CENTER_VERTICAL)
        nav_sizer.Add(self.plus_1_button, 0, wx.ALIGN_CENTER_VERTICAL)
        nav_sizer.Add(self.plus_3_button, 0, wx.ALIGN_CENTER_VERTICAL)
        nav_sizer.Add(self.plus_10_button, 0, wx.ALIGN_CENTER_VERTICAL)
        nav_sizer.Add(self.plus_30_button, 0, wx.ALIGN_CENTER_VERTICAL)
        row1.Add(nav_sizer)
        row1.AddStretchSpacer(1)

        row2 = wx.BoxSizer(wx.HORIZONTAL)
        row2.Add(self.cyc_start_text, 0, wx.ALIGN_CENTER_VERTICAL | wx.LEFT | wx.RIGHT, 2)
        slider_sizer = wx.BoxSizer(wx.HORIZONTAL)
        slider_sizer.Add(self.cyc_slider, 1, wx.ALIGN_CENTER_VERTICAL)
        row2.Add(slider_sizer, 1, wx.EXPAND)
        row2.Add(self.cyc_end_text, 0, wx.ALIGN_CENTER_VERTICAL | wx.LEFT | wx.RIGHT, 2)

        rows = wx.BoxSizer(wx.VERTICAL)
        rows.Add(row1, 0, wx.EXPAND | wx.TOP, 2)
        rows.Add(wx.StaticLine(self, wx.ID_ANY, style=wx.VERTICAL), 0, wx.EXPAND | wx.TOP | wx.BOTTOM, 4)
        rows.Add(row2, 0, wx.EXPAND)

        self.SetSizer(rows)
        self.Fit()
        self.SetAutoLayout(True)

    def SyncControls(self, tick):
        self.cyc_slider.SetValue(tick)
        self.__UpdateTimeLabels(tick)

    def __UpdateTimeLabels(self, tick):
        selected_clock = self.clock_combobox.GetValue()
        period = self.clock_periods.get(selected_clock)
        if period:
            self.current_cyc_text.SetLabel('cycle:{}'.format(int(tick) // int(period)))
            self.current_tick_text.Show()
        else:
            self.current_cyc_text.SetLabel('tick:{}'.format(tick))
            self.current_tick_text.Hide()
        self.current_tick_text.SetLabel('tick:{}'.format(tick))
        self.Layout()

    def GetCurrentCycle(self):
        if self.clock_combobox.GetValue() == '<any clk edge>':
            raise ValueError('A clock must be selected to get the current cycle')
        return int(self.current_cyc_text.GetLabel().split(':', 1)[1])

    def __UpdateRangeLabels(self):
        period = self.clock_periods.get(self.clock_combobox.GetValue())
        if period:
            label_prefix = 'cycle'
            start_value = int(self.frame.widget_renderer.start_tick) // int(period)
            end_value = int(self.frame.widget_renderer.end_tick) // int(period)
        else:
            label_prefix = 'tick'
            start_value = self.frame.widget_renderer.start_tick
            end_value = self.frame.widget_renderer.end_tick
        self.cyc_start_text.SetLabel('start-{}:{}'.format(label_prefix, start_value))
        self.cyc_end_text.SetLabel('end-{}:{}'.format(label_prefix, end_value))

    def __OnClockSelected(self):
        selected_clock = self.clock_combobox.GetValue()
        if selected_clock == '<any clk edge>' and self.frame.inspector.HasSchedulingLinesWidget():
            wx.MessageBox('Scheduling Lines widget requires a chosen clock', 'Error', wx.OK | wx.ICON_ERROR)
            self.clock_combobox.SetValue(self._selected_clock)
            return

        self._selected_clock = selected_clock
        widget_renderer = self.frame.widget_renderer
        current_tick = widget_renderer.tick
        period = self.clock_periods.get(selected_clock)
        if period:
            period = int(period)
            lower_edge, remainder = divmod(int(current_tick), period)
            if remainder * 2 >= period:
                lower_edge += 1
            current_tick = lower_edge * period
        self.__UpdateRangeLabels()
        widget_renderer.GoToTick(current_tick)

    def GetCurrentViewSettings(self):
        settings = {}
        settings['selected_clock'] = self.clock_combobox.GetValue()
        return settings

    def ApplyViewSettings(self, settings, update_widgets=True):
        selected_clock = settings['selected_clock']
        self.clock_combobox.SetValue(selected_clock)
        self._selected_clock = selected_clock
        widget_renderer = self.frame.widget_renderer
        current_tick = widget_renderer.tick
        period = self.clock_periods.get(selected_clock)
        if period:
            period = int(period)
            lower_edge, remainder = divmod(int(current_tick), period)
            if remainder * 2 >= period:
                lower_edge += 1
            current_tick = lower_edge * period
        self.__UpdateRangeLabels()
        widget_renderer.GoToTick(current_tick, update_widgets)

    def GetCurrentUserSettings(self):
        settings = {}
        settings['current_tick'] = self.cyc_slider.GetValue()
        return settings
    
    def ApplyUserSettings(self, settings, update_widgets=True):
        current_tick = settings['current_tick']
        widget_renderer = self.frame.widget_renderer
        widget_renderer.GoToTick(current_tick, update_widgets)

    def ResetToDefaultViewSettings(self, update_widgets=True):
        widget_renderer = self.frame.widget_renderer
        current_tick = widget_renderer.start_tick
        self.ApplyUserSettings({'current_tick': current_tick}, False)
        self.ApplyViewSettings({'selected_clock': '<any clk edge>'}, update_widgets)

    def __OnStep(self, event, step):
        widget_renderer = self.frame.widget_renderer
        cur_tick = widget_renderer.tick
        period = self.clock_periods.get(self.clock_combobox.GetValue())
        step_ticks = step * int(period) if period else step
        widget_renderer.GoToTick(cur_tick + step_ticks)

    def __OnCycSlider(self, event):
        widget_renderer = self.frame.widget_renderer
        widget_renderer.GoToTick(self.cyc_slider.GetValue())

    def __OnCycStart(self, event):
        widget_renderer = self.frame.widget_renderer
        widget_renderer.GoToStart()

    def __OnCycEnd(self, event):
        widget_renderer = self.frame.widget_renderer
        widget_renderer.GoToEnd()

    @property
    def frame(self):
        return self.GetParent()
