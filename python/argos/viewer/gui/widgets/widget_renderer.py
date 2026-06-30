import wx, copy
from viewer.gui import autocoloring

class WidgetRenderer:
    def __init__(self, frame):
        self.frame = frame
        cursor = frame.db.cursor()
        cursor.execute('SELECT MIN(Timestamp), MAX(Timestamp) FROM Timestamps')
        self._start_tick, self._end_tick = cursor.fetchone()

        # Recall that uint64_t is stored as a string
        if isinstance(self._start_tick, str):
            self._start_tick = int(self._start_tick)
            self._end_tick = int(self._end_tick)

        self._current_tick = self._start_tick
        self._utiliz_handler = IterableUtiliz(self, frame.simhier)
        self._auto_colors_by_key = {}
        self._auto_tags_by_key = {}
        autocoloring.BuildBrushes('default', 'default')

        self._auto_tag_list = []

        # First part: A to Z
        for char in range(ord('A'), ord('Z') + 1):
            self._auto_tag_list.append(chr(char))

        # Second part: Aa to Zz
        for char in range(ord('A'), ord('Z') + 1):
            for suffix in range(ord('a'), ord('z') + 1):
                self._auto_tag_list.append(chr(char) + chr(suffix))

    @property
    def tick(self):
        return self._current_tick

    @property
    def start_tick(self):
        return self._start_tick

    @property
    def end_tick(self):
        return self._end_tick
    
    @property
    def utiliz_handler(self):
        return self._utiliz_handler
    
    def GetCurrentViewSettings(self):
        return {}
    
    def ApplyViewSettings(self, settings):
        pass

    def GetCurrentUserSettings(self):
        settings = {}
        settings['auto_colors_by_key'] = copy.deepcopy(self._auto_colors_by_key)
        settings['auto_tags_by_key'] = copy.deepcopy(self._auto_tags_by_key)
        return settings
    
    def ApplyUserSettings(self, settings):
        auto_colors_by_key = settings.get('auto_colors_by_key', {})
        auto_tags_by_key = settings.get('auto_tags_by_key', {})

        if auto_colors_by_key == self._auto_colors_by_key and auto_tags_by_key == self._auto_tags_by_key:
            return

        self._auto_colors_by_key = copy.deepcopy(settings.get('auto_colors_by_key', {}))
        self._auto_tags_by_key = copy.deepcopy(settings.get('auto_tags_by_key', {}))
        self.frame.inspector.RefreshWidgetsOnAllTabs()

    def ResetToDefaultViewSettings(self, update_widgets=True):
        self._auto_colors_by_key = {}
        self._auto_tags_by_key = {}
        if update_widgets:
            self.frame.inspector.RefreshWidgetsOnAllTabs()

    def GoToTick(self, tick, update_widgets=True):
        tick = min(max(tick, self._start_tick), self._end_tick)
        self._current_tick = tick
        self.frame.playback_bar.SyncControls(tick)

        if update_widgets:
            self.__UpdateWidgetsOnCurrentTab()

    def GoToStart(self):
        self.GoToTick(self._start_tick)

    def GoToEnd(self):
        self.GoToTick(self._end_tick)

    def GetAutoColor(self, value):
        if value in self._auto_colors_by_key:
            return self._auto_colors_by_key[value]

        brushes = autocoloring.BACKGROUND_BRUSHES
        idx = len(self._auto_colors_by_key) % len(brushes)
        color = brushes[idx].GetColour()
        self._auto_colors_by_key[value] = (color.Red(), color.Green(), color.Blue())
        return color

    def GetAutoTag(self, value):
        if value in self._auto_tags_by_key:
            return self._auto_tags_by_key[value]

        idx = len(self._auto_tags_by_key) % len(self._auto_tag_list)
        tag = self._auto_tag_list[idx]
        self._auto_tags_by_key[value] = tag
        return tag

    def __UpdateWidgetsOnCurrentTab(self):
        notebook = self.frame.inspector
        page_idx = notebook.GetSelection()
        page = notebook.GetPage(page_idx)
        page.UpdateWidgets()

class IterableUtiliz:
    def __init__(self, widget_renderer, simhier):
        self.widget_renderer = widget_renderer
        self.simhier = simhier
        self._utiliz_pcts_by_elem_path = {}

    def GetUtilizPct(self, elem_path):
        self.__CacheUtilizValues()
        return self._utiliz_pcts_by_elem_path.get(elem_path, 0)

    def GetUtilizColor(self, elem_path):
        self.__CacheUtilizValues()
        utiliz_pct = self._utiliz_pcts_by_elem_path.get(elem_path, 0)
        return self.__GetColorForUtilizPct(utiliz_pct)
    
    def ConvertUtilizPctToColor(self, utiliz_pct):
        return self.__GetColorForUtilizPct(utiliz_pct)

    def CreateUtilizImageList(self):
        image_list = wx.ImageList(16, 16)
        for utiliz_pct in range(0, 101):
            utiliz_color = self.__GetColorForUtilizPct(utiliz_pct / 100)
            utiliz_bitmap = self.__CreateBitmap(utiliz_color)
            image_list.Add(utiliz_bitmap)

        # We reserve a utiliz value of -1% to represent timeseries data (no utiliz for non-queues)
        utiliz_color = (255, 255, 255)
        utiliz_bitmap = self.__CreateBitmap(utiliz_color, draw_x_for_empty=True)
        image_list.Add(utiliz_bitmap)

        return image_list

    def __CacheUtilizValues(self):
        # We need to get the number of data elements in each elem_path blob at this time step.
        data_retriever = self.widget_renderer.frame.data_retriever
        queue_sizes_by_collection_id = data_retriever.GetIterableSizesByCollectionID(self.widget_renderer.tick)

        self._utiliz_pcts_by_elem_path = {}
        for elem_path in self.simhier.GetItemElemPaths():
            collection_id = self.simhier.GetCollectionID(elem_path)
            if collection_id not in queue_sizes_by_collection_id:
                continue

            capacity = self.simhier.GetCapacityByCollectionID(collection_id)
            self._utiliz_pcts_by_elem_path[elem_path] = queue_sizes_by_collection_id[collection_id] / capacity

    def __GetColorForUtilizPct(self, utiliz_pct):
        """
        Maps a floating point value from 0 to 1 to an RGB color for a heatmap.
        The gradient transitions from white to red.
        
        Args:
        - utiliz_pct (float): A floating point value between 0 and 1.
        
        Returns:
        - tuple: An (R, G, B) color value where each component is in the range [0, 255].
        """

        # Cap the input value to [0,1]
        utiliz_pct = max(0, min(1, utiliz_pct))

        # Define the color transition: White (low) to Red (high)
        # Interpolating between white (255, 255, 255) and red (255, 0, 0)
        
        # White color components
        white = (255, 255, 255)
        # Red color components
        red = (255, 0, 0)
        
        # Interpolate between white and red based on the value
        r = int(white[0] * (1 - utiliz_pct) + red[0] * utiliz_pct)
        g = int(white[1] * (1 - utiliz_pct) + red[1] * utiliz_pct)
        b = int(white[2] * (1 - utiliz_pct) + red[2] * utiliz_pct)
        
        return (r, g, b)

    def __CreateBitmap(self, utiliz_color, size=(16, 16), draw_x_for_empty=False):
        """
        Creates a bitmap from an RGB color.
        
        Args:
        - utiliz_color (tuple): An (R, G, B) color value where each component is in the range [0, 255].
        - size (tuple): Size of the bitmap (width, height).
        
        Returns:
        - wx.Bitmap: A bitmap representing the specified color.
        """
        border_width = 1
        bordered_image_width, bordered_image_height = size
        interior_image_width = bordered_image_width - 2 * border_width
        interior_image_height = bordered_image_height - 2 * border_width

        interior_image = wx.Image(interior_image_width, interior_image_height)
        interior_image.SetRGB(wx.Rect(0, 0, interior_image_width, interior_image_height), *utiliz_color)

        # Create a new image with a black border
        bordered_image = wx.Bitmap(bordered_image_width, bordered_image_height)

        mem_dc = wx.MemoryDC(bordered_image)
        mem_dc.SetBackground(wx.Brush(utiliz_color))  # Background color for the bordered image
        mem_dc.Clear()

        # Draw the black border
        mem_dc.SetBrush(wx.Brush(wx.BLACK))
        mem_dc.DrawRectangle(0, 0, bordered_image_width, bordered_image_height)

        # Draw the original image with an offset for the border
        mem_dc.DrawBitmap(interior_image.ConvertToBitmap(), border_width, border_width)

        # Draw a diagonal line from the top-left to the bottom-right and bottom-left to the top-right
        # for empty queues.
        if tuple(utiliz_color) == (255, 255, 255) and draw_x_for_empty:
            mem_dc.SetPen(wx.Pen(wx.BLACK, 1))
            mem_dc.DrawLine(1, 1, interior_image_width, interior_image_height)  # Top-left to bottom-right
            mem_dc.DrawLine(1, interior_image_height, interior_image_width, 1)  # Bottom-left to top-right

        # Clean up
        mem_dc.SelectObject(wx.NullBitmap)

        return bordered_image
