import wx

class SummaryViews(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent)

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

