import wx

class DefaultStaticText(wx.StaticText):
    def __init__(self, *args, **kwargs):
        wx.StaticText.__init__(self, *args, **kwargs)
        self.SetForegroundColour(wx.BLACK)
        self.SetBackgroundColour(wx.WHITE)
