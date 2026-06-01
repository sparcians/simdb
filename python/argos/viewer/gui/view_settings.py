import wx, yaml, os, enum, shutil, tempfile

class DirtyReasons(enum.Enum):
    WidgetDropped = 1
    WidgetSplit = 2
    CanvasExploded = 3
    TabAdded = 4
    TabRenamed = 5
    TabDeleted = 6
    WatchlistAdded = 7
    WatchlistRemoved = 8
    WatchlistOrgChanged = 9
    QueueUtilizDispQueueChanged = 10
    TimeseriesPlotSettingsChanged = 11
    QueueTableDispColsChanged = 12
    QueueTableAutoColorizeChanged = 13
    SchedulingLinesWidgetChanged = 14
    SashPositionChanged = 15
    WidgetRemoved = 16,
    TrackedPacketChanged = 17

DIRTY_REASONS = {
    DirtyReasons.WidgetDropped: 'A widget was dropped onto the widget canvas',
    DirtyReasons.WidgetSplit: 'A widget was split horizontally or vertically',
    DirtyReasons.CanvasExploded: 'Widget canvas was exploded',
    DirtyReasons.TabAdded: 'A new tab was added',
    DirtyReasons.TabRenamed: 'A tab was renamed',
    DirtyReasons.TabDeleted: 'A tab was deleted',
    DirtyReasons.WatchlistAdded: 'Something was added to the Watchlist',
    DirtyReasons.WatchlistRemoved: 'Something was removed from the Watchlist',
    DirtyReasons.WatchlistOrgChanged: 'The Watchlist organization (flat/hier) was changed',
    DirtyReasons.QueueUtilizDispQueueChanged: 'The displayed queues in a Queue Utilization widget were changed',
    DirtyReasons.TimeseriesPlotSettingsChanged: 'Settings were changed for a timeseries plot',
    DirtyReasons.QueueTableDispColsChanged: 'Displayed columns were changed for a Queue Table widget',
    DirtyReasons.QueueTableAutoColorizeChanged: 'Auto-colorize column was changed for a Queue Table widget',
    DirtyReasons.SchedulingLinesWidgetChanged: 'Displayed queues were changed for a Scheduling Lines widget',
    DirtyReasons.SashPositionChanged: 'Widget canvas splitter window sash position changed',
    DirtyReasons.WidgetRemoved: 'A widget was removed from the inspector canvas',
    DirtyReasons.TrackedPacketChanged: 'Changes were made to tracked packet(s)'
}

class ViewSettings:
    def __init__(self):
        self._views_dir = os.getcwd()
        self._view_file = None
        self._frame = None
        self._dirty = False
        self._dirty_reasons = set()
        self._last_known_view = os.path.expanduser('~/.argos/last_known_view.avf')

    @property
    def view_file(self):
        return self._view_file
    
    @view_file.setter
    def view_file(self, view_file):
        self._view_file = view_file
        if view_file not in (None, ''):
            self._views_dir = os.path.dirname(view_file)

        self.__UpdateTitle()

    @property
    def dirty(self):
        return self._dirty

    def SetDirty(self, dirty=True, reason=None):
        self._dirty = dirty
        if dirty and reason is not None:
            assert isinstance(reason, DirtyReasons)
            self._dirty_reasons.add(reason)
        elif not dirty:
            self._dirty_reasons.clear()

        self.__UpdateTitle()

    def PostLoad(self, frame, view_file):
        self._frame = frame
        if view_file:
            self.Load(view_file)
        elif os.path.exists(self._last_known_view):
            self.Load(self._last_known_view, set_as_current=False)
        else:
            self.__ResetDefaultViewSettings()

        self.__ApplyUserSettings()
        self.SetDirty(False)
    
    def Load(self, view_file, set_as_current=True):
        if not os.path.isfile(view_file):
            msg = f"View file '{view_file}' does not exist"
            dlg = wx.MessageDialog(None, msg, 'Error', wx.OK | wx.ICON_ERROR)
            dlg.ShowModal()
            dlg.Destroy()
            return
        
        with open(view_file, 'r') as fin:
            settings = yaml.load(fin, Loader=yaml.FullLoader)
            self._frame.explorer.navtree.ApplyViewSettings(settings['NavTree'])
            self._frame.explorer.watchlist.ApplyViewSettings(settings['Watchlist'])
            self._frame.playback_bar.ApplyViewSettings(settings['PlaybackBar'])
            self._frame.data_retriever.ApplyViewSettings(settings['DataRetriever'])
            self._frame.inspector.ApplyViewSettings(settings['Inspector'])
            self._frame.widget_renderer.ApplyViewSettings(settings['WidgetRenderer'])

        self._frame.inspector.RefreshWidgetsOnAllTabs()
        if set_as_current:
            self.view_file = view_file
        self.SetDirty(False)

    def CreateNewView(self):
        if self._dirty:
            if self.view_file:
                result = self.__AskToSaveChangesToCurrentView("Save changes to '{}'?".format(self.view_file))
                if result == wx.ID_CANCEL:
                    return

                if result == wx.ID_YES:
                    self.SaveView(prompt_if_dirty=False)

                self.__ResetDefaultViewSettings()
            else:
                result = self.__AskToSaveChangesToCurrentView("Save current view to new file before creating a new view?")
                if result == wx.ID_CANCEL:
                    return

                if result == wx.ID_YES:
                    view_file = self.__GetViewFileForSave()
                    if view_file is None:
                        return
                    
                    self.view_file = view_file
                    self.SetDirty(True)
                    self.SaveView(prompt_if_dirty=False)

                self.__ResetDefaultViewSettings()
        else:
            self.__ResetDefaultViewSettings()

    def OpenView(self):
        view_file = self.__GetViewFileForOpen()
        if view_file is None:
            return

        def DoLoad(view_settings, view_file):
            try:
                view_settings.Load(view_file)
            finally:
                wx.EndBusyCursor()

        if view_file == self.view_file:
            msg = f"View file '{os.path.basename(view_file)}' is already open in Argos"
            dlg = wx.MessageDialog(None, msg, 'Error', wx.OK | wx.ICON_ERROR)
            dlg.ShowModal()
            dlg.Destroy()
            return
        
        if self._dirty:
            if self.view_file:
                msg = "Save changes to '{}' before opening '{}'?"
                msg = msg.format(os.path.basename(self.view_file), os.path.basename(view_file))
                result = self.__AskToSaveChangesToCurrentView(msg)
                if result == wx.ID_CANCEL:
                    return

                if result == wx.ID_YES:
                    self.SaveView(prompt_if_dirty=False)

                wx.BeginBusyCursor()
                wx.CallAfter(DoLoad, self, view_file)
            else:
                msg = "Save current view to new file before opening '{}'?"
                msg = msg.format(os.path.basename(view_file))
                result = self.__AskToSaveChangesToCurrentView(msg)
                if result == wx.ID_CANCEL:
                    return

                if result == wx.ID_YES:
                    save_file = self.__GetViewFileForSave()
                    if save_file is None:
                        return

                    self.view_file = save_file
                    self.SetDirty(True)
                    self.SaveView(prompt_if_dirty=False)

                wx.BeginBusyCursor()
                wx.CallAfter(DoLoad, self, view_file)
        else:
            wx.BeginBusyCursor()
            wx.CallAfter(DoLoad, self, view_file)

    def SaveView(self, prompt_if_dirty=True):
        self.__SaveUserSettings()

        if not self._dirty:
            return True

        if self.view_file is None and prompt_if_dirty:
            # Ask the user if they want to save the view to a new file
            dlg = SaveViewFileDlg(prompt="Save current Argos view to a new file?", reasons=self._dirty_reasons)
        elif self.view_file is not None and prompt_if_dirty:
            dlg = SaveViewFileDlg(prompt="Save changes to '{}'?".format(self.view_file), reasons=self._dirty_reasons)
        else:
            dlg = None
            result = wx.ID_YES

        if dlg:
            result = dlg.ShowModal()
            dlg.Destroy()

        if result == wx.ID_CANCEL:
            return False

        if result == wx.ID_YES:
            view_file = self.view_file
            if not view_file:
                assert prompt_if_dirty
                with wx.FileDialog(None, "Save Argos View", wildcard="AVF files (*.avf)|*.avf|All files (*.*)|*.*",
                                   style=wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT, defaultDir=self._views_dir) as dlg:
                    if dlg.ShowModal() == wx.ID_OK:
                        path = dlg.GetPath()
                        if not path.endswith('.avf'):
                            path += '.avf'

                        view_file = path

            # Do not close Argos if the user cancels the save dialog
            if not view_file:
                return False
            
        if result == wx.ID_NO:
            return True

        self.__WriteViewSettings(view_file)
        self.view_file = view_file
        self.SetDirty(False)
        return True

    def OnFrameClosing(self):
        # Returns True if Argos can be closed after calling this method.
        self.__SaveUserSettings()

        if self.view_file is None:
            # Common case: no named AVF in use. Snapshot the current view so the
            # next launch (without --view-file) reloads where the user left off.
            self.__WriteViewSettings(self._last_known_view)
            return True

        # A named AVF is in use. Offer to save changes back to it, then drop any
        # last_known_view so it does not shadow the named view on the next launch.
        if self._dirty:
            result = self.__AskToSaveChangesToCurrentView("Save changes to '{}'?".format(self.view_file))
            if result == wx.ID_CANCEL:
                return False

            if result == wx.ID_YES:
                self.SaveView(prompt_if_dirty=False)

        if os.path.exists(self._last_known_view):
            os.remove(self._last_known_view)

        return True

    def __WriteViewSettings(self, view_file):
        settings = {
            'NavTree': self._frame.explorer.navtree.GetCurrentViewSettings(),
            'Watchlist': self._frame.explorer.watchlist.GetCurrentViewSettings(),
            'PlaybackBar': self._frame.playback_bar.GetCurrentViewSettings(),
            'DataRetriever': self._frame.data_retriever.GetCurrentViewSettings(),
            'Inspector': self._frame.inspector.GetCurrentViewSettings(),
            'WidgetRenderer': self._frame.widget_renderer.GetCurrentViewSettings()
        }

        settings_dir = os.path.dirname(view_file)
        if settings_dir and not os.path.exists(settings_dir):
            os.makedirs(settings_dir)

        with open(view_file, 'w') as fout:
            yaml.dump(settings, fout)

    def __UpdateTitle(self):
        if self._frame is None:
            return

        view_file = self.view_file
        dirty = self._dirty

        if view_file is None:
            view_file = 'unnamed'

        title = 'Argos Viewer: %s' % os.path.basename(view_file)
        if dirty:
            title += '*'

        self._frame.SetTitle(title)

    def __SaveUserSettings(self):
        settings_dir = os.path.expanduser('~/.argos')
        if not os.path.exists(settings_dir):
            os.makedirs(settings_dir)
        
        settings_file = os.path.join(settings_dir, 'user_settings.yaml')

        settings = {
            'NavTree': self._frame.explorer.navtree.GetCurrentUserSettings(),
            'Watchlist': self._frame.explorer.watchlist.GetCurrentUserSettings(),
            'PlaybackBar': self._frame.playback_bar.GetCurrentUserSettings(),
            'DataRetriever': self._frame.data_retriever.GetCurrentUserSettings(),
            'Inspector': self._frame.inspector.GetCurrentUserSettings(),
            'WidgetRenderer': self._frame.widget_renderer.GetCurrentUserSettings()
        }

        with open(settings_file, 'w') as fout:
            yaml.dump(settings, fout)

    def __ApplyUserSettings(self):
        settings_dir = os.path.expanduser('~/.argos')
        if not os.path.exists(settings_dir):
            return
        
        settings_file = os.path.join(settings_dir, 'user_settings.yaml')
        if not os.path.exists(settings_file):
            return
        
        # Delete the user settings file on any exception. This typically happens
        # when the simulation hierarchy changes from one DB to the next. We don't
        # have a good way to partially apply the settings, so we just delete the
        # settings file and start over.
        try:
            with open(settings_file, 'r') as fin:
                settings = yaml.load(fin, Loader=yaml.FullLoader)
                self._frame.explorer.navtree.ApplyUserSettings(settings['NavTree'])
                self._frame.explorer.watchlist.ApplyUserSettings(settings['Watchlist'])
                self._frame.playback_bar.ApplyUserSettings(settings['PlaybackBar'])
                self._frame.data_retriever.ApplyUserSettings(settings['DataRetriever'])
                self._frame.inspector.ApplyUserSettings(settings['Inspector'])
                self._frame.widget_renderer.ApplyUserSettings(settings['WidgetRenderer'])
        except Exception as ex:
            print (f"Error loading user settings. Deleting settings file. Error: '{ex}'")
            os.remove(settings_file)

    def __AskToSaveChangesToCurrentView(self, prompt):
        dlg = SaveViewFileDlg(prompt=prompt, reasons=self._dirty_reasons)
        result = dlg.ShowModal()
        dlg.Destroy()
        return result
    
    def __ResetDefaultViewSettings(self):
        self._frame.explorer.navtree.ResetToDefaultViewSettings(False)
        self._frame.explorer.watchlist.ResetToDefaultViewSettings(False)
        self._frame.playback_bar.ResetToDefaultViewSettings(False)
        self._frame.data_retriever.ResetToDefaultViewSettings(False)
        self._frame.inspector.ResetToDefaultViewSettings(False)
        self._frame.widget_renderer.ResetToDefaultViewSettings(False)
        self._frame.inspector.RefreshWidgetsOnAllTabs()

        self.view_file = None
        self.SetDirty(False)

    def __GetViewFileForOpen(self):
        with wx.FileDialog(self._frame, "Open file", defaultDir=self._views_dir, 
                    wildcard="AVF files (*.avf)|*.avf|All files (*.*)|*.*",
                    style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST) as dlg:
            if dlg.ShowModal() == wx.ID_OK:
                return dlg.GetPath()
            else:
                return None
            
    def __GetViewFileForSave(self):
        with wx.FileDialog(None, "Save Argos View", wildcard="AVF files (*.avf)|*.avf|All files (*.*)|*.*",
                           style=wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT, defaultDir=self._views_dir) as dlg:
            if dlg.ShowModal() == wx.ID_OK:
                path = dlg.GetPath()
                if not path.endswith('.avf'):
                    path += '.avf'

                return path

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
