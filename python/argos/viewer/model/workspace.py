import wx
from viewer.model.frame import ArgosFrame
from viewer.gui.view_settings import ViewSettings

class Workspace:
    def __init__(self, db_path, view_file):
        self._view_settings = ViewSettings()
        self._frame = ArgosFrame(db_path, self._view_settings)
        self._frame.PostLoad(view_file)
        self._frame.Show()
        self._frame.Bind(wx.EVT_CLOSE, self.__OnCloseFrame)

    def __OnCloseFrame(self, event):
        if self._view_settings.OnFrameClosing():
            self._frame.Unbind(wx.EVT_CLOSE)
            self._frame.Destroy()
