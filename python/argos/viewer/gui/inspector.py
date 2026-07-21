import re
import wx
import wx.aui
from viewer.gui.canvas_grid import CanvasGrid
from viewer.gui.logs import CollectionLogs
from viewer.gui.view_settings import DirtyReasons
from contextlib import contextmanager
from functools import partial

class DataInspector(wx.aui.AuiNotebook):
    def __init__(self, parent, frame):
        super(DataInspector, self).__init__(parent, style=wx.aui.AUI_NB_TOP | wx.aui.AUI_NB_SCROLL_BUTTONS)

        self.frame = frame
        self.tabs = []

        # AuiNotebook (unlike wx.Notebook) emits EVT_AUINOTEBOOK_PAGE_CHANGED
        # during programmatic page mutations (e.g. DeletePage/InsertPage). This
        # guard ensures the "Add Tab" dialog only opens on a genuine user click
        # of the plus tab, not while we are rebuilding tabs ourselves.
        self.__suppress_add_tab_dialog = False

        # The "Logs" tab is a fixed, non-editable page pinned to index 0 when
        # present. It is not part of self.tabs (the user-managed CanvasGrid
        # tabs), so page index math below offsets past it when it exists.
        self.__has_logs_tab = False
        self.__AddPlusTab()
        if self.__HasNotifications():
            self.__AddLogsTab()
        self.__AddInspectorTab("Tab 1")
        self.__SelectDefaultTab()
        self.SetMinSize((200, 200))

        self.Bind(wx.aui.EVT_AUINOTEBOOK_PAGE_CHANGED, self.__OnPageChanged)
        self.Bind(wx.aui.EVT_AUINOTEBOOK_TAB_RIGHT_DOWN, self.__OnContextMenu)

    @contextmanager
    def __SuppressAddTabDialog(self):
        # Save/restore (rather than a plain True/False) so nested mutations,
        # e.g. ApplyViewSettings calling __AddInspectorTab, behave correctly.
        prev = self.__suppress_add_tab_dialog
        self.__suppress_add_tab_dialog = True
        try:
            yield
        finally:
            self.__suppress_add_tab_dialog = prev

    def GetCurrentTabWidgetContainers(self):
        tab_idx = self.__SelectedTabIndex()
        if tab_idx is None:
            return None

        return self.tabs[tab_idx].GetWidgetContainers()
    
    def ResetCurrentTab(self):
        tab_idx = self.__SelectedTabIndex()
        if tab_idx is None:
            return

        self.tabs[tab_idx].ResetLayout()

    def GetCurrentViewSettings(self):
        settings = {}
        # Skip the fixed "Logs" tab (when present) and the trailing "Add Tab" page.
        settings['tab_names'] = [self.GetPageText(i) for i in range(self.__FirstUserTabIndex(), self.GetPageCount() - 1)]
        settings['tab_settings'] = [tab.GetCurrentViewSettings() for tab in self.tabs]
        return settings
    
    def ApplyViewSettings(self, settings):
        with self.__SuppressAddTabDialog():
            # Reset all tabs
            for tab in self.tabs:
                tab.ResetLayout()

            self.tabs = []

            # Delete all user tabs, preserving the fixed "Logs" tab (when present)
            # and the trailing "Add Tab" page.
            while self.GetPageCount() > self.__FirstUserTabIndex() + 1:
                self.DeletePage(self.__FirstUserTabIndex())

            # Add new tabs
            for tab_name in settings['tab_names']:
                self.__AddInspectorTab(tab_name)

            # Apply settings to each tab
            if 'tab_settings' in settings:
                for i, tab_settings in enumerate(settings['tab_settings']):
                    self.tabs[i].ApplyViewSettings(tab_settings)

    def GetCurrentUserSettings(self):
        settings = {}
        settings['selected_tab'] = self.GetPageText(self.GetSelection())
        return settings

    def ApplyUserSettings(self, settings):
        selected_tab = settings.get('selected_tab')
        if not selected_tab:
            self.__SelectDefaultTab()
            return

        # "Tab 1" was the historical default before the Logs tab existed.
        if self.__has_logs_tab and selected_tab == 'Tab 1':
            self.__SelectDefaultTab()
            return

        for i in range(self.GetPageCount() - 1):
            if self.GetPageText(i) == selected_tab:
                self.SetSelection(i)
                return

        self.__SelectDefaultTab()

    def ResetToDefaultViewSettings(self, update_widgets=True):
        self.ApplyViewSettings({'tab_names': ['Tab 1']})
        self.__SelectDefaultTab()

    def RefreshWidgetsOnCurrentTab(self):
        tab_idx = self.__SelectedTabIndex()
        if tab_idx is None:
            return

        self.tabs[tab_idx].UpdateWidgets()
        self.tabs[tab_idx].Layout()
        self.tabs[tab_idx].Refresh()

    def RefreshWidgetsOnAllTabs(self):
        for tab in self.tabs:
            tab.UpdateWidgets()
            tab.Layout()
            tab.Refresh()

    def __GetDefaultTabName(self):
        # Suggest a "Tab N" name that does not collide with any existing tab.
        # N is one past the highest existing "Tab N" number, but at least
        # (num_tabs + 1) so the suggestion keeps growing as tabs are added.
        highest = 0
        for i in range(self.GetPageCount() - 1):
            match = re.fullmatch(r"Tab (\d+)", self.GetPageText(i))
            if match:
                highest = max(highest, int(match.group(1)))

        return "Tab %d" % max(len(self.tabs) + 1, highest + 1)

    def __HasNotifications(self):
        cursor = self.frame.db.cursor()
        cursor.execute("SELECT COUNT(*) FROM Notifications")
        return cursor.fetchone()[0] > 0

    def __FirstUserTabIndex(self):
        return 1 if self.__has_logs_tab else 0

    def __SelectDefaultTab(self):
        # Index 0 is the Logs tab when present, otherwise the first user tab.
        self.SetSelection(0)

    def __SelectedTabIndex(self):
        # Map the currently selected page to an index into self.tabs, or None
        # when the selection is the fixed "Logs" tab or the "Add Tab" page.
        selected_page = self.GetSelection()
        if selected_page == self.GetPageCount() - 1:
            return None
        if self.__has_logs_tab and selected_page == 0:
            return None

        return selected_page - self.__FirstUserTabIndex()

    def __AddPlusTab(self):
        super(DataInspector, self).AddPage(wx.Panel(self), "Add Tab")

    def __AddLogsTab(self):
        with self.__SuppressAddTabDialog():
            self.logs = CollectionLogs(self, self.frame)
            super(DataInspector, self).InsertPage(0, self.logs, "Logs")
            self.__has_logs_tab = True

    def __AddInspectorTab(self, name):
        with self.__SuppressAddTabDialog():
            canvas_grid = CanvasGrid(self)
            super(DataInspector, self).InsertPage(self.GetPageCount() - 1, canvas_grid, name)
            self.tabs.append(canvas_grid)
            self.SetSelection(self.GetPageCount() - 2)

    def __OnPageChanged(self, event):
        new_page_index = event.GetSelection()
        if not self.__suppress_add_tab_dialog and new_page_index == self.GetPageCount() - 1:
            self.__ShowAddTabDialog()
        else:
            self.RefreshWidgetsOnCurrentTab()

        event.Skip()

    def __ShowAddTabDialog(self):
        dlg = wx.TextEntryDialog(self, "Enter name for the new tab:", "New Tab", value=self.__GetDefaultTabName())

        if dlg.ShowModal() == wx.ID_OK:
            new_tab_name = dlg.GetValue().strip()
            if new_tab_name:
                for i in range(self.GetPageCount() - 1):
                    if self.GetPageText(i) == new_tab_name:
                        wx.MessageBox("A tab with that name already exists.", "Error", wx.OK | wx.ICON_ERROR)
                        self.SetSelection(self.GetPageCount() - 2)
                        return

                self.__AddInspectorTab(new_tab_name)
                self.frame.view_settings.SetDirty(reason=DirtyReasons.TabAdded)
        else:
            self.SetSelection(self.GetPageCount() - 2)

        dlg.Destroy()

    def __OnContextMenu(self, event):
        # The tab that was right-clicked is reported directly by the event.
        tab_idx = event.GetSelection()
        # Ignore the fixed "Logs" tab (when present) and the "Add Tab" page.
        if tab_idx < self.__FirstUserTabIndex() or tab_idx == self.GetPageCount() - 1:
            return

        # Show the context menu
        menu = wx.Menu()
        rename_item = menu.Append(wx.ID_ANY, "Rename tab")
        self.Bind(wx.EVT_MENU, partial(self.__OnRenameTab, tab_idx=tab_idx), rename_item)

        if len(self.tabs) > 1:
            delete_item = menu.Append(wx.ID_ANY, "Delete tab")        
            self.Bind(wx.EVT_MENU, partial(self.__OnDeleteTab, tab_idx=tab_idx), delete_item)

        # Popup at the current cursor position (wxDefaultPosition).
        self.PopupMenu(menu)
        menu.Destroy()
    
    def __OnRenameTab(self, event, tab_idx):
        # Show a dialog to enter the new name
        dlg = wx.TextEntryDialog(self, "Enter new name:", "Rename Tab",
                                 self.__GetDefaultTabName())
        
        if dlg.ShowModal() == wx.ID_OK:
            new_name = dlg.GetValue().strip()
            if new_name:
                if new_name in ('Add Tab', 'Logs'):
                    wx.MessageBox("You cannot rename this tab.", "Error", wx.OK | wx.ICON_ERROR)
                    return

                # Set the new name for the selected tab
                if self.GetPageText(tab_idx) != new_name:
                    self.SetPageText(tab_idx, new_name)
                    self.frame.view_settings.SetDirty(reason=DirtyReasons.TabRenamed)
        
        dlg.Destroy()
    
    def __OnDeleteTab(self, event, tab_idx):
        # Show a confirmation dialog
        dlg = wx.MessageDialog(self, "Are you sure you want to delete '{}'?".format(self.GetPageText(tab_idx)), "Delete Tab", wx.YES_NO | wx.ICON_QUESTION)
        
        if dlg.ShowModal() == wx.ID_YES:
            # Delete the selected tab
            with self.__SuppressAddTabDialog():
                self.DeletePage(tab_idx)
                self.tabs.pop(tab_idx - self.__FirstUserTabIndex())

                # Deleting the rightmost real tab can leave the plus tab
                # selected; move selection back to a real tab.
                if self.GetSelection() == self.GetPageCount() - 1:
                    self.SetSelection(self.GetPageCount() - 2)

            self.frame.view_settings.SetDirty(reason=DirtyReasons.TabDeleted)
        
        dlg.Destroy()
