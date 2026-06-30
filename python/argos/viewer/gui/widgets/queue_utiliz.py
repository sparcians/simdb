import wx, copy
from viewer.gui.dialogs.widget_data_selections import WidgetDataSelectionsDlg
from viewer.gui.view_settings import DirtyReasons

class QueueUtilizWidget(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent, size=(800, 500))
        self.SetBackgroundColour('white')
        self.frame = frame

        # Get all container sim paths from the simhier
        self.container_elem_paths = self.frame.simhier.GetContainerElemPaths()
        self.container_elem_paths.sort()

        gear_btn, clear_btn, split_lr, split_tb, maximize_btn = frame.CreateWidgetStandardButtons(
            self, self.__EditWidget, 'Edit data selections')

        # The layout of this widget is like a barchart:
        #
        # sim.path.foo    [28%  XXXXXXXXXX                           ]
        # sim.path.bar    [15%  XXXX                                 ]
        # sim.path.fizz   [19%  XXXXXX                               ]
        # sim.path.buzz   [3%   X                                    ]
        # sim.path.fizbuz [15%  XXXXX                                ]
        #
        # Where the X's above are shown as a colored heatmap based on the
        # utilization percentage of each queue.
        self.panel = wx.ScrolledWindow(self, style=wx.VSCROLL | wx.HSCROLL)
        self.panel.SetScrollRate(10, 10)
        self._elem_path_text_boxes = []
        self._utiliz_bars = []
        self.__LayoutComponents()

        sizer = wx.BoxSizer(wx.VERTICAL)
        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        btn_sizer.Add(gear_btn, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(clear_btn, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(split_lr, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(split_tb, 0, wx.TOP | wx.RIGHT, 5)
        btn_sizer.Add(maximize_btn, 0, wx.TOP, 5)
        sizer.Add(btn_sizer, 0, wx.BOTTOM | wx.LEFT | wx.TOP, 5)
        sizer.Add(self.panel, 1, wx.EXPAND | wx.LEFT, 5)
        self.SetSizer(sizer)

        self.panel.FitInside()
        self.Layout()

    def GetWidgetCreationString(self):
        return 'Queue Utilization'

    def GetErrorIfDroppedNodeIncompatible(self, elem_path):
        return None

    def AddElement(self, elem_path):
        pass

    def UpdateWidgetData(self):
        for elem_path, pct_bar in zip(self.container_elem_paths, self._utiliz_bars):
            utiliz_pct = self.frame.widget_renderer.utiliz_handler.GetUtilizPct(elem_path)
            pct_bar.UpdateUtilizPct(utiliz_pct)

    def GetCurrentViewSettings(self):
        settings = {}
        settings['displayed_elem_paths'] = self.container_elem_paths
        return settings
    
    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        paths1 = set(self.container_elem_paths)
        paths2 = set(settings['displayed_elem_paths'])
        if paths1 == paths2:
            return
        
        self.container_elem_paths = copy.deepcopy(settings['displayed_elem_paths'])
        self.container_elem_paths.sort()
        self.__LayoutComponents()
        self.UpdateWidgetData()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.QueueUtilizDispQueueChanged)

    def __OnSimElemInitDrag(self, event):
        text_elem = event.GetEventObject()

        if text_elem in self._elem_path_text_boxes:
            if text_elem.HasCapture():
                text_elem.ReleaseMouse()
            else:
                text_elem.CaptureMouse()
                data = wx.TextDataObject('IterableStruct$' + text_elem.GetLabel())
                drag_source = wx.DropSource(text_elem)
                drag_source.SetData(data)
                drag_source.DoDragDrop(wx.Drag_DefaultMove)
                text_elem.ReleaseMouse()

            event.Skip()

    def __EditWidget(self, event):
        dlg = WidgetDataSelectionsDlg(
            self, self.frame, self.container_elem_paths, queues_only=True,
        )
        if dlg.ShowModal() == wx.ID_OK:
            self.ApplyViewSettings({'displayed_elem_paths': dlg.GetSelectedElemPaths()})

        dlg.Destroy()

    def __LayoutComponents(self):
        had_sizer = self.panel.GetSizer() is not None
        if had_sizer:
            sizer = self.panel.GetSizer()
            for elem_path, pct_bar in zip(self._elem_path_text_boxes, self._utiliz_bars):
                sizer.Detach(elem_path)
                sizer.Detach(pct_bar)
                elem_path.Destroy()
                pct_bar.Destroy()

            sizer.Clear()
            self._elem_path_text_boxes = []
            self._utiliz_bars = []
        else:
            sizer = wx.FlexGridSizer(2, 0, 10)
            sizer.AddGrowableCol(1)
            assert len(self._elem_path_text_boxes) == 0
            assert len(self._utiliz_bars) == 0

        self._elem_path_text_boxes = [wx.StaticText(self.panel, label=elem_path) for elem_path in self.container_elem_paths]
        self._utiliz_bars = [UtilizBar(self.panel, self.frame) for _ in range(len(self.container_elem_paths))]

        for elem_path, utiliz_bar in zip(self._elem_path_text_boxes, self._utiliz_bars):
            sizer.Add(elem_path, 1, wx.EXPAND)
            sizer.Add(utiliz_bar, 1, wx.EXPAND)

        for text_elem in self._elem_path_text_boxes:
            text_elem.Bind(wx.EVT_LEFT_DOWN, self.__OnSimElemInitDrag)

        font = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        for elem in self._elem_path_text_boxes:
            elem.SetFont(font)

        if not had_sizer:
            self.panel.SetSizer(sizer)

        self.Layout()
        self.Refresh()

class UtilizBar(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent, size=(300, 10))
        self.frame = frame
        self.static_text = wx.StaticText(self, label='0%')
        font = wx.Font(8, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        self.static_text.SetFont(font)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(self.static_text, 1, wx.ALIGN_LEFT | wx.EXPAND)
        self.SetSizer(sizer)

    def UpdateUtilizPct(self, utiliz_pct):
        self.static_text.SetLabel('{}%'.format(round(utiliz_pct * 100)))
        color = self.frame.widget_renderer.utiliz_handler.ConvertUtilizPctToColor(utiliz_pct)
        self.SetBackgroundColour(color)
        self.static_text.SetBackgroundColour(color)
        
        height = self.GetSize().GetHeight()
        width = round(utiliz_pct * 500)
        if width == 0:
            assert color == (255, 255, 255)
            width = 500

        self.SetSize((width, height))
