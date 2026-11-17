import wx
from viewer.gui.font_settings import FontSettings

class FontSettingsDlg(wx.Dialog):
    def __init__(self, parent, sizes):
        super().__init__(parent, title='Fonts')

        self._spinners = {}
        sizer = wx.BoxSizer(wx.VERTICAL)

        for group_name, roles in FontSettings.GROUPS:
            box = wx.StaticBoxSizer(wx.VERTICAL, self, group_name)
            grid = wx.FlexGridSizer(cols=2, vgap=5, hgap=10)
            grid.AddGrowableCol(1)
            for role, label, _ in roles:
                spinner = wx.SpinCtrl(box.GetStaticBox(), min=FontSettings.MIN_SIZE, max=FontSettings.MAX_SIZE,
                                      initial=sizes[role])
                self._spinners[role] = spinner
                grid.Add(wx.StaticText(box.GetStaticBox(), label=label), 0, wx.ALIGN_CENTER_VERTICAL)
                grid.Add(spinner, 0, wx.ALIGN_RIGHT)

            # The grid must fill the box for the growable column to push the spinners to the right edge.
            box.Add(grid, 1, wx.EXPAND | wx.ALL, 5)
            sizer.Add(box, 0, wx.EXPAND | wx.ALL, 5)

        sizer.Add(self.CreateStdDialogButtonSizer(wx.OK | wx.CANCEL), 0, wx.ALIGN_RIGHT | wx.ALL, 10)
        self.SetSizerAndFit(sizer)
        self.CentreOnParent()

    def GetSizes(self):
        return {role: spinner.GetValue() for role, spinner in self._spinners.items()}
