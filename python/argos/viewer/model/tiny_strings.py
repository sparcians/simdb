import sqlite3

class TinyStrings:
    def __init__(self, db_file):
        with sqlite3.connect(db_file) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT StringValue,StringID FROM TinyStringIDs')

            self._strings_by_id = {}
            for sval, sid in cursor.fetchall():
                self._strings_by_id[sid] = sval

    def GetString(self, string_id, must_exist=False):
        if string_id in self._strings_by_id:
            return self._strings_by_id[string_id]
        if must_exist:
            raise Exception(f'String ID does not exist: {string_id}')
        return None
