import wx

class ScalarStruct(wx.Panel):
    def __init__(self, parent, frame, elem_path):
        super(ScalarStruct, self).__init__(parent)
        self.frame = frame
        self.elem_path = elem_path
        self.struct_text_elem = wx.StaticText(self, label='Scalar Struct:\n%s' % elem_path)
        self._deserializer = frame.data_retriever.GetDeserializer(elem_path)
        self._field_names = self._deserializer.GetAllFieldNames()

        font = wx.Font(10, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        self.struct_text_elem.SetFont(font)

        self.sizer = wx.BoxSizer(wx.VERTICAL)
        self.sizer.Add(self.struct_text_elem, 0, wx.EXPAND)
        self.SetSizer(self.sizer)
        self.Layout()

    def GetWidgetCreationString(self):
        return 'ScalarStruct$' + self.elem_path

    def UpdateWidgetData(self):
        widget_renderer = self.frame.widget_renderer
        tick = widget_renderer.tick
        queue_data = self.frame.data_retriever.Unpack(self.elem_path, tick)

        field_max_len = max([len(field_name) for field_name in self._field_names])
        struct_str = []
        for field_name in self._field_names:
            field_val = queue_data['DataVals'][0][field_name]
            field_name = field_name.ljust(field_max_len)
            struct_str.append('%s: %s' % (field_name, field_val))

        self.struct_text_elem.SetLabel('\n'.join(struct_str))

    def GetCurrentViewSettings(self):
        return {}
    
    def GetCurrentUserSettings(self):
        return {}

    def ApplyViewSettings(self, settings):
        pass
