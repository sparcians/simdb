import wx


def ShowCopyPathMenu(window, path):
    menu = wx.Menu()
    copy_path_item = menu.Append(wx.ID_ANY, 'Copy path')

    def CopyPath(event):
        if wx.TheClipboard.Open():
            try:
                wx.TheClipboard.SetData(wx.TextDataObject(path))
                wx.TheClipboard.Flush()
            finally:
                wx.TheClipboard.Close()

    menu.Bind(wx.EVT_MENU, CopyPath, copy_path_item)
    try:
        window.PopupMenu(menu)
    finally:
        menu.Destroy()


def BindCopyPathMenu(window, path):
    window.Bind(wx.EVT_CONTEXT_MENU, lambda event: ShowCopyPathMenu(window, path))