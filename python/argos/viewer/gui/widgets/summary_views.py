import wx

class SummaryViews(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent)
        self.info = None
        self.__ShowUsageInfo()

    def GetWidgetCreationString(self):
        return 'Summary Views'

    def GetErrorIfDroppedNodeIncompatible(self, elem_path):
        return None

    def UpdateWidgetData(self):
        pass

    def GetCurrentViewSettings(self):
        return {}

    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        pass

    def __ShowUsageInfo(self):
        if self.info:
            self.info.Destroy()
            self.info = None

        if self.GetSizer():
            self.GetSizer().Clear()

        self.SetBackgroundColour('light gray')

        self.info = wx.StaticText(self, label='Drag collectables from the Queues/Scalars tree to create summary views.')#, size=(750,18))
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
