import wx

class SystemwideTools(wx.TreeCtrl):
    def __init__(self, parent, frame):
        super(SystemwideTools, self).__init__(parent, style=wx.TR_DEFAULT_STYLE | wx.TR_LINES_AT_ROOT)
        self.frame = frame

        self._root = self.AddRoot("Systemwide Tools")
        self.AppendItem(self._root, "Queue Utilization")
        self.AppendItem(self._root, "Scheduling Lines")
        self.AppendItem(self._root, "Summary Views")
        self.ExpandAll()
