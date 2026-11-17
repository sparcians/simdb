import wx, re
from collections import OrderedDict

class Search(wx.Dialog):
    def __init__(self,
                 frame,
                 search_params: str = '',
                 elem_path: str = ''):

        wx.Dialog.__init__(self, None, title='Search')
        self.frame = frame

        # Most recent searches first. ENTER is handled like pressing 'Find'.
        self.data_constraints = wx.ComboBox(
            self, value=search_params, choices=list(frame.search_history),
            size=(400, -1), style=wx.CB_DROPDOWN | wx.TE_PROCESS_ENTER)
        self.data_constraints.Bind(wx.EVT_TEXT, self.__ValidateFind)
        self.data_constraints.Bind(wx.EVT_COMBOBOX, self.__ValidateFind)
        self.data_constraints.Bind(wx.EVT_TEXT_ENTER, self.__OnEnter)

        if elem_path:
            self.path_constraints = wx.RadioBox(
                self,
                label="Restrict search to:",
                choices=[elem_path, "All"],
                majorDimension=1,
                style=wx.RA_SPECIFY_COLS,
                size=(400, -1)
            )
        else:
            self.path_constraints = None

        sizer = wx.BoxSizer(wx.VERTICAL)
        sizer.Add(self.data_constraints, 0, wx.TOP | wx.LEFT, 5)
        if self.path_constraints:
            sizer.Add(self.path_constraints, 0, wx.TOP | wx.LEFT, 5)

        button_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.find_button = wx.Button(self, label="Find")
        close_button = wx.Button(self, label="Close")

        button_sizer.Add(self.find_button, 0, wx.RIGHT, 5)
        button_sizer.Add(close_button, 0)

        self.find_button.Bind(wx.EVT_BUTTON, self.__DoFind)
        close_button.Bind(wx.EVT_BUTTON, self.__DoClose)

        sizer.Add(
            button_sizer,
            0,
            wx.ALIGN_RIGHT | wx.ALL,
            10
        )

        self.SetSizer(sizer)
        self.Fit()
        self.Centre()

        self.__ValidateFind(None)

    def GetDataConstraints(self):
        return self.NormalizeDataConstraints(self.data_constraints.GetValue())

    def GetDataConstraintsText(self):
        # What the user typed (or picked from the history), with whitespace tidied.
        return ' '.join(self.data_constraints.GetValue().split())

    def GetPathConstraints(self):
        if self.path_constraints:
            sel = self.path_constraints.GetStringSelection()
            if sel != "All":
                return sel

        return None

    @staticmethod
    def NormalizeDataConstraints(constraints):
        if isinstance(constraints, dict):
            return constraints

        # Normalize whitespace and delimiters
        constraints = constraints.replace(',', ' ')
        constraints = ' '.join(constraints.split())

        pattern = re.compile(
            r'(\w+)\s*(?:\(\s*(0x[0-9a-fA-F]+|\d+)\s*\)|:\s*(0x[0-9a-fA-F]+|\d+))'
        )

        matches = list(pattern.finditer(constraints))

        # Ensure the entire string was consumed
        if not matches or ''.join(
            m.group(0) for m in matches
        ).replace(' ', '') != constraints.replace(' ', ''):
            return None

        # Preserve the order in which the fields were specified
        return OrderedDict(
            (m.group(1), m.group(2) or m.group(3))
            for m in matches
        )

    @staticmethod
    def ResolveCycleRange(start, end, first_cycle, last_cycle, curr_cycle, unit='cycle'):
        """
        Resolve the start/end text into absolute (start_cycle, end_cycle).
        Raises ValueError with a user-facing message if the range is invalid.

        Start: '' | 'start' | 'end' | '-N' (N cycles before current) | N (absolute)
        End:   '' | 'start' | 'end' | '+N' (N cycles after current)  | N (absolute)

        The numbers can be in any unit (e.g. 'tick') as long as they all use the same
        one. 'unit' is only used to word the error messages.
        """
        start = ''.join(start.split()).lower()
        end = ''.join(end.split()).lower()

        def absolute(text, name):
            cycle = int(text)
            if not first_cycle <= cycle <= last_cycle:
                raise ValueError(
                    f'{name} {unit} {cycle} is outside of [{first_cycle}, {last_cycle}]'
                )
            return cycle

        if start in ('', 'start'):
            start_cycle = first_cycle
        elif start == 'end':
            start_cycle = last_cycle
        elif re.fullmatch(r'-\d+', start):
            start_cycle = max(curr_cycle + int(start), first_cycle)
        elif start.isdigit():
            start_cycle = absolute(start, 'Start')
        else:
            raise ValueError(
                f"Start must be empty, 'start', 'end', '-N' (N previous {unit}s), "
                f"or an absolute {unit}"
            )

        if end in ('', 'end'):
            end_cycle = last_cycle
        elif end == 'start':
            end_cycle = first_cycle
        elif re.fullmatch(r'\+\d+', end):
            end_cycle = min(curr_cycle + int(end), last_cycle)
        elif end.isdigit():
            end_cycle = absolute(end, 'End')
        else:
            raise ValueError(
                f"End must be empty, 'start', 'end', '+N' (N future {unit}s), "
                f"or an absolute {unit}"
            )

        if start_cycle > end_cycle:
            raise ValueError(
                f'{unit.capitalize()} range is out of order ({start_cycle} > {end_cycle})'
            )

        return start_cycle, end_cycle

    def __ValidateFind(self, evt):
        error = None
        if not self.NormalizeDataConstraints(self.data_constraints.GetValue()):
            error = 'uid:1, opcode:0xdeadbeef\nuid(1) opcode(0xdeadbeef)\netc...'

        if isinstance(error, str):
            self.find_button.Disable()
            self.find_button.SetToolTip(error)
        else:
            self.find_button.Enable()
            self.find_button.UnsetToolTip()

    def __OnEnter(self, evt):
        if self.find_button.IsEnabled():
            self.__DoFind(evt)
        else:
            wx.Bell()

    def __DoFind(self, evt):
        self.EndModal(wx.ID_OK)

    def __DoClose(self, evt):
        self.EndModal(wx.ID_CLOSE)
