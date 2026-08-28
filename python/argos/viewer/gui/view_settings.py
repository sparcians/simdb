import wx, yaml, os, sqlite3, tempfile
from viewer.model.dirty_reasons import DirtyReasons
from viewer.gui.dialogs.save_view_file import SaveViewFileDlg
from viewer.gui.dialogs.save_view_choice import SaveViewChoiceDlg, SAVE_AS_AVF, SAVE_AS_DB

LAST_KNOWN_VIEW_TABLE = 'ArgosLastKnownView'

class ViewSettings:
    def __init__(self):
        self._views_dir = os.getcwd()
        self._view_file = None
        self._frame = None
        self._dirty = False
        self._dirty_reasons = set()

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
            self.Load(view_file) # Not dirty
        elif not self.__LoadLastKnownView():
            self.__ResetDefaultViewSettings()

        self.__ApplyUserSettings()
    
    def Load(self, view_file, set_as_current=True):
        if not os.path.isfile(view_file):
            msg = f"View file '{view_file}' does not exist"
            dlg = wx.MessageDialog(None, msg, 'Error', wx.OK | wx.ICON_ERROR)
            dlg.ShowModal()
            dlg.Destroy()
            return

        try:
            with open(view_file, 'r') as fin:
                settings = yaml.load(fin, Loader=yaml.FullLoader)
                self.__ApplyViewSettings(settings)
        except Exception as ex:
            print (f"Error loading view file '{view_file}': '{ex}'")
            self.__ResetDefaultViewSettings()
            set_as_current = False

        self._frame.inspector.RefreshWidgetsOnAllTabs()
        if set_as_current:
            self.view_file = view_file
        self.SetDirty(False)

    def __ApplyViewSettings(self, settings):
        db = self._frame.db
        simhier = self._frame.simhier
        dtype_inspector = self._frame.dtype_inspector

        load_errors = []
        self._frame.playback_bar.ValidateViewSettings(settings['PlaybackBar'], db, simhier, dtype_inspector, load_errors)
        self._frame.data_retriever.ValidateViewSettings(settings['DataRetriever'], db, simhier, dtype_inspector, load_errors)
        self._frame.inspector.ValidateViewSettings(settings['Inspector'], db, simhier, dtype_inspector, load_errors)
        self._frame.widget_renderer.ValidateViewSettings(settings['WidgetRenderer'], db, simhier, dtype_inspector, load_errors)

        if load_errors:
            title = 'Error ' if len(load_errors) == 1 else 'Errors '
            title += 'Loading View'
            msg = '\n\n-'.join(load_errors)
            wx.MessageBox(msg, title, wx.OK | wx.ICON_ERROR)
            raise RuntimeError(msg)

        self._frame.playback_bar.ApplyViewSettings(settings['PlaybackBar'])
        self._frame.data_retriever.ApplyViewSettings(settings['DataRetriever'])
        self._frame.inspector.ApplyViewSettings(settings['Inspector'])
        self._frame.widget_renderer.ApplyViewSettings(settings['WidgetRenderer'])
        self._frame.inspector.RefreshWidgetsOnAllTabs()

    def __GetViewSettings(self):
        return {
            'PlaybackBar': self._frame.playback_bar.GetCurrentViewSettings(),
            'DataRetriever': self._frame.data_retriever.GetCurrentViewSettings(),
            'Inspector': self._frame.inspector.GetCurrentViewSettings(),
            'WidgetRenderer': self._frame.widget_renderer.GetCurrentViewSettings()
        }

    def __LoadLastKnownView(self):
        try:
            cursor = self._frame.db.cursor()
            cursor.execute('SELECT ViewYaml FROM {} WHERE Id = 1'.format(LAST_KNOWN_VIEW_TABLE))
            row = cursor.fetchone()
            if row is None:
                return False

            settings = yaml.safe_load(row[0])
            self.__ApplyViewSettings(settings)
            self.view_file = None
            self.SetDirty(False)
            return True
        except Exception as ex:
            return False

    def __SaveViewToDatabase(self):
        settings_yaml = yaml.safe_dump(self.__GetViewSettings())
        with tempfile.NamedTemporaryFile(mode='w', suffix='.avf', prefix='argos_view_',
                                         dir=tempfile.gettempdir(), delete=False) as fout:
            fout.write(settings_yaml)

        cursor = self._frame.db.cursor()
        cursor.execute('CREATE TABLE IF NOT EXISTS {} ('
                       'Id INTEGER PRIMARY KEY CHECK (Id = 1), '
                       'ViewYaml TEXT NOT NULL)'.format(LAST_KNOWN_VIEW_TABLE))
        cursor.execute('INSERT OR REPLACE INTO {} (Id, ViewYaml) VALUES (1, ?)'
                       .format(LAST_KNOWN_VIEW_TABLE), (settings_yaml,))
        self._frame.db.commit()

    def __SaveViewChoice(self):
        dlg = SaveViewChoiceDlg()
        result = dlg.ShowModal()
        dlg.Destroy()
        return result

    def CreateNewView(self):
        if self._dirty:
            if self.view_file:
                result = self.__AskToSaveChangesToCurrentView("Save changes to '{}'?".format(os.path.basename(self.view_file)))
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

        if isinstance(self.view_file, str) and os.path.abspath(view_file) == os.path.abspath(self.view_file):
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
            dlg = SaveViewFileDlg(prompt="Save changes to '{}'?".format(os.path.basename(self.view_file)), reasons=self._dirty_reasons)
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

        self.__WriteViewSettings(view_file)
        self.view_file = view_file
        self.SetDirty(False)
        return True

    def OnFrameClosing(self):
        # Returns True if Argos can be closed after calling this method.
        self.__SaveUserSettings()

        if self.view_file is None:
            if not self._dirty:
                return True

            result = self.__SaveViewChoice()
            if result == wx.ID_CANCEL:
                return False

            if result == SAVE_AS_AVF:
                return self.SaveView()

            if result == SAVE_AS_DB:
                self.__SaveViewToDatabase()
                self.SetDirty(False)

            return True

        # A named AVF is in use. Offer to save changes back to it.
        if self._dirty:
            result = self.__AskToSaveChangesToCurrentView("Save changes to '{}'?".format(os.path.basename(self.view_file)))
            if result == wx.ID_CANCEL:
                return False

            if result == wx.ID_YES:
                if not self.SaveView(prompt_if_dirty=False):
                    return False

        return True

    def __WriteViewSettings(self, view_file):
        settings = self.__GetViewSettings()

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
                self._frame.playback_bar.ApplyUserSettings(settings['PlaybackBar'])
                self._frame.data_retriever.ApplyUserSettings(settings['DataRetriever'])
                self._frame.inspector.ApplyUserSettings(settings['Inspector'])
                self._frame.widget_renderer.ApplyUserSettings(settings['WidgetRenderer'])
        except Exception as ex:
            print (f"Error loading user settings. Deleting settings file. Error: '{ex}'")
            os.remove(settings_file)
            self.__ResetDefaultViewSettings()

    def __AskToSaveChangesToCurrentView(self, prompt):
        dlg = SaveViewFileDlg(prompt=prompt, reasons=self._dirty_reasons)
        result = dlg.ShowModal()
        dlg.Destroy()
        return result
    
    def __ResetDefaultViewSettings(self):
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
