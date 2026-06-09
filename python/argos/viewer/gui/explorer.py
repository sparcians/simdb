import wx
from viewer.gui.navtree import NavTree
from viewer.gui.tools import SystemwideTools

class DataExplorer(wx.Notebook):
    def __init__(self, parent, frame):
        super(DataExplorer, self).__init__(parent, style=wx.NB_LEFT)
        self.frame = frame
        self.navtree = NavTree(self, frame)
        self.tools = SystemwideTools(self, frame)

        self.AddPage(self.navtree, "NavTree")
        self.AddPage(self.tools, "Tools")

        self.SetMinSize((200, 200))
