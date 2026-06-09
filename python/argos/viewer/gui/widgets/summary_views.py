import wx

class SummaryViews(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent)

        self.info = wx.StaticText(self, label='Drag queues from the NavTree to create scheduling lines.', size=(600,18))
        self.info.SetFont(wx.Font(14, wx.FONTFAMILY_DEFAULT, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL))

        vsizer = wx.BoxSizer(wx.VERTICAL)
        vsizer.AddStretchSpacer()
        vsizer.Add(self.info, 1, wx.ALL | wx.CENTER | wx.EXPAND, 5)
        vsizer.AddStretchSpacer()

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

