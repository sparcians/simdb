import wx
import wx.lib.buttons

def CreateButton(*args, **kwargs):
    appearance = wx.SystemSettings.GetAppearance()
    if appearance.IsDark():
        btn = wx.lib.buttons.GenButton(*args, **kwargs)
    else:
        btn = wx.Button(*args, **kwargs)

    btn.SetForegroundColour(wx.BLACK)
    btn.SetBackgroundColour(wx.WHITE)
    return btn
