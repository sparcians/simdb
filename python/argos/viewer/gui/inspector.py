import re
import wx
from viewer.gui.canvas_grid import CanvasGrid
from viewer.gui.view_settings import DirtyReasons
from functools import partial

class DataInspector(wx.Notebook):
    def __init__(self, parent, frame):
        super(DataInspector, self).__init__(parent, style=wx.NB_TOP)

        self.frame = frame
        self.tabs = []
        self.__AddPlusTab()
        self.__AddInspectorTab("Tab 1")
        self.SetSelection(0)
        self.SetMinSize((200, 200))

        self.Bind(wx.EVT_NOTEBOOK_PAGE_CHANGED, self.__OnPageChanged)
        self.Bind(wx.EVT_CONTEXT_MENU, self.__OnContextMenu)

    def GetCurrentTabWidgetContainers(self):
        selected_tab = self.GetSelection()
        if selected_tab == self.GetPageCount() - 1:
            return None
        
        return self.tabs[selected_tab].GetWidgetContainers()
    
    def ResetCurrentTab(self):
        selected_tab = self.GetSelection()
        if selected_tab == self.GetPageCount() - 1:
            return
        
        self.tabs[selected_tab].ResetLayout()

    def GetCurrentViewSettings(self):
        settings = {}
        settings['tab_names'] = [self.GetPageText(i) for i in range(self.GetPageCount() - 1)]
        settings['tab_settings'] = [tab.GetCurrentViewSettings() for tab in self.tabs]
        return settings
    
    def ApplyViewSettings(self, settings):
        # Reset all tabs
        for tab in self.tabs:
            tab.ResetLayout()

        self.tabs = []

        # Delete all tabs except the last one
        while self.GetPageCount() > 1:
            self.DeletePage(0)

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
        # Select the tab that was selected before saving the settings
        for i in range(self.GetPageCount() - 1):
            if self.GetPageText(i) == settings['selected_tab']:
                self.SetSelection(i)
                break

    def ResetToDefaultViewSettings(self, update_widgets=True):
        self.ApplyViewSettings({'tab_names': ['Tab 1']})
        self.ApplyUserSettings({'selected_tab': 'Tab 1'})

    def RefreshWidgetsOnCurrentTab(self):
        selected_tab = self.GetSelection()
        if selected_tab == self.GetPageCount() - 1:
            return

        self.tabs[selected_tab].UpdateWidgets()
        self.tabs[selected_tab].Layout()
        self.tabs[selected_tab].Refresh()

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

    def __AddPlusTab(self):
        super(DataInspector, self).AddPage(wx.Panel(self), "Add Tab")

    def __AddInspectorTab(self, name):
        canvas_grid = CanvasGrid(self)
        super(DataInspector, self).InsertPage(self.GetPageCount() - 1, canvas_grid, name)
        self.tabs.append(canvas_grid)
        self.SetSelection(self.GetPageCount() - 2)

    def __OnPageChanged(self, event):
        new_page_index = event.GetSelection()
        if new_page_index == self.GetPageCount() - 1:
            self.__ShowAddTabDialog()

        event.Skip()

    def __ShowAddTabDialog(self):
        dlg = wx.TextEntryDialog(self, "Enter name for the new tab:", "New Tab", value=self.__GetDefaultTabName())

        if dlg.ShowModal() == wx.ID_OK:
            new_tab_name = dlg.GetValue().strip()
            if new_tab_name:
                for i in range(self.GetPageCount() - 1):
                    if self.GetPageText(i) == new_tab_name:
                        wx.MessageBox("A tab with that name already exists.", "Error", wx.OK | wx.ICON_ERROR)
                        return

                self.__AddInspectorTab(new_tab_name)
                self.frame.view_settings.SetDirty(reason=DirtyReasons.TabAdded)
        else:
            self.SetSelection(self.GetPageCount() - 2)

        dlg.Destroy()

    def __OnContextMenu(self, event):
        # Get the position where the user right-clicked
        pos = event.GetPosition()
        pos = self.ScreenToClient(pos)  # Convert to client coordinates

        # Check if the right-click was within the tab area
        hit = self.HitTest(pos)
        if hit == wx.NOT_FOUND or hit[0] == self.GetPageCount() - 1 or hit[0] == -1:
            return
        
        # Show the context menu
        menu = wx.Menu()
        rename_item = menu.Append(wx.ID_ANY, "Rename tab")
        self.Bind(wx.EVT_MENU, partial(self.__OnRenameTab, tab_idx=hit[0]), rename_item)

        if len(self.tabs) > 1:
            delete_item = menu.Append(wx.ID_ANY, "Delete tab")        
            self.Bind(wx.EVT_MENU, partial(self.__OnDeleteTab, tab_idx=hit[0]), delete_item)

        # Popup the menu
        pos = wx.Point(pos.x, pos.y - 40)
        self.PopupMenu(menu, self.ClientToScreen(pos))
        menu.Destroy()
    
    def __OnRenameTab(self, event, tab_idx):
        # Show a dialog to enter the new name
        dlg = wx.TextEntryDialog(self, "Enter new name:", "Rename Tab",
                                 self.__GetDefaultTabName())
        
        if dlg.ShowModal() == wx.ID_OK:
            new_name = dlg.GetValue().strip()
            if new_name:
                if new_name == 'Add Tab':
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
            self.DeletePage(tab_idx)
            self.tabs.pop(tab_idx)
            self.frame.view_settings.SetDirty(reason=DirtyReasons.TabDeleted)
        
        dlg.Destroy()
