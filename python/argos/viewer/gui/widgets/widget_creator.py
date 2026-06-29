from viewer.gui.widgets.queue_utiliz import QueueUtilizWidget
from viewer.gui.widgets.scheduling_lines import SchedulingLinesWidget
from viewer.gui.widgets.summary_views import SummaryViews
from viewer.gui.widgets.iterable_struct import IterableStruct

class WidgetCreator:
    def __init__(self, frame):
        self.frame = frame

    def CreateWidget(self, widget_creation_key, widget_container):
        if widget_creation_key == 'Queue Utilization':
            return QueueUtilizWidget(widget_container, self.frame)
        elif widget_creation_key == 'Scheduling Lines':
            return SchedulingLinesWidget(widget_container, self.frame)
        elif widget_creation_key == 'Summary Views':
            return SummaryViews(widget_container, self.frame)
        elif widget_creation_key.find('$') != -1:
            widget_name, elem_path = widget_creation_key.split('$')
            if widget_name == 'IterableStruct':
                return IterableStruct(widget_container, self.frame, elem_path)
        elif widget_creation_key == 'NO_WIDGET':
            return None

        raise ValueError(f"Unknown widget creation key: {widget_creation_key}")
