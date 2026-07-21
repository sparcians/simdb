import wx
from functools import partial

class WidgetDataSelectionsDlg(wx.Dialog):
    def __init__(
        self, parent, frame, elem_paths, queues_only=False, single_selection=False,
        settings_chkboxes=None, title="Edit Data Selections",
    ):
        _, screen_h = wx.GetDisplaySize()
        super().__init__(parent, title=title, size=(600, int(screen_h * 0.75)))

        self.frame = frame
        self.simhier = frame.simhier
        self._settings_chkboxes = settings_chkboxes or []
        self._settings_chkboxes_by_label = {}
        if queues_only:
            self._all_leaf_paths = sorted(self.simhier.GetContainerElemPaths())
        else:
            self._all_leaf_paths = sorted(self.simhier.GetElemPaths(True))

        initial_paths_set = set(elem_paths)
        self._selected_paths = [p for p in elem_paths if p in self._all_leaf_paths]
        for p in self._all_leaf_paths:
            if p in initial_paths_set and p not in self._selected_paths:
                self._selected_paths.append(p)

        self._initial_paths = list(self._selected_paths)
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

            self.move_up_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_GO_UP, wx.ART_BUTTON))
            self.move_up_btn.Bind(wx.EVT_BUTTON, self.__MoveSelectedElemUp)

            self.move_down_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_GO_DOWN, wx.ART_BUTTON))
            self.move_down_btn.Bind(wx.EVT_BUTTON, self.__MoveSelectedElemDown)

        btn_sizer = wx.StdDialogButtonSizer()
        self.ok_btn = wx.Button(self, wx.ID_OK)
        btn_sizer.AddButton(self.ok_btn)
        btn_sizer.AddButton(wx.Button(self, wx.ID_CANCEL))
        btn_sizer.Realize()

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(instruction_label, 0, wx.ALL, 5)
        sizer.Add(self.hier_tree, 1, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
        if not single_selection:
            list_sizer = wx.BoxSizer(wx.HORIZONTAL)
            list_sizer.Add(self.selections_list, 1, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)

            arrow_btns_sizer = wx.BoxSizer(wx.VERTICAL)
            arrow_btns_sizer.Add(self.move_up_btn)
            arrow_btns_sizer.Add(self.move_down_btn)
            list_sizer.Add(arrow_btns_sizer)
            sizer.Add(list_sizer)

        self._BuildSettingsArea(sizer)

        sizer.Add(btn_sizer, 0, wx.ALL | wx.ALIGN_RIGHT, 10)
        self.SetSizer(sizer)

        if not single_selection:
            self.hier_tree.Bind(wx.EVT_RIGHT_DOWN, partial(self.__OnTreeRightClick, tree=self.hier_tree))
            self.selections_list.Bind(wx.EVT_LIST_ITEM_CHECKED, self.__OnListItemChecked)
            self.selections_list.Bind(wx.EVT_LIST_ITEM_UNCHECKED, self.__OnListItemUnchecked)
            self.selections_list.Bind(wx.EVT_LIST_ITEM_SELECTED, self.__UpdateButtonStates)
        else:
            self.hier_tree.Bind(wx.EVT_TREE_SEL_CHANGED, self.__OnTreeSelectionChanged)
            self.hier_tree.Bind(wx.EVT_RIGHT_DOWN, partial(self.__OnTreeRightClick, tree=self.hier_tree))

        self.__BuildTree()
        self.__BuildSelectionsList()
        self.__UpdateButtonStates()

    def _OnWidgetCheckbox(self, label, checked):
        raise RuntimeError("Not implemented")

    def GetSettingCheckbox(self, label):
        return self._settings_chkboxes_by_label[label].IsChecked()

    def _BuildSettingsArea(self, sizer):
        if not self._settings_chkboxes:
            return

        for i, (label, checked) in enumerate(self._settings_chkboxes):
            chkbox = wx.CheckBox(self, label=label)
            chkbox.SetValue(checked)
            if i == 0:
                sizer.Add(chkbox)
            else:
                sizer.Add(chkbox, 0, wx.TOP, 5)
            chkbox.Bind(wx.EVT_CHECKBOX, partial(self.__OnSettingsCheckbox, label=label))
            self._settings_chkboxes_by_label[label] = chkbox

    def __OnSettingsCheckbox(self, evt, label):
        if self._settings_chkboxes:
            self._OnWidgetCheckbox(label, evt.IsChecked())
        evt.Skip()

    def GetSelectedElemPaths(self):
        if self._single_selection:
            return [self._single_selected_path] if self._single_selected_path else []

        selected_paths = []
        for idx in range(self.selections_list.GetItemCount()):
            if self.selections_list.IsItemChecked(idx):
                selected_paths.append(self.selections_list.GetItemText(idx))

        return selected_paths

    def __RebuildSelectedPaths(self, selected_paths_set):
        kept = [p for p in self._selected_paths if p in selected_paths_set]
        for p in self._all_leaf_paths:
            if p in selected_paths_set and p not in kept:
                kept.append(p)
        self._selected_paths = kept

    def __OnListItemChecked(self, evt):
        if self._syncing_list_checkboxes:
            evt.Skip()
            return

        path = self.selections_list.GetItemText(evt.GetIndex())
        if path not in self._selected_paths:
            selected_paths = set(self._selected_paths)
            selected_paths.add(path)
            self.__RebuildSelectedPaths(selected_paths)
            self.__UpdateButtonStates()
        evt.Skip()

    def __OnListItemUnchecked(self, evt):
        if self._syncing_list_checkboxes:
            evt.Skip()
            return

        path = self.selections_list.GetItemText(evt.GetIndex())
        if path in self._selected_paths:
            selected_paths = set(self._selected_paths)
            selected_paths.remove(path)
            self.__RebuildSelectedPaths(selected_paths)
            self.__UpdateButtonStates()
        evt.Skip()

    def __OnTreeSelectionChanged(self, evt):
        item = self.hier_tree.GetSelection()
        if item.IsOk() and item in self._leaf_paths_by_tree_item:
            self._single_selected_path = self._leaf_paths_by_tree_item[item]
        else:
            self._single_selected_path = None

        self.__UpdateButtonStates()
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

    def __MoveSelectedElemUp(self, evt):
        selected_rows = self.__GetListCtrlSelectedRows()
        assert len(selected_rows) == 1
        src_row = selected_rows[0]
        assert src_row > 0
        dst_row = src_row - 1
        self.__SwapListCtrlItems(src_row, dst_row)

    def __MoveSelectedElemDown(self, evt):
        selected_rows = self.__GetListCtrlSelectedRows()
        assert len(selected_rows) == 1
        src_row = selected_rows[0]
        dst_row = src_row + 1
        assert dst_row < self.selections_list.GetItemCount()
        self.__SwapListCtrlItems(src_row, dst_row)

    def __SwapListCtrlItems(self, src_row, dst_row):
        src_text = self.selections_list.GetItemText(src_row)
        dst_text = self.selections_list.GetItemText(dst_row)
        self.selections_list.SetItemText(dst_row, src_text)
        self.selections_list.SetItemText(src_row, dst_text)

        src_checked = self.selections_list.IsItemChecked(src_row)
        dst_checked = self.selections_list.IsItemChecked(dst_row)
        self.selections_list.CheckItem(src_row, dst_checked)
        self.selections_list.CheckItem(dst_row, src_checked)

        src_selected = self.selections_list.IsSelected(src_row)
        dst_selected = self.selections_list.IsSelected(dst_row)
        self.selections_list.Select(src_row, dst_selected)
        self.selections_list.Select(dst_row, src_selected)

        self.selections_list.EnsureVisible(dst_row)

        self._selected_paths = [
            self.selections_list.GetItemText(i)
            for i in range(self.selections_list.GetItemCount())
            if self.selections_list.IsItemChecked(i)
        ]

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
            self.__RebuildSelectedPaths(selected_paths)
            self.__ApplySelectionToList()
            self.__UpdateButtonStates()

    def __UpdateButtonStates(self, *args):
        if self._single_selection:
            return

        list_ctrl_count = self.selections_list.GetItemCount()
        selected_rows = self.__GetListCtrlSelectedRows()
        if list_ctrl_count <= 1 or len(selected_rows) > 1 or len(selected_rows) == 0:
            self.move_up_btn.Disable()
            self.move_down_btn.Disable()
        else:
            selected_row = selected_rows[0]
            if selected_row == 0:
                self.move_up_btn.Disable()
                self.move_down_btn.Enable()
            elif selected_row == list_ctrl_count - 1:
                self.move_up_btn.Enable()
                self.move_down_btn.Disable()
            else:
                self.move_up_btn.Enable()
                self.move_down_btn.Enable()

    def __GetListCtrlSelectedRows(self):
        rows = []
        for idx in range(self.selections_list.GetItemCount()):
            if self.selections_list.IsSelected(idx):
                rows.append(idx)

        return rows

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
        selected = list(self._selected_paths)
        unselected = [p for p in self._all_leaf_paths if p not in selected]
        return selected + unselected

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

class QueueUtilizEditDlg(WidgetDataSelectionsDlg):
    SHOW_FULL_PATHS_LABEL = 'Show full paths'

    def __init__(self, parent, frame, elem_paths, show_full_paths, title="Edit Data Selections"):
        chkboxes = [(self.SHOW_FULL_PATHS_LABEL, show_full_paths)]
        WidgetDataSelectionsDlg.__init__(
            self, parent, frame, elem_paths, queues_only=True,
            settings_chkboxes=chkboxes, title=title,
        )

    def _OnWidgetCheckbox(self, label, checked):
        pass

    @property
    def show_full_paths(self):
        return self.GetSettingCheckbox(self.SHOW_FULL_PATHS_LABEL)

class SummaryViewsEditDlg(WidgetDataSelectionsDlg):
    SHOW_FULL_PATHS_LABEL = 'Show full paths'
    SHOW_DID_LABEL = 'Show DID'

    def __init__(self, parent, frame, elem_paths, show_full_paths, show_did, title="Edit Data Selections"):
        chkboxes = [
            (self.SHOW_FULL_PATHS_LABEL, show_full_paths),
            (self.SHOW_DID_LABEL, show_did),
        ]
        WidgetDataSelectionsDlg.__init__(
            self, parent, frame, elem_paths,
            settings_chkboxes=chkboxes, title=title,
        )

    def _OnWidgetCheckbox(self, label, checked):
        pass

    @property
    def show_full_paths(self):
        return self.GetSettingCheckbox(self.SHOW_FULL_PATHS_LABEL)

    @property
    def show_did(self):
        return self.GetSettingCheckbox(self.SHOW_DID_LABEL)

class SchedulingLinesEditDlg(WidgetDataSelectionsDlg):
    SHOW_DETAILS_LABEL = 'Show detailed queue packets'
    HIDE_EMPTY_ROWS_LABEL = 'Hide empty rows'
    SHOW_FULL_PATHS_LABEL = 'Show full paths'
    ENABLE_TOOLTIPS_LABEL = 'Enable tooltips'
    SHOW_DID_LABEL = 'Show DID'

    def __init__(
        self, parent, frame, elem_paths, num_ticks_before, num_ticks_after, show_details, hide_empty_rows, show_full_paths, enable_tooltips, show_did, title="Edit Data Selections",
    ):
        self._num_ticks_before = num_ticks_before
        self._num_ticks_after = num_ticks_after
        chkboxes = [
            (self.SHOW_DETAILS_LABEL, show_details),
            (self.HIDE_EMPTY_ROWS_LABEL, hide_empty_rows),
            (self.SHOW_FULL_PATHS_LABEL, show_full_paths),
            (self.ENABLE_TOOLTIPS_LABEL, enable_tooltips),
            (self.SHOW_DID_LABEL, show_did),
        ]
        WidgetDataSelectionsDlg.__init__(
            self, parent, frame, elem_paths, queues_only=True, settings_chkboxes=chkboxes,
        )

    def _BuildSettingsArea(self, sizer):
        assert self._num_ticks_before >= 1 and self._num_ticks_before <= 25
        info_ticks_before = wx.StaticText(self, label='Num ticks before current tick:')
        self._label_ticks_before = wx.StaticText(self, label=f'({self._num_ticks_before})')
        self._slider_ticks_before = wx.Slider(
            self, value=self._num_ticks_before, minValue=1, maxValue=25)
        self._slider_ticks_before.Bind(wx.EVT_SLIDER, self.__SyncWithSliderTicks)

        assert self._num_ticks_after >= 1 and self._num_ticks_after <= 25
        info_ticks_after = wx.StaticText(self, label='Num ticks after current tick:')
        self._label_ticks_after = wx.StaticText(self, label=f'({self._num_ticks_after})')
        self._slider_ticks_after = wx.Slider(
            self, value=self._num_ticks_after, minValue=1, maxValue=25)
        self._slider_ticks_after.Bind(wx.EVT_SLIDER, self.__SyncWithSliderTicks)

        gb_sizer = wx.GridBagSizer(vgap=10, hgap=12)
        gb_sizer.Add(info_ticks_before, pos=(0, 0))
        gb_sizer.Add(self._slider_ticks_before, pos=(0, 1), flag=wx.EXPAND)
        gb_sizer.Add(self._label_ticks_before, pos=(0, 2))

        gb_sizer.Add(info_ticks_after, pos=(1, 0))
        gb_sizer.Add(self._slider_ticks_after, pos=(1, 1), flag=wx.EXPAND)
        gb_sizer.Add(self._label_ticks_after, pos=(1, 2))

        gb_sizer.AddGrowableCol(1)
        sizer.Add(gb_sizer, 0, wx.EXPAND | wx.LEFT | wx.RIGHT, 5)

        WidgetDataSelectionsDlg._BuildSettingsArea(self, sizer)

    def _OnWidgetCheckbox(self, label, checked):
        pass

    @property
    def num_ticks_before(self):
        return self._slider_ticks_before.GetValue()

    @property
    def num_ticks_after(self):
        return self._slider_ticks_after.GetValue()

    @property
    def show_details(self):
        return self.GetSettingCheckbox(self.SHOW_DETAILS_LABEL)

    @property
    def hide_empty_rows(self):
        return self.GetSettingCheckbox(self.HIDE_EMPTY_ROWS_LABEL)

    @property
    def show_full_paths(self):
        return self.GetSettingCheckbox(self.SHOW_FULL_PATHS_LABEL)

    @property
    def enable_tooltips(self):
        return self.GetSettingCheckbox(self.ENABLE_TOOLTIPS_LABEL)

    @property
    def show_did(self):
        return self.GetSettingCheckbox(self.SHOW_DID_LABEL)

    def __UpdateButtonStates(self, *args):
        WidgetDataSelectionsDlg.__UpdateButtonStates(self, *args)

    def __SyncWithSliderTicks(self, evt):
        value = self._slider_ticks_before.GetValue()
        self._label_ticks_before.SetLabel(f'({value})')

        value = self._slider_ticks_after.GetValue()
        self._label_ticks_after.SetLabel(f'({value})')

        evt.Skip()
