import wx


SAVE_AS_AVF = wx.NewIdRef()
SAVE_AS_DB = wx.NewIdRef()


class SaveViewChoiceDlg(wx.Dialog):
    def __init__(self):
        super().__init__(None, title='Save Layout')

        sizer = wx.BoxSizer(wx.VERTICAL)
        instruction = wx.StaticText(self, label='Save layout?')
        instruction.Wrap(400)
        sizer.Add(instruction, 0, wx.ALL | wx.ALIGN_CENTER, 10)

        button_sizer = wx.BoxSizer(wx.HORIZONTAL)
        for label, result in (('.alf', SAVE_AS_AVF), ('.db', SAVE_AS_DB),
                              ('No', wx.ID_NO), ('Cancel', wx.ID_CANCEL)):
            button = wx.Button(self, label=label)
            button.Bind(wx.EVT_BUTTON, lambda event, result=result: self.EndModal(result))
            button_sizer.Add(button, 0, wx.ALL, 5)

        sizer.Add(button_sizer, 0, wx.ALIGN_CENTER | wx.ALL, 10)
        self.SetSizer(sizer)
        self.Fit()
        self.Layout()