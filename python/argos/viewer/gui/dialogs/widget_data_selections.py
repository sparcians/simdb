import wx
from functools import partial

class WidgetDataSelectionsDlg(wx.Dialog):
    def __init__(
        self, parent, frame, elem_paths, only_show_selected=False, queues_only=False, single_selection=False, title="Edit Data Selections",
    ):
        _, screen_h = wx.GetDisplaySize()
        super().__init__(parent, title=title, size=(600, int(screen_h * 0.75)))

        self.frame = frame
        self.simhier = frame.simhier
        if queues_only:
            self._all_leaf_paths = sorted(self.simhier.GetContainerElemPaths())
        else:
            self._all_leaf_paths = sorted(self.simhier.GetElemPaths(True))

        initial_paths = set(elem_paths)
        if single_selection:
            assert len(initial_paths) <= 1

        self._selected_paths = [path for path in self._all_leaf_paths if path in initial_paths]
        self._initial_paths = list(self._selected_paths)
        self._initial_only_show_selected = only_show_selected
        self._only_show_selected = only_show_selected
        self._single_selection = single_selection
        self._single_selected_path = None

        self._tree_items_by_id = {}
        self._leaf_paths_by_tree_item = {}
        self._list_indices_by_path = {}
        self._syncing_list_checkboxes = False

        if single_selection and queues_only:
            instruction_text = 'Select a leaf queue from the tree'
        elif single_selection:
            instruction_text = 'Select a leaf node from the tree'
        else:
            instruction_text = 'Right-click nodes to add/remove from widget'

        instruction_label = wx.StaticText(self, label=instruction_text)
        tree_style = wx.TR_DEFAULT_STYLE | wx.TR_HIDE_ROOT | wx.TR_LINES_AT_ROOT
        if not single_selection:
            tree_style = tree_style | wx.TR_MULTIPLE
        self.hier_tree = wx.TreeCtrl(self, style=tree_style)

        if not single_selection:
            self.selections_list = wx.ListCtrl(self, style=wx.LC_REPORT | wx.LC_SINGLE_SEL)
            self.selections_list.EnableCheckBoxes(True)
            self.selections_list.InsertColumn(0, 'Current Selections', width=550)

            self.only_selected_cb = wx.CheckBox(self, label='Only show selected data')
            self.only_selected_cb.SetValue(only_show_selected)
            self.only_selected_cb.Bind(wx.EVT_CHECKBOX, self.__OnOnlyShowSelectedChanged)

        self.ok_btn_disabled_reason = wx.StaticText(self)
        self.ok_btn_disabled_reason.SetForegroundColour(wx.RED)
        self.ok_btn_disabled_reason.Hide()

        btn_sizer = wx.StdDialogButtonSizer()
        self.ok_btn = wx.Button(self, wx.ID_OK)
        btn_sizer.AddButton(self.ok_btn)
        btn_sizer.AddButton(wx.Button(self, wx.ID_CANCEL))
        btn_sizer.Realize()

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(instruction_label, 0, wx.ALL, 5)
        sizer.Add(self.hier_tree, 1, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
        if not single_selection:
            sizer.Add(self.selections_list, 1, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
            sizer.Add(self.only_selected_cb, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM, 5)
        sizer.Add(self.ok_btn_disabled_reason, 0, wx.LEFT, 5)
        sizer.Add(btn_sizer, 0, wx.ALL | wx.ALIGN_RIGHT, 10)
        self.SetSizer(sizer)

        if not single_selection:
            self.hier_tree.Bind(wx.EVT_RIGHT_DOWN, partial(self.__OnTreeRightClick, tree=self.hier_tree))
            self.selections_list.Bind(wx.EVT_LIST_ITEM_CHECKED, self.__OnListItemChecked)
            self.selections_list.Bind(wx.EVT_LIST_ITEM_UNCHECKED, self.__OnListItemUnchecked)
        else:
            self.hier_tree.Bind(wx.EVT_TREE_SEL_CHANGED, self.__OnTreeSelectionChanged)
            self.hier_tree.Bind(wx.EVT_RIGHT_DOWN, partial(self.__OnTreeRightClick, tree=self.hier_tree))

        self.__BuildTree()
        self.__BuildSelectionsList()
        self.__UpdateOkButton()

    def GetSelectedElemPaths(self):
        if self._single_selection:
            return [self._single_selected_path] if self._single_selected_path else []
        return list(self._selected_paths)

    def GetOnlyShowSelected(self):
        return self._only_show_selected

    def __OnOnlyShowSelectedChanged(self, evt):
        self._only_show_selected = self.only_selected_cb.IsChecked()
        self.__BuildSelectionsList()
        self.__UpdateOkButton()
        evt.Skip()

    def __OnListItemChecked(self, evt):
        if self._syncing_list_checkboxes:
            evt.Skip()
            return

        path = self.selections_list.GetItemText(evt.GetIndex())
        if path not in self._selected_paths:
            selected_paths = set(self._selected_paths)
            selected_paths.add(path)
            self._selected_paths = [p for p in self._all_leaf_paths if p in selected_paths]
            self.__UpdateOkButton()
        evt.Skip()

    def __OnListItemUnchecked(self, evt):
        if self._syncing_list_checkboxes:
            evt.Skip()
            return

        path = self.selections_list.GetItemText(evt.GetIndex())
        if path in self._selected_paths:
            selected_paths = set(self._selected_paths)
            selected_paths.remove(path)
            self._selected_paths = [p for p in self._all_leaf_paths if p in selected_paths]
            if self._only_show_selected:
                self.__BuildSelectionsList()
            self.__UpdateOkButton()
        evt.Skip()

    def __OnTreeSelectionChanged(self, evt):
        item = self.hier_tree.GetSelection()
        if item.IsOk() and item in self._leaf_paths_by_tree_item:
            self._single_selected_path = self._leaf_paths_by_tree_item[item]
        else:
            self._single_selected_path = None

        self.__UpdateOkButton()
        evt.Skip()

    def __OnTreeRightClick(self, evt, tree):
        item = tree.HitTest(evt.GetPosition())
        if not item:
            return

        item = item[0]
        if not item.IsOk():
            return

        if self._single_selection:
            tree.SelectItem(item)
        else:
            selections = tree.GetSelections()
            if item not in selections:
                tree.SelectItem(item)
        self.__PopupTreeContextMenu(tree, item)

    def __GetSelectedLeafPaths(self, tree):
        paths = []
        for selected_item in tree.GetSelections():
            if selected_item.IsOk() and selected_item in self._leaf_paths_by_tree_item:
                paths.append(self._leaf_paths_by_tree_item[selected_item])
        return paths

    def __GetTargetLeafPathsForMenu(self, tree, item):
        selected_leaf_paths = self.__GetSelectedLeafPaths(tree)
        if item in tree.GetSelections() and selected_leaf_paths:
            return selected_leaf_paths
        return [self._leaf_paths_by_tree_item[item]]

    def __PopupTreeContextMenu(self, tree, item):
        menu = wx.Menu()
        if not self._single_selection:
            if item in self._leaf_paths_by_tree_item:
                target_paths = self.__GetTargetLeafPathsForMenu(tree, item)
                in_widget = [path for path in target_paths if path in self._selected_paths]
                not_in_widget = [path for path in target_paths if path not in self._selected_paths]
                if not_in_widget:
                    add_item = menu.Append(-1, 'Add to Widget')
                    self.Bind(
                        wx.EVT_MENU,
                        partial(self.__OnAddLeavesFromBranch, paths=not_in_widget),
                        add_item,
                    )
                if in_widget:
                    remove_item = menu.Append(-1, 'Remove from Widget')
                    self.Bind(
                        wx.EVT_MENU,
                        partial(self.__OnRemoveLeavesFromBranch, paths=in_widget),
                        remove_item,
                    )
            else:
                leaves = self.__CollectLeavesFromItem(tree, item)
                selected_leaves = [path for path in leaves if path in self._selected_paths]
                if len(selected_leaves) < len(leaves):
                    add_leaves = menu.Append(-1, 'Add leaves to widget')
                    self.Bind(
                        wx.EVT_MENU,
                        partial(self.__OnAddLeavesFromBranch, paths=leaves),
                        add_leaves,
                    )
                if selected_leaves:
                    remove_leaves = menu.Append(-1, 'Remove leaves from widget')
                    self.Bind(
                        wx.EVT_MENU,
                        partial(self.__OnRemoveLeavesFromBranch, paths=selected_leaves),
                        remove_leaves,
                    )
            menu.AppendSeparator()
        self.__AppendExpandCollapseSubmenu(menu, tree)
        tree.PopupMenu(menu)
        menu.Destroy()

    def __AppendExpandCollapseSubmenu(self, menu, tree):
        expand_submenu = wx.Menu()
        all_expanded, all_collapsed = self.__GetTreeExpandCollapseState(tree)
        def ExpandAll(evt, **kwargs):
            kwargs['tree'].ExpandAll()
            evt.Skip()
        def CollapseAll(evt, **kwargs):
            kwargs['tree'].CollapseAll()
            evt.Skip()
        if not all_expanded:
            expand_all = expand_submenu.Append(-1, 'Expand All')
            self.Bind(wx.EVT_MENU, partial(ExpandAll, tree=tree), expand_all)
        if not all_collapsed:
            collapse_all = expand_submenu.Append(-1, 'Collapse All')
            self.Bind(wx.EVT_MENU, partial(CollapseAll, tree=tree), collapse_all)
        menu.AppendSubMenu(expand_submenu, 'Expand / Collapse')

    def __OnAddLeavesFromBranch(self, evt, paths):
        self.__SetPathsSelected(paths, True)
        evt.Skip()

    def __OnRemoveLeavesFromBranch(self, evt, paths):
        self.__SetPathsSelected(paths, False)
        evt.Skip()

    def __SetPathsSelected(self, paths, selected):
        selected_paths = set(self._selected_paths)
        changed = False
        for path in paths:
            if selected:
                if path not in selected_paths:
                    selected_paths.add(path)
                    changed = True
            elif path in selected_paths:
                selected_paths.remove(path)
                changed = True

        if changed:
            self._selected_paths = [p for p in self._all_leaf_paths if p in selected_paths]
            self.__ApplySelectionToList()
            self.__UpdateOkButton()

    def __UpdateOkButton(self):
        if self._single_selection:
            enable = (
                self._single_selected_path is not None
                and self._single_selected_path not in set(self._initial_paths)
            )
        else:
            enable = (
                self._selected_paths != self._initial_paths
                or self._only_show_selected != self._initial_only_show_selected
            )
        self.ok_btn.Enable(enable)
        if enable:
            self.ok_btn_disabled_reason.Hide()
        else:
            if self._single_selection:
                if self._single_selected_path is not None:
                    reason = "Selected queue has not changed"
                else:
                    reason = ""
            else:
                reason = "Selections have not changed"
            self.ok_btn_disabled_reason.SetLabel(reason)
            self.ok_btn_disabled_reason.Show()

    def __BuildTree(self):
        self._tree_items_by_id = {}
        self._leaf_paths_by_tree_item = {}

        self.hier_tree.DeleteAllItems()
        root = self.hier_tree.AddRoot('root')
        self._tree_items_by_id[0] = root

        visible_paths = self.__BuildVisibleElemPaths(self._all_leaf_paths)
        self.__RecurseBuildTree(
            self.hier_tree, self.simhier.GetTree().GetRoot(), visible_paths,
        )

    def __RecurseBuildTree(self, tree_ctrl, node, visible_paths):
        if node is self.simhier.GetTree().GetRoot():
            for child in node.GetChildren():
                self.__RecurseBuildTree(tree_ctrl, child, visible_paths)
            return

        elem_path = node.GetPath()
        if elem_path not in visible_paths:
            return

        if node.GetParent():
            parent_id = node.GetParent().GetID()
        else:
            parent_id = 0

        tree_item = tree_ctrl.AppendItem(self._tree_items_by_id[parent_id], node.GetName())
        node_id = node.GetID()
        self._tree_items_by_id[node_id] = tree_item

        if not node.children:
            self._leaf_paths_by_tree_item[tree_item] = elem_path

        for child in node.GetChildren():
            self.__RecurseBuildTree(tree_ctrl, child, visible_paths)

    def __ApplySelectionToList(self):
        if self._only_show_selected:
            self.__BuildSelectionsList()
        else:
            self.__SyncSelectionCheckboxes()

    def __BuildSelectionsList(self):
        if self._single_selection:
            return

        self.selections_list.Freeze()
        try:
            self.selections_list.DeleteAllItems()
            self._list_indices_by_path = {}

            selected_paths = set(self._selected_paths)
            for path in self.__GetDisplayedListPaths():
                idx = self.selections_list.InsertItem(self.selections_list.GetItemCount(), path)
                self.selections_list.CheckItem(idx, path in selected_paths)
                self._list_indices_by_path[path] = idx
        finally:
            self.selections_list.Thaw()

    def __SyncSelectionCheckboxes(self):
        if not self._list_indices_by_path:
            self.__BuildSelectionsList()
            return

        selected_paths = set(self._selected_paths)
        self._syncing_list_checkboxes = True
        try:
            for path, idx in self._list_indices_by_path.items():
                checked = path in selected_paths
                if self.selections_list.IsItemChecked(idx) != checked:
                    self.selections_list.CheckItem(idx, checked)
        finally:
            self._syncing_list_checkboxes = False

    def __GetDisplayedListPaths(self):
        if self._only_show_selected:
            return list(self._selected_paths)
        return list(self._all_leaf_paths)

    def __CollectLeavesFromItem(self, tree, item):
        if item in self._leaf_paths_by_tree_item:
            return [self._leaf_paths_by_tree_item[item]]

        paths = []
        child, cookie = tree.GetFirstChild(item)
        while child.IsOk():
            paths.extend(self.__CollectLeavesFromItem(tree, child))
            child = tree.GetNextSibling(child)

        return paths

    def __GetTreeExpandCollapseState(self, tree):
        all_expanded = True
        all_collapsed = True
        has_expandable = False

        child, cookie = tree.GetFirstChild(tree.GetRootItem())
        while child.IsOk():
            branch_expanded, branch_collapsed, branch_has_expandable = (
                self.__BranchExpandCollapseState(tree, child)
            )
            if branch_has_expandable:
                has_expandable = True
                if not branch_expanded:
                    all_expanded = False
                if not branch_collapsed:
                    all_collapsed = False
            child = tree.GetNextSibling(child)

        if not has_expandable:
            return False, False

        return all_expanded, all_collapsed

    def __BranchExpandCollapseState(self, tree, item):
        has_expandable = tree.ItemHasChildren(item)
        all_expanded = True
        all_collapsed = True

        if has_expandable:
            if tree.IsExpanded(item):
                all_collapsed = False
            else:
                all_expanded = False

        child, cookie = tree.GetFirstChild(item)
        while child.IsOk():
            child_expanded, child_collapsed, child_has_expandable = (
                self.__BranchExpandCollapseState(tree, child)
            )
            if child_has_expandable:
                has_expandable = True
                if not child_expanded:
                    all_expanded = False
                if not child_collapsed:
                    all_collapsed = False
            child = tree.GetNextSibling(child)

        return all_expanded, all_collapsed, has_expandable

    @staticmethod
    def __BuildVisibleElemPaths(leaf_elem_paths):
        visible_paths = set()
        for leaf_path in leaf_elem_paths:
            parts = leaf_path.split('.')
            for i in range(1, len(parts) + 1):
                visible_paths.add('.'.join(parts[:i]))
        return visible_paths
