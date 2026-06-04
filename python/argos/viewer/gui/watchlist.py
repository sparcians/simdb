import wx
from functools import partial
from viewer.gui.view_settings import DirtyReasons

class Watchlist(wx.TreeCtrl):
    def __init__(self, parent, frame):
        super(Watchlist, self).__init__(parent, style=wx.TR_DEFAULT_STYLE | wx.TR_HIDE_ROOT | wx.TR_LINES_AT_ROOT)
        self.frame = frame
        self._watched_sim_elems = []
        self._mode = 'flat'

        sizer = wx.BoxSizer(wx.VERTICAL)
        self.SetSizer(sizer)
        self.__RenderWatchlist()

        self.Bind(wx.EVT_TREE_ITEM_GETTOOLTIP, self.__ProcessTooltip)
        self.Bind(wx.EVT_RIGHT_DOWN, self.__OnRightClick)
        self.Bind(wx.EVT_TREE_ITEM_EXPANDED, self.__OnItemExpanded)

        self._utiliz_image_list = frame.widget_renderer.utiliz_handler.CreateUtilizImageList()
        self.SetImageList(self._utiliz_image_list)

        self._tooltips_by_item = {}

    def GetWatchedSimElems(self):
        return self._watched_sim_elems
    
    def GetItemElemPath(self, item):
        if not item.IsOk():
            return None

        path_parts = []
        while item and item != self.GetRootItem():
            item_text = self.GetItemText(item)
            path_parts.append(item_text)
            item = self.GetItemParent(item)
        
        if item == self.GetRootItem():
            path_parts.append(self.GetItemText(self.GetRootItem()))
        
        path_parts.reverse()
        return '.'.join(path_parts).replace('Root.Watchlist.', '')
       
    def AddToWatchlist(self, elem_path):
        if elem_path in self._watched_sim_elems:
            return

        self._watched_sim_elems.append(elem_path)
        self.__RenderWatchlist()
        self.GetParent().ChangeSelection(1)
        self.frame.view_settings.SetDirty(reason=DirtyReasons.WatchlistAdded)

    def UpdateUtilizBitmaps(self):
        self._tooltips_by_item = {}
        self.__UpdateUtilizBitmaps(self.GetRootItem())

    def ExpandAll(self):
        self.Unbind(wx.EVT_TREE_ITEM_EXPANDED)
        super(Watchlist, self).ExpandAll()
        self.UpdateUtilizBitmaps()
        self.Bind(wx.EVT_TREE_ITEM_EXPANDED, self.__OnItemExpanded)

    def GetCurrentViewSettings(self):
        settings = {}
        settings['grouping_mode'] = self._mode
        settings['watched_sim_elem_paths'] = self._watched_sim_elems
        return settings
    
    def ApplyViewSettings(self, settings):
        grouping_mode = settings['grouping_mode']
        watched_sim_elem_paths = settings['watched_sim_elem_paths']

        self._watched_sim_elems = watched_sim_elem_paths
        self._mode = grouping_mode
        self.__RenderWatchlist()

    def GetCurrentUserSettings(self):
        # All our settings are in the user settings and do not affect the view file
        return {}

    def ApplyUserSettings(self, settings):
        # All our settings are in the user settings and do not affect the view file
        pass

    def ResetToDefaultViewSettings(self, update_widgets=True):
        self.ApplyViewSettings({'grouping_mode': 'flat', 'watched_sim_elem_paths': []})

    def __GetExpandedElemPaths(self, start_item):
        expanded_items = []
        if start_item.IsOk() and self.IsExpanded(start_item):
            if start_item != self.GetRootItem():
                elem_path = self.GetItemElemPath(start_item)
                expanded_items.append(elem_path)

            child = self.GetFirstChild(start_item)[0]
            while child.IsOk():
                expanded_items.extend(self.__GetExpandedElemPaths(child))
                child = self.GetNextSibling(child)

        return expanded_items

    def __UpdateUtilizBitmaps(self, item):
        elem_path = self.GetItemElemPath(item)
        if elem_path is None:
            return

        if elem_path in self._watched_sim_elems:
            simhier = self.frame.explorer.navtree.simhier
            collectable_id = simhier.GetCollectionID(elem_path)
            widget_type = simhier.GetWidgetType(collectable_id)
            if widget_type == 'QueueTable':
                utiliz_pct = self.frame.widget_renderer.utiliz_handler.GetUtilizPct(elem_path)
                image_idx = int(utiliz_pct * 100)
                self.SetItemImage(item, image_idx)

                capacity = simhier.GetCapacityByElemPath(elem_path)
                size = int(capacity * utiliz_pct)
                tooltip = '{}\nUtilization: {}% ({}/{} bins filled)'.format(elem_path, round(utiliz_pct*100), size, capacity)
                self._tooltips_by_item[item] = tooltip
            elif widget_type == 'Timeseries':
                image_idx = self._utiliz_image_list.GetImageCount() - 1
                self.SetItemImage(item, image_idx)
                tooltip = '{}\nNo utilization data available for timeseries stats'.format(elem_path)
                self._tooltips_by_item[item] = tooltip

        child, cookie = self.GetFirstChild(item)
        while child.IsOk():
            self.__UpdateUtilizBitmaps(child)
            child, cookie = self.GetNextChild(item, cookie)

    def __RenderWatchlist(self):
        if self._mode == 'flat':
            self.__RenderFlatView()
        else:
            self.__RenderHierView()

    def __RenderFlatView(self, *args, **kwargs):
        dirty = self._mode == 'hier'
        self._mode = 'flat'
        self._undeletable_items = []

        self.DeleteAllItems()
        if len(self._watched_sim_elems) > 0:
            self.AddRoot("Root")

        sizer = self.GetSizer()
        sizer.Clear(True)

        if len(self._watched_sim_elems) == 0:
            howto = wx.StaticText(self, label="Right-click nodes in the \nNavTree to add them to \nthe Watchlist.",
                                  style=wx.ST_ELLIPSIZE_END)
            
            # Make the font size of howto smaller
            font = howto.GetFont()
            font.SetPointSize(font.GetPointSize() - 2)
            howto.SetFont(font)

            sizer.Add(howto, 1, wx.EXPAND | wx.ALL, 10)
        else:
            watchlist_root = self.AppendItem(self.GetRootItem(), "Watchlist")
            for elem in self._watched_sim_elems:
                self.AppendItem(watchlist_root, elem)

            self.ExpandAll()

        self.UpdateUtilizBitmaps()

        if dirty:
            self.frame.view_settings.SetDirty(reason=DirtyReasons.WatchlistOrgChanged)

    def __RenderHierView(self, *args, **kwargs):
        dirty = self._mode = 'flat'
        self._mode = 'hier'
        self._undeletable_items = []

        self.DeleteAllItems()
        if len(self._watched_sim_elems) > 0:
            self.AddRoot("Root")

        sizer = self.GetSizer()
        sizer.Clear(True)

        if len(self._watched_sim_elems) == 0:
            howto = wx.StaticText(self, label="Right-click nodes in the \nNavTree to add them to \nthe Watchlist.",
                                  style=wx.ST_ELLIPSIZE_END)
            
            # Make the font size of howto smaller
            font = howto.GetFont()
            font.SetPointSize(font.GetPointSize() - 2)
            howto.SetFont(font)

            sizer.Add(howto, 1, wx.EXPAND | wx.ALL, 10)
        else:
            items_by_path = {}
            items_by_path["Root"] = self.GetRootItem()

            watchlist_root = self.AppendItem(self.GetRootItem(), "Watchlist")
            items_by_path["Root.Watchlist"] = watchlist_root

            for _, item in items_by_path.items():
                self._undeletable_items.append(item)

            # Honor the same hierarchy as the NavTree
            navtree_leaf_paths = self.frame.explorer.navtree.simhier.GetItemElemPaths()

            for path in navtree_leaf_paths:
                if path not in self._watched_sim_elems:
                    continue

                parts = path.split('.')
                current_item = watchlist_root
                
                for part in parts:
                    item_key = ".".join(parts[:parts.index(part)+1])
                    
                    # Check if the item already exists
                    if item_key not in items_by_path:
                        current_item = self.AppendItem(current_item, part)
                        items_by_path[item_key] = current_item
                    else:
                        current_item = items_by_path[item_key]

            self.ExpandAll()

        self.UpdateUtilizBitmaps()

        if dirty:
            self.frame.view_settings.SetDirty(reason=DirtyReasons.WatchlistOrgChanged)

    def __ProcessTooltip(self, event):
        item = event.GetItem()
        tooltip = self._tooltips_by_item.get(item, '')
        event.SetToolTip(tooltip)

    def __OnRightClick(self, event):
        item = self.HitTest(event.GetPosition())
        if not item:
            return

        item = item[0]
        if not item.IsOk():
            return

        self.SelectItem(item)

        menu = wx.Menu()

        def ExpandAll(event, **kwargs):
            kwargs['watchlist'].ExpandAll()
            event.Skip()

        def CollapseAll(event, **kwargs):
            kwargs['watchlist'].CollapseAll()
            event.Skip()

        expand_all = menu.Append(-1, "Expand All")
        self.Bind(wx.EVT_MENU, partial(ExpandAll, watchlist=self), expand_all)

        collapse_all = menu.Append(-1, "Collapse All")
        self.Bind(wx.EVT_MENU, partial(CollapseAll, watchlist=self), collapse_all)

        menu.AppendSeparator()

        if self._mode == 'hier':
            flat_view = menu.Append(-1, "Flat View")
            self.Bind(wx.EVT_MENU, self.__RenderFlatView, flat_view)

        if self._mode == 'flat':
            hier_view = menu.Append(-1, "Hierarchical View")
            self.Bind(wx.EVT_MENU, self.__RenderHierView, hier_view)

        elem_path = self.GetItemElemPath(item)
        if elem_path in self._watched_sim_elems:
            menu.AppendSeparator()
            remove_from_watchlist = menu.Append(-1, "Remove from Watchlist")
            self.Bind(wx.EVT_MENU, partial(self.__RemoveFromWatchlist, elem_path=elem_path), remove_from_watchlist)
        elif item not in self._undeletable_items:
            menu.AppendSeparator()
            remove_from_watchlist = menu.Append(-1, "Remove from Watchlist")
            self.Bind(wx.EVT_MENU, partial(self.__RemoveFromWatchlist, item_to_remove=item), remove_from_watchlist)

        self.PopupMenu(menu)
        menu.Destroy()

        event.Skip()

    def __RemoveFromWatchlist(self, *args, **kwargs):
        dirty = False
        if 'elem_path' in kwargs:
            elem_path = kwargs['elem_path']
            dirty = elem_path in self._watched_sim_elems
            self._watched_sim_elems.remove(elem_path)
        else:
            item_to_remove = kwargs['item_to_remove']
            watched_sim_elems = []
            self.__RecurseGetWatchedSimElems(item_to_remove, watched_sim_elems)

            for sim_elem in watched_sim_elems:
                dirty |= sim_elem in self._watched_sim_elems
                self._watched_sim_elems.remove(sim_elem)

        self.__RenderWatchlist()

        if dirty:
            self.frame.view_settings.SetDirty(reason=DirtyReasons.WatchlistRemoved)

    def __RecurseGetWatchedSimElems(self, item, watched_sim_elems):
        item_path = self.GetItemElemPath(item)
        if item_path in self._watched_sim_elems:
            watched_sim_elems.append(item_path)

        child, cookie = self.GetFirstChild(item)
        while child.IsOk():
            self.__RecurseGetWatchedSimElems(child, watched_sim_elems)
            child, cookie = self.GetNextChild(item, cookie)

    def __OnItemExpanded(self, event):
        self.UpdateUtilizBitmaps()
