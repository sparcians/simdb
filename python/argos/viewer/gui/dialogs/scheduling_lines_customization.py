import wx, copy, re
from collections import OrderedDict

class SchedulingLinesCustomizationDlg(wx.Dialog):
    def __init__(self, parent, caption_mgr, num_ticks_before, num_ticks_after, show_detailed_queue_packets, enable_tooltips):
        super().__init__(parent, title="Customize Scheduling Lines")

        self.caption_mgr = copy.deepcopy(caption_mgr)
        self.show_detailed_queue_packets = show_detailed_queue_packets
        self.enable_tooltips = enable_tooltips
        self.ok_btn = None

        self.move_up_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_GO_UP, wx.ART_BUTTON))
        self.move_up_btn.Bind(wx.EVT_BUTTON, self.__MoveSelectedElemUp)

        self.move_down_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_GO_DOWN, wx.ART_BUTTON))
        self.move_down_btn.Bind(wx.EVT_BUTTON, self.__MoveSelectedElemDown)

        self.remove_btn = wx.BitmapButton(self, bitmap=wx.ArtProvider.GetBitmap(wx.ART_DELETE, wx.ART_BUTTON))
        self.remove_btn.Bind(wx.EVT_BUTTON, self.__RemoveSelectedElems)
        self.remove_btn.Disable()

        edit_btns_sizer = wx.BoxSizer(wx.VERTICAL)
        edit_btns_sizer.Add(self.move_up_btn, 0, wx.ALL, 5)
        edit_btns_sizer.Add(self.move_down_btn, 0, wx.ALL, 5)
        edit_btns_sizer.Add(self.remove_btn, 0, wx.ALL, 5)

        self.element_path_regexes_list_ctrl = wx.ListCtrl(self, style=wx.LC_REPORT)
        self.element_path_regexes_list_ctrl.InsertColumn(0, "Path Regex")
        self.element_path_regexes_list_ctrl.InsertColumn(1, "Caption")
        self.element_path_regexes_list_ctrl.Bind(wx.EVT_LIST_ITEM_SELECTED, self.__OnListCtrlItemSelected)
        self.element_path_regexes_list_ctrl.Bind(wx.EVT_LIST_ITEM_DESELECTED, self.__OnListCtrlItemSelected)
        self.element_path_regexes_list_ctrl.Bind(wx.EVT_LEFT_DCLICK, self.__OnListCtrlItemDClicked)
        self.element_path_regexes_list_ctrl.Bind(wx.EVT_LIST_END_LABEL_EDIT, self.__ValidateRegexSettings)

        element_path_caption_regexes = self.caption_mgr.GetElemPathRegexReplacements()
        idx = 0
        for path_regex, caption_replacements in element_path_caption_regexes.items():
            self.element_path_regexes_list_ctrl.InsertItem(idx, path_regex)
            self.element_path_regexes_list_ctrl.SetItem(idx, 1, caption_replacements)

            if len(element_path_caption_regexes) == 1:
                self.move_up_btn.Disable()
                self.move_down_btn.Disable()
            else:
                idx += 1

        hsizer = wx.BoxSizer(wx.HORIZONTAL)
        hsizer.Add(self.element_path_regexes_list_ctrl, 1, wx.ALL | wx.EXPAND, 5)
        hsizer.Add(edit_btns_sizer)

        self.ok_btn = wx.Button(self, wx.ID_OK)
        self.cancel_btn = wx.Button(self, wx.ID_CANCEL)

        exit_btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        exit_btn_sizer.Add(self.ok_btn, 0, wx.ALL | wx.EXPAND, 5)
        exit_btn_sizer.Add(self.cancel_btn, 0, wx.ALL | wx.EXPAND, 5)

        num_ticks_before_min_val = 1
        num_ticks_before_max_val = 25
        num_ticks_after_min_val = 5
        num_ticks_after_max_val = 100

        num_ticks_before_info_label = wx.StaticText(self, label='Cycles to show before current tick:')
        num_ticks_after_info_label = wx.StaticText(self,  label='Cycles to show after current tick:')

        self.num_ticks_before_value_label = wx.StaticText(self, label=str(num_ticks_before))
        self.num_ticks_after_value_label = wx.StaticText(self, label=str(num_ticks_after))

        num_ticks_before_min_val_text_label = wx.StaticText(self, label=str(num_ticks_before_min_val))
        num_ticks_before_max_val_text_label = wx.StaticText(self, label=str(num_ticks_before_max_val))
        num_ticks_after_min_val_text_label = wx.StaticText(self, label=str(num_ticks_after_min_val))
        num_ticks_after_max_val_text_label = wx.StaticText(self, label=str(num_ticks_after_max_val))

        self.num_ticks_before_slider = wx.Slider(self, minValue=num_ticks_before_min_val, maxValue=num_ticks_before_max_val, value=num_ticks_before)
        self.num_ticks_after_slider = wx.Slider(self, minValue=num_ticks_after_min_val, maxValue=num_ticks_after_max_val, value=num_ticks_after)

        self.num_ticks_before_slider.Bind(wx.EVT_SCROLL, self.__OnNumTicksBeforeSliderChanged)
        self.num_ticks_after_slider.Bind(wx.EVT_SCROLL, self.__OnNumTicksAfterSliderChanged)

        w,h = self.num_ticks_before_slider.GetSize()
        self.num_ticks_before_slider.SetMinSize((300, h))

        w,h = self.num_ticks_after_slider.GetSize()
        self.num_ticks_after_slider.SetMinSize((300, h))

        num_ticks_sizer = wx.FlexGridSizer(rows=2, cols=6, hgap=0, vgap=0)
        num_ticks_sizer.Add(num_ticks_before_info_label)
        num_ticks_sizer.Add(self.num_ticks_before_value_label, 0, wx.LEFT, 20)
        num_ticks_sizer.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.LEFT | wx.EXPAND, 20)
        num_ticks_sizer.Add(num_ticks_before_min_val_text_label, 0, wx.LEFT, 20)
        num_ticks_sizer.Add(self.num_ticks_before_slider, 0, wx.TOP | wx.EXPAND, -7)
        num_ticks_sizer.Add(num_ticks_before_max_val_text_label)

        num_ticks_sizer.Add(num_ticks_after_info_label)
        num_ticks_sizer.Add(self.num_ticks_after_value_label, 0, wx.LEFT, 20)
        num_ticks_sizer.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.LEFT | wx.EXPAND, 20)
        num_ticks_sizer.Add(num_ticks_after_min_val_text_label, 0, wx.LEFT, 20)
        num_ticks_sizer.Add(self.num_ticks_after_slider, 0, wx.TOP | wx.EXPAND, -7)
        num_ticks_sizer.Add(num_ticks_after_max_val_text_label)

        show_detailed_queue_packets_checkbox = wx.CheckBox(self, label='Show detailed queue packets')
        show_detailed_queue_packets_checkbox.SetValue(show_detailed_queue_packets)
        show_detailed_queue_packets_checkbox.Bind(wx.EVT_CHECKBOX, self.__OnShowDetailedQueuePacketsChanged)

        enable_tooltips_checkbox = wx.CheckBox(self, label='Enable tooltips')
        enable_tooltips_checkbox.SetValue(enable_tooltips)
        enable_tooltips_checkbox.Bind(wx.EVT_CHECKBOX, self.__OnEnableTooltipsChanged)

        regex_example_label = wx.StaticText(self, label='Regex examples:')
        regex_example_text = wx.StaticText(self, label='top.cpu.core([0-9]+).rob.stats.num_insts_retired')
        regex_example_text2 = wx.StaticText(self, label='top.cpu.core0.rob.stats.ipc')

        caption_example_label = wx.StaticText(self, label='Caption examples:')
        caption_example_text = wx.StaticText(self, label='NumInstsRetired\\1')
        caption_example_text2 = wx.StaticText(self, label='IPC')

        font = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        regex_example_label.SetFont(font.Bold())
        regex_example_text.SetFont(font)
        regex_example_text2.SetFont(font)
        caption_example_label.SetFont(font.Bold())
        caption_example_text.SetFont(font)
        caption_example_text2.SetFont(font)
        self.element_path_regexes_list_ctrl.SetFont(font)

        dc = wx.ScreenDC()
        dc.SetFont(self.element_path_regexes_list_ctrl.GetFont())
        col0_width = max([dc.GetTextExtent(s)[0] for s in element_path_caption_regexes.keys()]) + 50
        col1_width = max([dc.GetTextExtent(s)[0] for s in element_path_caption_regexes.values()]) + 50

        col0_width = max(col0_width, dc.GetTextExtent(regex_example_text.GetLabel())[0])
        col0_width += dc.GetTextExtent('([0-9]+)')[0]

        hgap = col0_width - dc.GetTextExtent(regex_example_text.GetLabel())[0]
        example_sizer = wx.FlexGridSizer(rows=3, cols=2, hgap=hgap, vgap=0)
        example_sizer.Add(regex_example_label)
        example_sizer.Add(caption_example_label)
        example_sizer.Add(regex_example_text)
        example_sizer.Add(caption_example_text)
        example_sizer.Add(regex_example_text2)
        example_sizer.Add(caption_example_text2)

        vsizer = wx.BoxSizer(wx.VERTICAL)
        vsizer.Add(hsizer, 1, wx.ALL | wx.EXPAND, 5)
        vsizer.Add(example_sizer, 0, wx.ALL | wx.EXPAND, 5)
        vsizer.AddSpacer(10)
        vsizer.Add(num_ticks_sizer, 0, wx.ALL | wx.EXPAND, 5)
        vsizer.Add(show_detailed_queue_packets_checkbox, 0, wx.ALL | wx.EXPAND, 5)
        vsizer.Add(enable_tooltips_checkbox, 0, wx.ALL | wx.EXPAND, 5)
        vsizer.Add(exit_btn_sizer, 0, wx.ALL | wx.EXPAND, 5)

        self.errors_label = wx.StaticText(self, label='No issues found', size=(col0_width,-1))
        hsizer2 = wx.BoxSizer(wx.HORIZONTAL)
        hsizer2.Add(vsizer)
        hsizer2.AddSpacer(10)
        hsizer2.Add(wx.StaticLine(self, style=wx.LI_VERTICAL), 0, wx.EXPAND)
        hsizer2.AddSpacer(10)
        hsizer2.Add(self.errors_label)

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(hsizer2, 1, wx.ALL | wx.EXPAND, 5)
        self.SetSizer(sizer)
        self.Layout()

        self.element_path_regexes_list_ctrl.SetColumnWidth(0, col0_width)
        self.element_path_regexes_list_ctrl.SetColumnWidth(1, col1_width)

        dlg_width  = col0_width + col1_width
        dlg_min_width = dlg_width + hsizer.CalcMin()[0] + 50
        dlg_min_height = sizer.CalcMin()[1] + 75
        self.SetSize((dlg_min_width, dlg_min_height))
        self.Layout()

    @property
    def frame(self):
        return self.GetParent().frame

    def GetNumTicksBefore(self):
        return self.num_ticks_before_slider.GetValue()

    def GetNumTicksAfter(self):
        return self.num_ticks_after_slider.GetValue()

    def ShowDetailedQueuePackets(self):
        return self.show_detailed_queue_packets

    def EnableTooltips(self):
        return self.enable_tooltips
    
    def GetElementPathCaptionRegexes(self, as_list=False):
        regexes = OrderedDict()
        for row in range(self.element_path_regexes_list_ctrl.GetItemCount()):
            path_regex = self.element_path_regexes_list_ctrl.GetItemText(row, 0)
            caption_replacements = self.element_path_regexes_list_ctrl.GetItemText(row, 1)
            regexes[path_regex] = caption_replacements

        if as_list:
            return list(regexes.items())

        return regexes

    def __OnListCtrlItemSelected(self, evt):
        selected_elem_idxs = self.__GetListCtrlSelectedItemIdxs()
        if len(selected_elem_idxs) == 0:
            self.move_up_btn.Disable()
            self.move_down_btn.Disable()
            self.remove_btn.Disable()
            return

        if len(selected_elem_idxs) != 1:
            self.move_up_btn.Disable()
            self.move_down_btn.Disable()

            if len(selected_elem_idxs) > 0:
                self.remove_btn.Enable()
            else:
                self.remove_btn.Disable()

            return

        selected_elem_idx = selected_elem_idxs[0]
        if selected_elem_idx == wx.NOT_FOUND:
            self.move_up_btn.Disable()
            self.move_down_btn.Disable()
            self.remove_btn.Disable()
            return
        else:
            self.remove_btn.Enable()

            if selected_elem_idx == 0:
                self.move_up_btn.Disable()
            else:
                self.move_up_btn.Enable()

            if selected_elem_idx == self.element_path_regexes_list_ctrl.GetItemCount() - 1:
                self.move_down_btn.Disable()
            else:
                self.move_down_btn.Enable()

    def __MoveSelectedElemUp(self, evt):
        selected_elem_idxs = self.__GetListCtrlSelectedItemIdxs()
        assert len(selected_elem_idxs) == 1
        selected_elem_idx = selected_elem_idxs[0]
        assert selected_elem_idx > 0

        orig_top_idx = selected_elem_idx-1
        orig_bottom_idx = selected_elem_idx

        orig_top_item_col0_text = self.element_path_regexes_list_ctrl.GetItemText(orig_top_idx, 0)
        orig_top_item_col1_text = self.element_path_regexes_list_ctrl.GetItemText(orig_top_idx, 1)

        orig_bottom_item_col0_text = self.element_path_regexes_list_ctrl.GetItemText(orig_bottom_idx, 0)
        orig_bottom_item_col1_text = self.element_path_regexes_list_ctrl.GetItemText(orig_bottom_idx, 1)

        self.element_path_regexes_list_ctrl.SetItem(orig_top_idx, 0, orig_bottom_item_col0_text)
        self.element_path_regexes_list_ctrl.SetItem(orig_top_idx, 1, orig_bottom_item_col1_text)

        self.element_path_regexes_list_ctrl.SetItem(orig_bottom_idx, 0, orig_top_item_col0_text)
        self.element_path_regexes_list_ctrl.SetItem(orig_bottom_idx, 1, orig_top_item_col1_text)

        self.element_path_regexes_list_ctrl.Select(orig_bottom_idx, False)
        self.element_path_regexes_list_ctrl.Select(orig_top_idx, True)

        # The list ctrl rows have already been swapped above, so read it back in
        # the new order and sync the caption manager (no extra swap).
        self.caption_mgr.SetElemPathRegexReplacements(self.GetElementPathCaptionRegexes())

    def __MoveSelectedElemDown(self, evt):
        selected_elem_idxs = self.__GetListCtrlSelectedItemIdxs()
        assert len(selected_elem_idxs) == 1
        selected_elem_idx = selected_elem_idxs[0]
        assert selected_elem_idx < self.element_path_regexes_list_ctrl.GetItemCount() - 1

        orig_top_idx = selected_elem_idx
        orig_bottom_idx = selected_elem_idx+1

        orig_top_item_col0_text = self.element_path_regexes_list_ctrl.GetItemText(orig_top_idx, 0)
        orig_top_item_col1_text = self.element_path_regexes_list_ctrl.GetItemText(orig_top_idx, 1)

        orig_bottom_item_col0_text = self.element_path_regexes_list_ctrl.GetItemText(orig_bottom_idx, 0)
        orig_bottom_item_col1_text = self.element_path_regexes_list_ctrl.GetItemText(orig_bottom_idx, 1)

        self.element_path_regexes_list_ctrl.SetItem(orig_top_idx, 0, orig_bottom_item_col0_text)
        self.element_path_regexes_list_ctrl.SetItem(orig_top_idx, 1, orig_bottom_item_col1_text)

        self.element_path_regexes_list_ctrl.SetItem(orig_bottom_idx, 0, orig_top_item_col0_text)
        self.element_path_regexes_list_ctrl.SetItem(orig_bottom_idx, 1, orig_top_item_col1_text)

        self.element_path_regexes_list_ctrl.Select(orig_top_idx, False)
        self.element_path_regexes_list_ctrl.Select(orig_bottom_idx, True)

        # The list ctrl rows have already been swapped above, so read it back in
        # the new order and sync the caption manager (no extra swap).
        self.caption_mgr.SetElemPathRegexReplacements(self.GetElementPathCaptionRegexes())

    def __RemoveSelectedElems(self, evt):
        selected_elem_idxs = self.__GetListCtrlSelectedItemIdxs()
        selected_elem_idxs.reverse()

        for i in selected_elem_idxs:
            self.element_path_regexes_list_ctrl.DeleteItem(i)

        for i in range(self.element_path_regexes_list_ctrl.GetItemCount()):
            self.element_path_regexes_list_ctrl.Select(i, False)

        element_path_caption_regexes = self.GetElementPathCaptionRegexes()
        self.caption_mgr.SetElemPathRegexReplacements(element_path_caption_regexes)
        self.remove_btn.Disable()

    def __GetListCtrlSelectedItemIdxs(self):
        idxs = []
        item_count = self.element_path_regexes_list_ctrl.GetItemCount()

        for i in range(item_count):
            if self.element_path_regexes_list_ctrl.GetItemState(i, wx.LIST_STATE_SELECTED):
                idxs.append(i)

        return idxs

    def __OnShowDetailedQueuePacketsChanged(self, evt):
        self.show_detailed_queue_packets = evt.IsChecked()

    def __OnEnableTooltipsChanged(self, evt):
        self.enable_tooltips = evt.IsChecked()

    def __OnListCtrlItemDClicked(self, evt):
        # Get the mouse position in the list control
        mouse_x, mouse_y = evt.GetPosition()

        hit = self.element_path_regexes_list_ctrl.HitTest((mouse_x, mouse_y))
        if not hit or hit[0] == -1:
            return
        
        row = hit[0]
        if mouse_x < self.element_path_regexes_list_ctrl.GetColumnWidth(0):
            col = 0
        elif mouse_x > self.element_path_regexes_list_ctrl.GetColumnWidth(0):
            col = 1
        else:
            return

        row_rect = self.element_path_regexes_list_ctrl.GetItemRect(row)
        if col == 0:
            cell_rect = wx.Rect(row_rect.GetLeft(),
                                row_rect.GetTop(),
                                self.element_path_regexes_list_ctrl.GetColumnWidth(0),
                                row_rect.GetHeight())
        else:
            cell_rect = wx.Rect(row_rect.GetLeft() + self.element_path_regexes_list_ctrl.GetColumnWidth(0),
                                row_rect.GetTop(),
                                self.element_path_regexes_list_ctrl.GetColumnWidth(1),
                                row_rect.GetHeight())

        pos = cell_rect.GetTopLeft()
        size = cell_rect.GetSize()
        self.__EditListCtrlCell(row, col, pos, size)

    def __EditListCtrlCell(self, row, col, text_ctrl_pos, text_ctrl_size):
        # Get the current value of the cell
        current_text = self.element_path_regexes_list_ctrl.GetItemText(row, col)

        # Create a text control to edit the cell
        self.text_ctrl = wx.TextCtrl(self, value=current_text, style=wx.TE_PROCESS_ENTER, pos=text_ctrl_pos, size=text_ctrl_size)

        # Callback when editing the cell
        self.text_ctrl.Bind(wx.EVT_TEXT, lambda evt: self.__OnListCtrlEdit(evt, row, col, current_text))

        # Callbacks when finished editing the cell
        self.text_ctrl.Bind(wx.EVT_TEXT_ENTER, lambda evt: self.__OnListCtrlEditComplete(evt, row, col, current_text))
        self.text_ctrl.Bind(wx.EVT_KILL_FOCUS, lambda evt: self.__OnListCtrlEditComplete(evt, row, col, current_text))

        # Focus the text control right away
        self.text_ctrl.SetFocus()

    def __OnListCtrlEdit(self, evt, row, col, current_text):
        if col == 0:
            regex = self.text_ctrl.GetValue()
            try:
                re.compile(regex)
                self.text_ctrl.SetBackgroundColour(wx.WHITE)
                evt.Skip()
            except:
                self.text_ctrl.SetBackgroundColour((255, 192, 203)) # Pink
        else:
            regex = self.element_path_regexes_list_ctrl.GetItemText(row, 0)
            caption = self.text_ctrl.GetValue()
            valid = False

            for elem_path in self.frame.simhier.GetContainerElemPaths():
                try:
                    re.compile(regex)
                    re.sub(regex, caption, elem_path)
                    self.text_ctrl.SetBackgroundColour(wx.WHITE)
                    evt.Skip()
                    valid = True
                    break
                except:
                    pass

            if not valid:
                self.text_ctrl.SetBackgroundColour((255, 192, 203)) # Pink

    def __OnListCtrlEditComplete(self, evt, row, col, orig_text):
        if not self.text_ctrl:
            return

        self.text_ctrl.Unbind(wx.EVT_TEXT_ENTER)
        self.text_ctrl.Unbind(wx.EVT_KILL_FOCUS)
        evt.Skip()

        new_value = self.text_ctrl.GetValue()

        def DestroyTextCtrl(text_ctrl):
            text_ctrl.Destroy()
            text_ctrl = None

        wx.CallAfter(DestroyTextCtrl, self.text_ctrl)

        def SetListCtrlItem(list_ctrl, row, col, text):
            list_ctrl.SetItem(row, col, text)

        if self.text_ctrl.GetBackgroundColour() == (255, 192, 203):
            # The regex or caption is invalid. Revert to the original text.
            wx.CallAfter(SetListCtrlItem, self.element_path_regexes_list_ctrl, row, col, orig_text)
        else:
            # The regex or caption is valid. Update the list control.
            wx.CallAfter(SetListCtrlItem, self.element_path_regexes_list_ctrl, row, col, new_value)

        wx.CallAfter(self.__ValidateRegexSettings, None)

    def __OnNumTicksBeforeSliderChanged(self, evt):
        self.num_ticks_before_value_label.SetLabel(str(self.num_ticks_before_slider.GetValue()))

    def __OnNumTicksAfterSliderChanged(self, evt):
        self.num_ticks_after_value_label.SetLabel(str(self.num_ticks_after_slider.GetValue()))

    def __ValidateRegexSettings(self, evt):
        if not self.ok_btn:
            return

        orig_regex_replacements = self.caption_mgr.GetElemPathRegexReplacements()
        validate_regex_replacements = OrderedDict()
        for row in range(self.element_path_regexes_list_ctrl.GetItemCount()):
            path_regex = self.element_path_regexes_list_ctrl.GetItemText(row, 0)
            caption_replacements = self.element_path_regexes_list_ctrl.GetItemText(row, 1)
            validate_regex_replacements[path_regex] = caption_replacements

        self.caption_mgr.SetElemPathRegexReplacements(validate_regex_replacements)

        error_msgs = []
        for row in range(self.element_path_regexes_list_ctrl.GetItemCount()):
            path_regex = self.element_path_regexes_list_ctrl.GetItemText(row, 0)
            caption_replacements = self.element_path_regexes_list_ctrl.GetItemText(row, 1)

            try:
                re.compile(path_regex)
            except:
                error_msgs.append('Invalid regex in row {}:\n  {}'.format(row, path_regex))
                continue

            replacements_valid = False
            for elem_path in self.frame.simhier.GetContainerElemPaths():
                try:
                    re.sub(path_regex, caption_replacements, elem_path)
                    replacements_valid = True
                    break
                except:
                    continue

            if not replacements_valid:
                error_msgs.append('Invalid replacements in row {}:\n  {}'.format(row, caption_replacements))

        caption_prefixes = set()
        for elem_path in self.frame.simhier.GetContainerElemPaths():
            caption_prefix = self.caption_mgr.GetCaptionPrefix(elem_path)
            if caption_prefix is None:
                continue

            if caption_prefix in caption_prefixes:
                error_msgs.append('Duplicate caption prefix found: {}'.format(caption_prefix))
            else:
                caption_prefixes.add(caption_prefix)

        if len(error_msgs) > 0:
            self.ok_btn.Disable()
            self.caption_mgr.SetElemPathRegexReplacements(orig_regex_replacements)
            self.errors_label.SetLabel('\n'.join(error_msgs))
            self.errors_label.SetForegroundColour(wx.RED)

            current_font = self.errors_label.GetFont()

            # Create a new font based on the current one but bold
            bold_font = wx.Font(current_font.GetPointSize(),
                                current_font.GetFamily(),
                                current_font.GetStyle(),
                                wx.FONTWEIGHT_BOLD)
            
            self.errors_label.SetFont(bold_font)
        else:
            self.ok_btn.Enable()
            self.errors_label.SetLabel('No issues found')
            self.errors_label.SetForegroundColour(wx.BLACK)

            current_font = self.errors_label.GetFont()

            # Create a new font based on the current one but without bold
            normal_font = wx.Font(current_font.GetPointSize(),
                                  current_font.GetFamily(),
                                  current_font.GetStyle(),
                                  wx.FONTWEIGHT_NORMAL)
            
            self.errors_label.SetFont(normal_font)
