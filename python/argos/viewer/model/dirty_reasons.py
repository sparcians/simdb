import enum

class DirtyReasons(enum.Enum):
    WidgetDropped = 1
    WidgetSplit = 2
    CanvasExploded = 3
    TabAdded = 4
    TabRenamed = 5
    TabDeleted = 6
    QueueUtilizDispQueueChanged = 7
    TimeseriesPlotSettingsChanged = 8
    QueueTableDispColsChanged = 9
    QueueTableAutoColorizeChanged = 10
    SchedulingLinesWidgetChanged = 11
    SashPositionChanged = 12
    WidgetRemoved = 13
    TrackedPacketChanged = 14
    SummaryViewsWidgetChanged = 15

DIRTY_REASONS = {
    DirtyReasons.WidgetDropped: 'A widget was dropped onto the widget canvas',
    DirtyReasons.WidgetSplit: 'A widget was split horizontally or vertically',
    DirtyReasons.CanvasExploded: 'Widget canvas was exploded',
    DirtyReasons.TabAdded: 'A new tab was added',
    DirtyReasons.TabRenamed: 'A tab was renamed',
    DirtyReasons.TabDeleted: 'A tab was deleted',
    DirtyReasons.QueueUtilizDispQueueChanged: 'The displayed queues in a Queue Utilization widget were changed',
    DirtyReasons.TimeseriesPlotSettingsChanged: 'Settings were changed for a timeseries plot',
    DirtyReasons.QueueTableDispColsChanged: 'Displayed columns were changed for a Queue Table widget',
    DirtyReasons.QueueTableAutoColorizeChanged: 'Auto-colorize column was changed for a Queue Table widget',
    DirtyReasons.SchedulingLinesWidgetChanged: 'Displayed queues were changed for a Scheduling Lines widget',
    DirtyReasons.SashPositionChanged: 'Widget canvas splitter window sash position changed',
    DirtyReasons.WidgetRemoved: 'A widget was removed from the inspector canvas',
    DirtyReasons.TrackedPacketChanged: 'Changes were made to tracked packet(s)',
    DirtyReasons.SummaryViewsWidgetChanged: 'Displayed collectables were changed for a Summary Views widget'
}
