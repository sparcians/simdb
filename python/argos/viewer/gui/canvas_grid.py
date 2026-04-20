import wx
from viewer.gui.widgets.splitter_window import DirtySplitterWindow
from viewer.gui.view_settings import DirtyReasons

class CanvasGrid(wx.Panel):
    def __init__(self, parent, rows=1, cols=1):
        super(CanvasGrid, self).__init__(parent)

        if rows == 1 and cols == 1:
            self.container = WidgetContainer(self)
            self.Bind(wx.EVT_CONTEXT_MENU, self.__OnContextMenu)
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

    def __RecursivelyApplyViewSettings(self, settings, window):
        if settings['window_type'] == 'widget_container':
            widget_creation_str = settings['widget_creation_str']
            if widget_creation_str:
                widget = self.frame.widget_creator.CreateWidget(widget_creation_str, window.container)
                if 'widget_settings' in settings:
                    widget.ApplyViewSettings(settings['widget_settings'])

                window.container.SetWidget(widget)
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

    def __OnContextMenu(self, event):
        # Get the position where the user right-clicked
        pos = event.GetPosition()
        pos = self.ScreenToClient(pos)

        menu = wx.Menu()

        split_vertically = menu.Append(-1, "Split left/right")
        split_horizontally = menu.Append(-1, "Split top/bottom")

        if isinstance(self.GetParent(), wx.SplitterWindow):
            menu.AppendSeparator()
            explode = menu.Append(-1, "Explode")
            self.Bind(wx.EVT_MENU, self.__Explode, explode)
        elif isinstance(self.container, WidgetContainer):
            widget = self.container.GetWidget()
            if widget:
                menu.AppendSeparator()
                clear = menu.Append(-1, "Clear widget")
                self.Bind(wx.EVT_MENU, lambda event: self.__DestroyAllWidgets(self.container), clear)

        self.Bind(wx.EVT_MENU, self.__OnSplitVertically, split_vertically)
        self.Bind(wx.EVT_MENU, self.__OnSplitHorizontally, split_horizontally)

        self.PopupMenu(menu, pos)

    def __OnSplitVertically(self, event):
        widget_creation_str = None
        if self.container:
            widget = self.container.GetWidget()
            widget_creation_str = widget.GetWidgetCreationString() if widget else None

        if self.container:
            self.container.DestroyAllWidgets()

        self.GetSizer().Clear()
        self.container = CanvasGrid(self, rows=1, cols=2)
        self.GetSizer().Add(self.container, 1, wx.EXPAND)
        self.Layout()

        if widget_creation_str:
            win1 = self.container.container.GetWindow1()
            widget_container = win1.container
            widget = self.container.frame.widget_creator.CreateWidget(widget_creation_str, widget_container)
            widget_container.SetWidget(widget)

        splitter = self.container.container
        splitter.SetSashPosition(splitter.GetSize().GetWidth() // 2)
        self.frame.view_settings.SetDirty(reason=DirtyReasons.WidgetSplit)

    def __OnSplitHorizontally(self, event):
        widget_creation_str = None
        if self.container:
            widget = self.container.GetWidget()
            widget_creation_str = widget.GetWidgetCreationString() if widget else None

        if self.container:
            self.container.DestroyAllWidgets()

        self.GetSizer().Clear()
        self.container = CanvasGrid(self, rows=2, cols=1)
        self.GetSizer().Add(self.container, 1, wx.EXPAND)
        self.Layout()

        if widget_creation_str:
            win1 = self.container.container.GetWindow1()
            widget_container = win1.container
            widget = self.container.frame.widget_creator.CreateWidget(widget_creation_str, widget_container)
            widget_container.SetWidget(widget)

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
        widget = self.container.GetWidget()
        widget_creation_str = widget.GetWidgetCreationString() if widget else None

        frame = self.container.frame
        inspector = frame.inspector
        inspector.ResetCurrentTab()

        if widget_creation_str:
            containers = inspector.GetCurrentTabWidgetContainers()
            widget = frame.widget_creator.CreateWidget(widget_creation_str, containers[0])
            containers[0].SetWidget(widget)

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

        frame = parent 
        while frame and not isinstance(frame, wx.Frame):
            frame = frame.GetParent()

        self.SetDropTarget(WidgetContainerDropTarget(self, self.frame.widget_creator))

        sizer = wx.BoxSizer(wx.VERTICAL)
        self.SetSizer(sizer)

    @property
    def frame(self):
        frame = self.GetParent()
        while frame and not isinstance(frame, wx.Frame):
            frame = frame.GetParent()

        return frame
    
    def SetWidget(self, widget):
        if self._widget:
            self.GetSizer().Detach(self._widget)
            self._widget.Destroy()

        self._widget = widget
        if widget:
            sizer = self.GetSizer()
            sizer.Add(widget, 1, wx.EXPAND)

        self.Layout()
        self.frame.view_settings.SetDirty(reason=DirtyReasons.WidgetDropped)
        wx.CallAfter(self.__RefreshWidget)

    def SetWidgetFocus(self):
        if self._widget:
            self._widget.SetFocus()

    def DestroyAllWidgets(self):
        if self._widget:
            self.GetSizer().Detach(self._widget)
            self._widget.Destroy()
            self._widget = None

    def UpdateWidgets(self):
        if self._widget:
            self._widget.UpdateWidgetData()

    def GetWidget(self):
        return self._widget
    
    def SplitCanvas(self, split_mode, sash_position):
        assert not self.GetSizer().GetChildren(), 'Cannot split a non-empty widget container'

        if split_mode == 'horizontal':
            self.SplitHorizontally(WidgetContainer(self), WidgetContainer(self))
        elif split_mode == 'vertical':
            self.SplitVertically(WidgetContainer(self), WidgetContainer(self))

        self.SetSashPosition(sash_position)
    
    def __RefreshWidget(self):
        self.UpdateWidgets()
        self.SetWidgetFocus()

class WidgetContainerDropTarget(wx.TextDropTarget):
    def __init__(self, widget_container, widget_creator):
        super(WidgetContainerDropTarget, self).__init__()
        self.widget_container = widget_container
        self.widget_creator = widget_creator

    def OnDropText(self, x, y, text):
        widget = self.widget_container.GetWidget()
        if widget:
            current_widget_is_tool = widget.GetWidgetCreationString().find('$') == -1
            incoming_widget_is_tool = text.find('$') == -1

            if current_widget_is_tool and incoming_widget_is_tool:
                return self.__DropWidget(text)
            elif current_widget_is_tool and not incoming_widget_is_tool:
                elem_path = text.split('$')[1]
                err_msg = widget.GetErrorIfDroppedNodeIncompatible(elem_path)
                if err_msg:
                    msg, title = err_msg
                    wx.MessageBox(msg, title, wx.OK | wx.ICON_ERROR)
                    return False

                widget.AddElement(elem_path)
                return True
            else:
                return self.__DropWidget(text)
        else:
            return self.__DropWidget(text)

    def __DropWidget(self, text):
        widget = self.widget_creator.CreateWidget(text, self.widget_container)
        if not widget:
            return False
        
        self.widget_container.SetWidget(widget)
        return True
