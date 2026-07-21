import wx
from functools import partial

class CollectableTree(wx.TreeCtrl):
    def __init__(self, parent, frame, leaf_elem_paths):
        super(CollectableTree, self).__init__(parent, style=wx.TR_DEFAULT_STYLE | wx.TR_HIDE_ROOT | wx.TR_LINES_AT_ROOT)
        self.frame = frame
        self.simhier = frame.simhier
        self._visible_elem_paths = self.__BuildVisibleElemPaths(leaf_elem_paths)

        self._root = self.AddRoot("root")
        self._tree_items_by_id = {0: self._root }
        self._tree_items_by_elem_path = {}
        self.__RecurseBuildTree(self.simhier.GetTree().GetRoot())

        self._leaf_elem_paths_by_tree_item = {}
        for db_id, tree_item in self._tree_items_by_id.items():
            if not self.GetChildrenCount(tree_item):
                self._leaf_elem_paths_by_tree_item[tree_item] = self.simhier.GetElemPath(db_id).replace('root.','')

        self._leaf_tree_items_by_elem_path = {v: k for k, v in self._leaf_elem_paths_by_tree_item.items()}

        self.Bind(wx.EVT_RIGHT_DOWN, self.__OnRightClick)
        self.Bind(wx.EVT_TREE_ITEM_EXPANDED, self.__OnItemExpanded)

        self._tooltips_by_item = {}
        self.Bind(wx.EVT_TREE_ITEM_GETTOOLTIP, self.__ProcessTooltip)

        # Sanity checks to ensure that no element path contains 'root.'
        for _,elem_path in self._leaf_elem_paths_by_tree_item.items():
            assert elem_path.find('root.') == -1

        for elem_path,_ in self._tree_items_by_elem_path.items():
            assert elem_path.find('root.') == -1

    def UpdateUtilizBitmaps(self):
        pass

    def ExpandAll(self):
        self.Unbind(wx.EVT_TREE_ITEM_EXPANDED)
        super(CollectableTree, self).ExpandAll()
        self.UpdateUtilizBitmaps()
        self.Bind(wx.EVT_TREE_ITEM_EXPANDED, self.__OnItemExpanded)

    def GetItemElemPath(self, item):
        if not item or not item.IsOk():
            return None

        if item in self._leaf_elem_paths_by_tree_item:
            return self._leaf_elem_paths_by_tree_item[item]

        node_names = []
        while item and item != self.GetRootItem():
            node_name = self.GetItemText(item)
            node_names.append(node_name)
            item = self.GetItemParent(item)

        node_names.reverse()
        return '.'.join(node_names)
    
    def GetCurrentViewSettings(self):
        # All our settings are in the user settings and do not affect the view file
        return {}
    
    def ApplyViewSettings(self, settings):
        # All our settings are in the user settings and do not affect the view file
        return
    
    def GetCurrentUserSettings(self):
        settings = {}
        settings['expanded_elem_paths'] = self.__GetExpandedElemPaths(self.GetRootItem())
        settings['selected_elem_path'] = self.GetItemElemPath(self.GetSelection())
        return settings

    def ApplyUserSettings(self, settings):
        expanded_elem_paths = settings['expanded_elem_paths']
        selected_elem_path = settings['selected_elem_path']

        self.CollapseAll()
        for elem_path in expanded_elem_paths:
            item = self._tree_items_by_elem_path[elem_path]
            self.Expand(item)

        if selected_elem_path:
            item = self._tree_items_by_elem_path[selected_elem_path]
            self.SelectItem(item)
            self.EnsureVisible(item)
        else:
            selected_item = self.GetSelection()
            if selected_item and selected_item.IsOk():
                self.SelectItem(selected_item, False)

    def ResetToDefaultViewSettings(self, update_widgets=True):
        self.ApplyUserSettings({'expanded_elem_paths': [], 'selected_elem_path': None})

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

    @staticmethod
    def __BuildVisibleElemPaths(leaf_elem_paths):
        visible_paths = set()
        for leaf_path in leaf_elem_paths:
            parts = leaf_path.split('.')
            for i in range(1, len(parts) + 1):
                visible_paths.add('.'.join(parts[:i]))
        return visible_paths

    def __RecurseBuildTree(self, node):
        if node is self.simhier.GetTree().GetRoot():
            for child in node.GetChildren():
                self.__RecurseBuildTree(child)
        else:
            if node.GetPath() not in self._visible_elem_paths:
                return

            if node.GetParent():
                parent_id = node.GetParent().GetID()
            else:
                parent_id = 0

            child_name = node.GetName()
            child_id = node.GetID()
            child = self.AppendItem(self._tree_items_by_id[parent_id], child_name)
            self._tree_items_by_id[child_id] = child
            self._tree_items_by_elem_path[self.simhier.GetElemPath(child_id)] = child

            for child in node.GetChildren():
                self.__RecurseBuildTree(child)

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
            kwargs['tree'].ExpandAll()
            event.Skip()

        def CollapseAll(event, **kwargs):
            kwargs['tree'].CollapseAll()
            event.Skip()

        expand_all = menu.Append(-1, "Expand All")
        self.Bind(wx.EVT_MENU, partial(ExpandAll, tree=self), expand_all)

        collapse_all = menu.Append(-1, "Collapse All")
        self.Bind(wx.EVT_MENU, partial(CollapseAll, tree=self), collapse_all)

        self.PopupMenu(menu)
        menu.Destroy()

    def __OnItemExpanded(self, event):
        self.UpdateUtilizBitmaps()

    def __ProcessTooltip(self, event):
        item = event.GetItem()
        event.SetToolTip(self._tooltips_by_item.get(item, ""))

class QueuesTree(CollectableTree):
    def __init__(self, parent, frame):
        super().__init__(parent, frame, frame.simhier.GetContainerElemPaths())
        self._utiliz_image_list = frame.widget_renderer.utiliz_handler.CreateUtilizImageList()
        self.SetImageList(self._utiliz_image_list)

    def UpdateUtilizBitmaps(self):
        for elem_path in self.simhier.GetContainerElemPaths():
            collectable_id = self.simhier.GetCollectionID(elem_path)
            widget_type = self.simhier.GetWidgetType(collectable_id)
            if widget_type == 'QueueTable':
                utiliz_pct = self.frame.widget_renderer.utiliz_handler.GetUtilizPct(elem_path)
                image_idx = int(utiliz_pct * 100)
                item = self._leaf_tree_items_by_elem_path[elem_path]
                self.SetItemImage(item, image_idx)

                capacity = self.simhier.GetCapacityByElemPath(elem_path)
                size = int(capacity * utiliz_pct)
                tooltip = '{}\nUtilization: {}% ({}/{} bins filled)'.format(elem_path, round(utiliz_pct*100), size, capacity)
            elif widget_type == 'Timeseries':
                image_idx = self._utiliz_image_list.GetImageCount() - 1
                item = self._leaf_tree_items_by_elem_path[elem_path]
                self.SetItemImage(item, image_idx)
                tooltip = '{}\nNo utilization data available for timeseries stats'.format(elem_path)
            else:
                item = None 
                tooltip = None

            if item and tooltip:
                self._tooltips_by_item[item] = tooltip

class ScalarsTree(CollectableTree):
    def __init__(self, parent, frame):
        simhier = frame.simhier
        leaf_paths = simhier.GetScalarStatsElemPaths() + simhier.GetScalarStructsElemPaths()
        leaf_paths.sort()
        super().__init__(parent, frame, leaf_paths)
