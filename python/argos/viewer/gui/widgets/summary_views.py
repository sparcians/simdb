import wx, copy
from viewer.gui.view_settings import DirtyReasons

class SummaryViews(wx.Panel):
    def __init__(self, parent, frame):
        super().__init__(parent)
        self.frame = frame
        self.info = None
        self._elem_paths_by_cid = {}
        self.__ShowUsageInfo()

    @property
    def elem_paths(self):
        return list(self._elem_paths_by_cid.values())

    @property
    def cids(self):
        return list(self._elem_paths_by_cid.keys())

    def GetWidgetCreationString(self):
        return 'Summary Views'

    def GetErrorIfDroppedNodeIncompatible(self, elem_path):
        if elem_path in self.elem_paths:
            return 'This collectable is already being displayed.', 'Duplicate Collectable'

        return None

    def UpdateWidgetData(self):
        self.__Refresh()

    def GetCurrentViewSettings(self):
        settings = {}
        settings['elem_paths'] = self.elem_paths
        return settings

    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        dirty = self.elem_paths != settings['elem_paths']
        if not dirty:
            return

        self._elem_paths_by_cid = {
            self.frame.simhier.GetCollectionID(path):path
            for path in settings['elem_paths']
        }

        self.frame.view_settings.SetDirty(reason=DirtyReasons.SummaryViewsWidgetChanged)

    def AddElement(self, elem_path):
        if elem_path in self.elem_paths:
            return

        cid = self.frame.simhier.GetCollectionID(elem_path)
        self._elem_paths_by_cid[cid] = elem_path
        self.__Refresh()

    def __Refresh(self):
        if len(self.elem_paths):
            if self.info:
                self.info.Hide()

            self.SetBackgroundColour('white')

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
