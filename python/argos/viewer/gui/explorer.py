import wx
from viewer.gui.collectable_tree import QueuesTree, ScalarsTree
from viewer.gui.tools import SystemwideTools

class DataExplorer(wx.Notebook):
    def __init__(self, parent, frame):
        super(DataExplorer, self).__init__(parent, style=wx.NB_LEFT)
        self.frame = frame
        self.queues_tree = QueuesTree(self, frame)
        self.scalars_tree = ScalarsTree(self, frame)
        self.tools = SystemwideTools(self, frame)

        self.AddPage(self.queues_tree, "Queues")
        self.AddPage(self.scalars_tree, "Scalars")
        self.AddPage(self.tools, "Tools")

        self.SetMinSize((200, 200))
