import wx, wx.adv, wx.aui
from viewer.gui.widgets.splitter_window import DirtySplitterWindow
from viewer.gui.view_settings import DirtyReasons
from viewer.gui.widgets.queue_utiliz import QueueUtilizWidget
from viewer.gui.widgets.scheduling_lines import SchedulingLinesWidget
from viewer.gui.widgets.summary_views import SummaryViews
from viewer.gui.widgets.iterable_struct import IterableStruct
from viewer.gui.dialogs.widget_data_selections import WidgetDataSelectionsDlg
from viewer.gui.dialogs.widget_data_selections import SchedulingLinesEditDlg
from functools import partial

class CanvasGrid(wx.Panel):
    def __init__(self, parent, rows=1, cols=1):
        super(CanvasGrid, self).__init__(parent)

        if rows == 1 and cols == 1:
            self.container = WidgetContainer(self)
        else:
            self.container = DirtySplitterWindow(self.frame, self, style=wx.SP_LIVE_UPDATE)
            self.__BuildGrid(self.container, rows, cols)
            if not self.container.GetSizer():
                sizer = wx.BoxSizer(wx.VERTICAL)
                sizer.Add(self.container.GetWindow1(), 1, wx.EXPAND)
                sizer.Add(self.container.GetWindow2(), 1, wx.EXPAND)
                self.container.SetSizer(sizer)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(self.container, 1, wx.EXPAND)
        self.SetSizer(sizer)

    @property
    def frame(self):
        frame = self.GetParent()
        while frame and not isinstance(frame, wx.Frame):
            frame = frame.GetParent()

        return frame

    def DestroyAllWidgets(self):
        self.__DestroyAllWidgets(self.container)

    def UpdateWidgets(self):
        self.__UpdateWidgets(self.container)

    def GetWidgetContainers(self):
        widget_containers = []
        self.__GetWidgetContainers(widget_containers)
        return widget_containers
    
    def ResetLayout(self):
        sizer = self.GetSizer()
        sizer.Detach(self.container)
        self.container.Destroy()
 
        self.container = WidgetContainer(self)
        sizer.Add(self.container, 1, wx.EXPAND)
        self.Layout()

    def GetCurrentViewSettings(self):
        settings = {}
        self.__RecursivelyGetViewSettings(settings, self.container)
        return settings
    
    def ApplyViewSettings(self, settings):
        self.__RecursivelyApplyViewSettings(settings, self)

    def AddQuickLinks(self, links, splitters_only=False):
        links.append(("Split left/right", self.__OnSplitVertically))
        links.append(("Split top/bottom", self.__OnSplitHorizontally))
        if not splitters_only:
            links.append(("Maximize", self.__Explode))

    def __RecursivelyApplyViewSettings(self, settings, window):
        if settings['window_type'] == 'widget_container':
            widget_creation_str = settings['widget_creation_str']
            if widget_creation_str and widget_creation_str != 'NO_WIDGET':
                widget = self.frame.widget_creator.CreateWidget(widget_creation_str, window.container)
                if 'widget_settings' in settings:
                    widget.ApplyViewSettings(settings['widget_settings'])

                window.container.SetWidget(widget)
            else:
                window.container.SetWidget(None)
        elif settings['window_type'] == 'splitter':
            if settings['split_mode'] == 'horizontal':
                window.__OnSplitHorizontally(None)
            else:
                window.__OnSplitVertically(None)

            splitter = window.container.container
            window1, window2 = splitter.GetWindow1(), splitter.GetWindow2()
            if 'window1' in settings:
                assert window1
                self.__RecursivelyApplyViewSettings(settings['window1'], window1)
            if 'window2' in settings:
                assert window2
                self.__RecursivelyApplyViewSettings(settings['window2'], window2)

            splitter.SetSashPosition(settings['sash_position'])

    def __RecursivelyGetViewSettings(self, settings, window):
        if isinstance(window, WidgetContainer):
            widget = window.GetWidget()
            settings['window_type'] = 'widget_container'
            settings['widget_creation_str'] = widget.GetWidgetCreationString() if widget else 'NO_WIDGET'
            if widget:
                settings['widget_settings'] = widget.GetCurrentViewSettings()
        elif isinstance(window, CanvasGrid):
            self.__RecursivelyGetViewSettings(settings, window.container)
        elif isinstance(window, wx.SplitterWindow):
            settings['window_type'] = 'splitter'
            settings['sash_position'] = window.GetSashPosition()
            settings['split_mode'] = 'vertical' if window.GetSplitMode() == wx.SPLIT_VERTICAL else 'horizontal'
            if window.Window1:
                settings['window1'] = {}
                self.__RecursivelyGetViewSettings(settings['window1'], window.Window1)

            if window.Window2:
                settings['window2'] = {}
                self.__RecursivelyGetViewSettings(settings['window2'], window.Window2)

    def __GetWidgetContainers(self, widget_containers):
        if isinstance(self.container, WidgetContainer):
            widget_containers.append(self.container)
        else:
            for child in self.container.GetChildren():
                if isinstance(child, CanvasGrid):
                    child.__GetWidgetContainers(widget_containers)

    def __BuildGrid(self, splitter, rows, cols):
        assert rows > 0 and cols > 0

        if rows > 1 and cols > 1:
            top_splitter = DirtySplitterWindow(self.frame, splitter, style=wx.SP_LIVE_UPDATE)
            bottom_splitter = DirtySplitterWindow(self.frame, splitter, style=wx.SP_LIVE_UPDATE)

            top_grid = CanvasGrid(top_splitter, rows=rows // 2, cols=cols)
            top_sizer = wx.BoxSizer(wx.VERTICAL)
            top_sizer.Add(top_grid, 1, wx.EXPAND)
            top_splitter.SetSizer(top_sizer)

            bottom_grid = CanvasGrid(bottom_splitter, rows=rows - rows // 2, cols=cols)
            bottom_sizer = wx.BoxSizer(wx.VERTICAL)
            bottom_sizer.Add(bottom_grid, 1, wx.EXPAND)
            bottom_splitter.SetSizer(bottom_sizer)

            splitter.SplitHorizontally(top_splitter, bottom_splitter)

            sizer = wx.BoxSizer(wx.VERTICAL)
            sizer.Add(top_splitter, 1, wx.EXPAND)
            sizer.Add(bottom_splitter, 1, wx.EXPAND)
            splitter.SetSizer(sizer)
        elif rows == 1:
            splitter.SplitVertically(CanvasGrid(splitter, rows, cols // 2), CanvasGrid(splitter, rows, cols - cols // 2))
        elif cols == 1:
            splitter.SplitHorizontally(CanvasGrid(splitter, rows // 2, cols), CanvasGrid(splitter, rows - rows // 2, cols))

    def __GetWidgetState(self):
        if not isinstance(self.container, WidgetContainer):
            return None, None

        widget = self.container.GetWidget()
        if widget is None:
            return None, None

        return widget.GetWidgetCreationString(), widget.GetCurrentViewSettings()

    @staticmethod
    def __PlaceWidgetInContainer(frame, widget_container, widget_creation_str, widget_settings):
        if not widget_creation_str:
            return

        widget = frame.widget_creator.CreateWidget(widget_creation_str, widget_container)
        if widget_settings is not None:
            widget.ApplyViewSettings(widget_settings)

        widget_container.SetWidget(widget)

    def __OnSplitVertically(self, event=None):
        widget_creation_str, widget_settings = self.__GetWidgetState()

        if self.container:
            self.container.DestroyAllWidgets()
            self.container.Destroy()
            self.container = None

        self.GetSizer().Clear()
        self.container = CanvasGrid(self, rows=1, cols=2)
        self.GetSizer().Add(self.container, 1, wx.EXPAND)
        self.Layout()

        win1 = self.container.container.GetWindow1()
        self.__PlaceWidgetInContainer(self.frame, win1.container, widget_creation_str, widget_settings)

        splitter = self.container.container
        splitter.SetSashPosition(splitter.GetSize().GetWidth() // 2)
        self.frame.view_settings.SetDirty(reason=DirtyReasons.WidgetSplit)

    def __OnSplitHorizontally(self, event=None):
        widget_creation_str, widget_settings = self.__GetWidgetState()

        if self.container:
            self.container.DestroyAllWidgets()
            self.container.Destroy()
            self.container = None

        self.GetSizer().Clear()
        self.container = CanvasGrid(self, rows=2, cols=1)
        self.GetSizer().Add(self.container, 1, wx.EXPAND)
        self.Layout()

        win1 = self.container.container.GetWindow1()
        self.__PlaceWidgetInContainer(self.frame, win1.container, widget_creation_str, widget_settings)

        splitter = self.container.container
        splitter.SetSashPosition(splitter.GetSize().GetHeight() // 2)
        self.frame.view_settings.SetDirty(reason=DirtyReasons.WidgetSplit)

    def __FindFirstWidgetContainer(self, container):
        if isinstance(container, WidgetContainer):
            return container
        else:
            for child in container.GetChildren():
                return self.__FindFirstWidgetContainer(child)
            
        return None

    def __Explode(self, event):
        widget_creation_str, widget_settings = self.__GetWidgetState()

        frame = self.frame
        inspector = frame.inspector
        inspector.ResetCurrentTab()

        if widget_creation_str:
            containers = inspector.GetCurrentTabWidgetContainers()
            CanvasGrid.__PlaceWidgetInContainer(
                frame, containers[0], widget_creation_str, widget_settings,
            )

        frame.view_settings.SetDirty(reason=DirtyReasons.CanvasExploded)

    def __DestroyAllWidgets(self, container):
        if isinstance(container, WidgetContainer):
            container.DestroyAllWidgets()
            container.frame.view_settings.SetDirty(reason=DirtyReasons.WidgetRemoved)
        else:
            for child in container.GetChildren():
                self.__DestroyAllWidgets(child)

    def __UpdateWidgets(self, container):
        if isinstance(container, WidgetContainer):
            container.UpdateWidgets()
        else:
            for child in container.GetChildren():
                self.__UpdateWidgets(child)

class WidgetContainer(wx.Panel):
    def __init__(self, parent):
        super(WidgetContainer, self).__init__(parent)
        self._widget = None
        self._quick_links = WidgetQuickLinks(self)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(self._quick_links, 1, wx.EXPAND | wx.LEFT | wx.TOP, 5)
        self.SetSizer(sizer)

    @property
    def frame(self):
        frame = self.GetParent()
        while frame and not isinstance(frame, wx.Frame):
            frame = frame.GetParent()

        return frame

    def SetWidget(self, widget):
        sizer = self.GetSizer()
        if widget:
            dirty_reason = DirtyReasons.WidgetDropped
        elif self._widget:
            dirty_reason = DirtyReasons.WidgetRemoved
        else:
            dirty_reason = None

        if self._widget:
            sizer.Detach(self._widget)
            self._widget.Destroy()
            self._widget = None

        if widget:
            self.__HideQuickLinks()
            self._widget = widget
            sizer.Add(widget, 1, wx.EXPAND)
        else:
            self.__ShowQuickLinks()

        self.Layout()
        if dirty_reason is not None:
            self.frame.view_settings.SetDirty(reason=dirty_reason)
        wx.CallAfter(self.__RefreshWidget)

    def SetWidgetFocus(self):
        if self._widget:
            self._widget.SetFocus()

    def DestroyAllWidgets(self):
        self.SetWidget(None)

    def UpdateWidgets(self):
        if self._widget:
            self._widget.UpdateWidgetData()

    def GetWidget(self):
        return self._widget

    def LaunchQueueViewer(self):
        selected = [self._widget.elem_path] if self._widget else []
        dlg = WidgetDataSelectionsDlg(self, self.frame, selected, queues_only=True, single_selection=True, title="Select queue")
        result = dlg.ShowModal()

        if result == wx.ID_OK:
            selected = dlg.GetSelectedElemPaths()
            if len(selected) == 0:
                wx.MessageBox('No data selected', 'Error', wx.OK | wx.ICON_ERROR)
                selected = None
            else:
                selected = selected[0]
        else:
            selected = None
        dlg.Destroy()

        if not selected:
            return

        widget = IterableStruct(self, self.frame, selected)
        self.SetWidget(widget)

    def LaunchQueueUtilizViewer(self):
        widget = QueueUtilizWidget(self, self.frame)
        self.SetWidget(widget)

    def LaunchSchedulingLinesViewer(self):
        if isinstance(self._widget, SchedulingLinesWidget):
            elem_paths = self._widget.caption_mgr.GetAllMatchingElemPaths()
            num_ticks_before = self._widget.num_ticks_before
            num_ticks_after = self._widget.num_ticks_after
            show_details = self._widget.show_detailed_queue_packets
            hide_empty_rows = self._widget.hide_empty_rows
            show_full_paths = self._widget.show_full_paths
            enable_tooltips = self._widget.enable_tooltips
            show_did = self._widget.show_did
        else:
            elem_paths = []
            num_ticks_before = SchedulingLinesWidget.DEFAULT_TICKS_BEFORE
            num_ticks_after = SchedulingLinesWidget.DEFAULT_TICKS_AFTER
            show_details = SchedulingLinesWidget.DEFAULT_SHOW_DETAILS
            hide_empty_rows = SchedulingLinesWidget.DEFAULT_HIDE_EMPTY_ROWS
            show_full_paths = SchedulingLinesWidget.DEFAULT_SHOW_FULL_PATHS
            enable_tooltips = SchedulingLinesWidget.DEFAULT_ENABLE_TOOLTIPS
            show_did = SchedulingLinesWidget.DEFAULT_SHOW_DID

        dlg = SchedulingLinesEditDlg(self, self.frame, elem_paths, num_ticks_before, num_ticks_after, show_details, hide_empty_rows, show_full_paths, enable_tooltips, show_did)
        result = dlg.ShowModal()
        if result == wx.ID_OK:
            elem_paths = dlg.GetSelectedElemPaths()
            num_ticks_before = dlg.num_ticks_before
            num_ticks_after = dlg.num_ticks_after
            show_details = dlg.show_details
            hide_empty_rows = dlg.hide_empty_rows
            show_full_paths = dlg.show_full_paths
            enable_tooltips = dlg.enable_tooltips
            show_did = dlg.show_did
            if len(elem_paths) > 0:
                widget = SchedulingLinesWidget(self, self.frame, elem_paths, num_ticks_before, num_ticks_after, show_details, hide_empty_rows, show_full_paths, enable_tooltips, show_did)
                self.SetWidget(widget)
            else:
                wx.MessageBox("No data selected", "Error", wx.OK | wx.ICON_ERROR)

        dlg.Destroy()

    def LaunchWatchlistBuilder(self):
        widget = SummaryViews(self, self.frame)
        widget.Hide()
        if widget.EditWidget(None):
            widget.Show()
            self.SetWidget(widget)
        else:
            widget.Destroy()

    def AddQuickLinks(self, links):
        self.GetParent().AddQuickLinks(links, True)

    def __HideQuickLinks(self):
        if not self._quick_links.IsShown():
            return

        sizer = self.GetSizer()
        for item in sizer.GetChildren():
            if item.GetWindow() == self._quick_links:
                sizer.Detach(self._quick_links)
                break
        self._quick_links.Hide()

    def __ShowQuickLinks(self):
        sizer = self.GetSizer()
        self._quick_links.Show()
        for item in sizer.GetChildren():
            if item.GetWindow() == self._quick_links:
                return
        sizer.Add(self._quick_links, 1, wx.EXPAND | wx.LEFT | wx.TOP, 5)

    def __RefreshWidget(self):
        self.UpdateWidgets()
        self.SetWidgetFocus()

class WidgetQuickLinks(wx.Panel):
    def __init__(self, widget_container):
        wx.Panel.__init__(self, widget_container)

        def AddLinks(label, links):
            label = wx.StaticText(self, label=label)
            vsizer.Add(label)
            for label, callback in links:
                row = wx.BoxSizer(wx.HORIZONTAL)
                bullet = wx.StaticText(self, label="\u2022")
                link = wx.adv.HyperlinkCtrl(
                    self, label=label, style=wx.adv.HL_DEFAULT_STYLE & ~wx.adv.HL_CONTEXTMENU)
                link.Bind(wx.adv.EVT_HYPERLINK, lambda evt, cb=callback: cb())
                row.Add(bullet, 0, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 8)
                row.Add(link)
                vsizer.Add(row, 0, wx.LEFT, 5)

        vsizer = wx.BoxSizer(wx.VERTICAL)
        self.SetSizer(vsizer)

        # Links to create widgets
        links = [
            ("Queue Inspector", widget_container.LaunchQueueViewer),
            ("Queue Utilizations", widget_container.LaunchQueueUtilizViewer),
            ("Scheduling Lines", widget_container.LaunchSchedulingLinesViewer),
            ("Watchlist", widget_container.LaunchWatchlistBuilder)
        ]
        AddLinks("Widgets:", links)

        # Links to split canvas
        links = []
        widget_container.AddQuickLinks(links)
        AddLinks("Canvas:", links)

        self.Layout()
