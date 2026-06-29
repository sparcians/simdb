import wx
from viewer.model.dirty_reasons import DIRTY_REASONS

class SaveViewFileDlg(wx.Dialog):
    def __init__(self, prompt='Save to view file?', reasons=None):
        super().__init__(None, title='Save View', size=(550, 200))

        self._reasons = reasons
        panel = wx.Panel(self)
        sizer = wx.BoxSizer(wx.VERTICAL)

        instruction = wx.StaticText(panel, label=prompt)
        sizer.Add(instruction, 0, wx.ALL | wx.CENTER, 10)

        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)

        # Save button
        save_btn = wx.Button(panel, label="Save")
        save_btn.Bind(wx.EVT_BUTTON, lambda event: self.EndModal(wx.ID_YES))
        btn_sizer.Add(save_btn, 0, wx.ALL | wx.CENTER, 5)

        # Do not save button (closes Argos)
        do_not_save_btn = wx.Button(panel, label="Do not save")
        do_not_save_btn.Bind(wx.EVT_BUTTON, lambda event: self.EndModal(wx.ID_NO))
        btn_sizer.Add(do_not_save_btn, 0, wx.ALL | wx.CENTER, 5)

        # Cancel button
        cancel_btn = wx.Button(panel, label="Cancel")
        cancel_btn.Bind(wx.EVT_BUTTON, lambda event: self.EndModal(wx.ID_CANCEL))
        btn_sizer.Add(cancel_btn, 0, wx.ALL | wx.CENTER, 5)

        # "What changed?" button
        what_changed_btn = wx.Button(panel, wx.ID_ANY, label="What changed?")
        what_changed_btn.Bind(wx.EVT_BUTTON, self.__ShowWhatChanged)
        btn_sizer.Add(what_changed_btn, 0, wx.ALL | wx.CENTER, 5)

        sizer.Add(btn_sizer, 0, wx.ALL | wx.RIGHT, 10)
        panel.SetSizer(sizer)

    def __ShowWhatChanged(self, event):
        if self._reasons is None:
            return

        if len(self._reasons) > 1:
            msg = 'The following changes were made to the view:\n\n'
            for i,reason in enumerate(self._reasons):
                msg += str(i) + '.  ' + DIRTY_REASONS[reason] + '\n'
        else:
            msg = DIRTY_REASONS[self._reasons.pop()]

        dlg = wx.MessageDialog(self, msg, 'Changes', wx.OK | wx.ICON_INFORMATION)
        dlg.ShowModal()
        dlg.Destroy()
