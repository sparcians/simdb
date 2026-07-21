import wx, wx.grid

class Grid(wx.grid.Grid):
    def __init__(self, parent, frame, rows, cols, cell_font=None, label_font=None, cell_selection_allowed=True, **kwargs):
        super().__init__(parent, **kwargs)
        self.frame = frame
        self.renderer = GridCellRenderer(rows, cols, cell_font)

        if label_font:
            self.SetLabelFont(label_font)

        self.CreateGrid(rows, cols)
        self.EnableEditing(False)
        self.DisableDragColMove()
        self.DisableDragColSize()
        self.DisableDragRowMove()
        self.DisableDragRowSize()
        self.DisableDragGridSize()
        self.DisableCellEditControl()

        if not cell_selection_allowed:
            self.Bind(wx.grid.EVT_GRID_SELECT_CELL, lambda evt: evt.Veto())

        self.SetDefaultRenderer(self.renderer)

    def SetCellValue(self, row, col, value, immediate_refresh=False):
        self.renderer.SetCellValue(row, col, value)
        if immediate_refresh:
            self.Refresh()
            self.AutoSize()

    def GetCellValue(self, row, col):
        return self.renderer.GetCellValue(row, col)
    
    def SetCellAlignment(self, row, col, alignment):
        self.renderer.SetCellAlignment(row, col, alignment)

    def SetCellBackgroundColour(self, row, col, color, immediate_refresh=False):
        self.renderer.SetCellBackgroundColour(row, col, color)
        if immediate_refresh:
            self.Refresh()
            self.AutoSize()

    def GetCellBackgroundColour(self, row, col):
        return self.renderer.GetCellBackgroundColour(row, col)
    
    def GetCellBorderWidth(self, row, col):
        return self.renderer.GetCellBorderWidth(row, col)
    
    def GetCellBorderSide(self, row, col):
        return self.renderer.GetCellBorderSide(row, col)

    def SetCellBorder(self, row, col, border_width=1, border_side=wx.ALL, immediate_refresh=False):
        self.renderer.SetCellBorder(row, col, border_width, border_side)
        if immediate_refresh:
            self.Refresh()
            self.AutoSize()

    def SetCellBorderWidth(self, row, col, border_width, immediate_refresh=False):
        self.renderer.SetCellBorderWidth(row, col, border_width)
        if immediate_refresh:
            self.Refresh()
            self.AutoSize()

    def SetCellBorderSide(self, row, col, border_side, immediate_refresh=False):
        self.renderer.SetCellBorderSide(row, col, border_side)
        if immediate_refresh:
            self.Refresh()
            self.AutoSize()

    def RemoveCellBorder(self, row, col, immediate_refresh=False):
        self.SetCellBorder(row, col, 0, wx.ALL, immediate_refresh)

    def SetCellFont(self, row, col, font):
        self.renderer.SetCellFont(row, col, font)

    def SetCellToolTip(self, row, col, tooltip):
        self.renderer.SetCellToolTip(row, col, tooltip)

    def GetCellToolTip(self, row, col):
        return self.renderer.GetCellToolTip(row, col)
    
    def UnsetCellToolTip(self, row, col):
        self.renderer.UnsetCellToolTip(row, col)

    def ClearGrid(self):
        for row in range(self.GetNumberRows()):
            for col in range(self.GetNumberCols()):
                self.SetCellValue(row, col, '')
                self.SetCellBackgroundColour(row, col, (255, 255, 255))
                self.RemoveCellBorder(row, col)
                self.UnsetCellToolTip(row, col)

        self.Refresh()
        self.AutoSize()

class GridCellRenderer(wx.grid.GridCellRenderer):
    def __init__(self, rows, cols, font=None):
        super().__init__()
        self.cells = [[GridCell(font) for _ in range(cols)] for _ in range(rows)]

    def SetCellValue(self, row, col, value):
        self.cells[row][col].SetText(value)

    def GetCellValue(self, row, col):
        return self.cells[row][col].GetText()

    def SetCellAlignment(self, row, col, alignment):
        self.cells[row][col].AlignLabel(alignment)

    def SetCellBackgroundColour(self, row, col, color):
        self.cells[row][col].SetBackgroundColour(color)

    def GetCellBackgroundColour(self, row, col):
        return self.cells[row][col].GetBackgroundColour()
    
    def GetCellBorderWidth(self, row, col):
        return self.cells[row][col].GetBorderWidth()
    
    def GetCellBorderSide(self, row, col):
        return self.cells[row][col].GetBorderSide()

    def SetCellBorder(self, row, col, border_width, border_side):
        self.cells[row][col].SetBorder(border_width, border_side)

    def SetCellBorderWidth(self, row, col, border_width):
        self.cells[row][col].SetBorderWidth(border_width)

    def SetCellBorderSide(self, row, col, border_side):
        self.cells[row][col].SetBorderSide(border_side)

    def SetCellFont(self, row, col, font):
        self.cells[row][col].SetFont(font)

    def SetCellToolTip(self, row, col, tooltip):
        self.cells[row][col].SetToolTip(tooltip)

    def GetCellToolTip(self, row, col):
        return self.cells[row][col].GetToolTip()
    
    def UnsetCellToolTip(self, row, col):
        self.cells[row][col].UnsetToolTip()

    def Draw(self, grid, attr, dc, rect, row, col, isSelected):
        self.cells[row][col].Draw(grid, dc, rect)

    def GetBestSize(self, grid, attr, dc, row, col):
        return self.cells[row][col].GetBestSize(grid, attr, dc, row, col)

class GridCell:
    def __init__(self, font=None):
        self.text = ''
        self.text_alignment = wx.ALIGN_CENTER
        self.font = font if font else wx.Font(12, wx.FONTFAMILY_MODERN, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL)
        self.background_colour = (255, 255, 255)
        self.border_width = 0
        self.border_side = wx.ALL
        self.tooltip = None

    def SetText(self, text):
        self.text = text

    def GetText(self):
        return self.text
    
    def AlignLabel(self, alignment):
        self.text_alignment = alignment

    def SetFont(self, font):
        self.font = font
    
    def SetBackgroundColour(self, color):
        self.background_colour = color

    def GetBackgroundColour(self):
        return self.background_colour
    
    def GetBorderWidth(self):
        return self.border_width
    
    def GetBorderSide(self):
        return self.border_side
    
    def SetBorder(self, border_width, border_side):
        self.border_width = border_width
        self.border_side = border_side if border_width else wx.ALL

    def SetBorderWidth(self, border_width):
        self.border_width = border_width

    def SetBorderSide(self, border_side):
        self.border_side = border_side

    def SetToolTip(self, tooltip):
        if tooltip in (None, ''):
            self.UnsetToolTip()
        else:
            self.tooltip = tooltip

    def GetToolTip(self):
        return self.tooltip
    
    def UnsetToolTip(self):
        self.tooltip = None

    def Draw(self, grid, dc, rect):
        dc.SetBrush(wx.Brush(self.background_colour))
        dc.SetPen(wx.Pen(wx.TRANSPARENT_PEN))
        dc.DrawRectangle(rect)

        if self.text:
            dc.SetFont(self.font)
            dc.DrawLabel(self.text, rect, self.text_alignment)

        if self.border_width:
            dc.SetPen(wx.Pen(wx.BLACK, self.border_width))
            if self.border_side & wx.TOP:
                dc.DrawLine(rect.GetLeft(), rect.GetTop(), rect.GetRight(), rect.GetTop())
            if self.border_side & wx.BOTTOM:
                dc.DrawLine(rect.GetLeft(), rect.GetBottom(), rect.GetRight(), rect.GetBottom())
            if self.border_side & wx.LEFT:
                dc.DrawLine(rect.GetLeft(), rect.GetTop(), rect.GetLeft(), rect.GetBottom())
            if self.border_side & wx.RIGHT:
                dc.DrawLine(rect.GetRight(), rect.GetTop(), rect.GetRight(), rect.GetBottom())

    def GetBestSize(self, grid, attr, dc, row, col):
        if not self.text:
            return (dc.GetTextExtent(' ')[0], 5)

        dc.SetFont(self.font)
        w,h = dc.GetTextExtent(self.text)
        w += 2
        h += 2
        return wx.Size(w, h)
