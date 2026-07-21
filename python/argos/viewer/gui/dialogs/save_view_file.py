import wx
from viewer.model.dirty_reasons import DIRTY_REASONS

class SaveViewFileDlg(wx.Dialog):
    def __init__(self, prompt='Save to view file?', reasons=None, include_skip_btn=False):
        super().__init__(None, title='Save View')

        self._reasons = set(reasons) if reasons else None
        sizer = wx.BoxSizer(wx.VERTICAL)
        instruction = wx.StaticText(self, label=prompt)
        instruction.Wrap(400)
        sizer.Add(instruction, 0, wx.ALL | wx.ALIGN_CENTER, 10)

        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)

        # Save button
        save_btn = wx.Button(self, label="Save")
        save_btn.Bind(wx.EVT_BUTTON, lambda event: self.EndModal(wx.ID_YES))
        btn_sizer.Add(save_btn, 0, wx.ALL, 5)

        # Skip button
        if include_skip_btn:
            skip_btn = wx.Button(self, label="Skip")
            skip_btn.Bind(wx.EVT_BUTTON, lambda event: self.EndModal(wx.ID_NO))
            btn_sizer.Add(skip_btn, 0, wx.ALL, 5)

        # Cancel button
        cancel_btn = wx.Button(self, label="Cancel")
        cancel_btn.Bind(wx.EVT_BUTTON, lambda event: self.EndModal(wx.ID_CANCEL))
        btn_sizer.Add(cancel_btn, 0, wx.ALL, 5)

        # "What changed?" button
        if self._reasons:
            what_changed_btn = wx.Button(self, wx.ID_ANY, label="What changed?")
            what_changed_btn.Bind(wx.EVT_BUTTON, self.__ShowWhatChanged)
            btn_sizer.Add(what_changed_btn, 0, wx.ALL, 5)

        sizer.Add(btn_sizer, 0, wx.ALIGN_CENTER | wx.ALL, 10)
        self.SetSizer(sizer)
        self.Fit()
        self.Layout()

    def __ShowWhatChanged(self, event):
        if self._reasons is None:
            return

        if len(self._reasons) > 1:
            msg = 'The following changes were made to the view:\n\n'
            for i, reason in enumerate(self._reasons):
                msg += str(i) + '.  ' + DIRTY_REASONS[reason] + '\n'
        else:
            reason = next(iter(self._reasons))
            msg = DIRTY_REASONS[reason]

        dlg = wx.MessageDialog(self, msg, 'Changes', wx.OK | wx.ICON_INFORMATION)
        dlg.ShowModal()
        dlg.Destroy()
