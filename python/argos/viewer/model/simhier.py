import copy, re
import sqlite3

class SimHierarchy:
    def __init__(self, db, dtype_inspector, show_in_ui_only=True):
        full_paths = []
        metas_by_path = {}

        cursor = db.cursor()

        # ShowInUI=1
        #    --> This means that we only show collectables that were actually collected.
        #        The database has all the metadata to add everything to the hierarchy,
        #        but without any collected data it just looks cluttered.
        cmd = "SELECT FullPath,CID,ClockID,TypeName FROM CollectableTreeNodes"
        if show_in_ui_only:
            cmd += " WHERE ShowInUI=1"
        cursor.execute(cmd)
        rows = [(r[0], r[1], r[2], r[3]) for r in cursor.fetchall()]

        for full_path, cid, clk_id, type_name in rows:
            full_paths.append(full_path)
            metas_by_path[full_path] = {
                'CID': cid,
                'ClkID': clk_id,
                'TypeName': type_name,
                'ArgosDefaultHiddenColumns': ''
            }

        self._simhier_tree = SimHierTree()
        self._simhier_tree.BuildFromList(full_paths)
        for full_path, meta in metas_by_path.items():
            for meta_name, meta_value in meta.items():
                self._simhier_tree.SetMetaAtPath(full_path, meta_name, meta_value)

        def HandleLeaf(leaf):
            cid = leaf.GetMeta('CID')
            dtype = leaf.GetMeta('TypeName')
            full_path = leaf.GetPath()
            self._cids_by_elem_path[full_path] = cid
            if '_contig_capacity' in dtype or '_sparse_capacity' in dtype:
                self._widget_types_by_cid[cid] = 'QueueTable'
                self._container_elem_paths.append(full_path)
                idx = dtype.find('_capacity')
                capacity = int(dtype[idx+len('_capacity'):])
                self._capacities_by_cid[cid] = capacity
                if '_contig_capacity' in dtype:
                    self._contig_cids.add(cid)
                else:
                    self._sparse_cids.add(cid)
            elif dtype_inspector.GetStructDefn(dtype) is not None:
                self._scalar_structs_elem_paths.append(leaf.GetPath())
            else:
                self._scalar_stats_elem_paths.append(leaf.GetPath())

        self._widget_types_by_cid = {}
        self._scalar_stats_elem_paths = []
        self._scalar_structs_elem_paths = []
        self._container_elem_paths = []
        self._capacities_by_cid = {}
        self._sparse_cids = set()
        self._contig_cids = set()
        self._cids_by_elem_path = {}
        self._simhier_tree.VisitLeaves(HandleLeaf)

        def HandleNode(node):
            this_id = node.GetID()
            child_ids = [child.GetID() for child in node.children.values()]
            self._child_ids_by_parent_id[this_id] = child_ids

            if node.GetParent():
                parent_id = node.GetParent().GetID()
                self._parent_ids_by_child_id[this_id] = parent_id
            else:
                self._parent_ids_by_child_id[this_id] = 0

            self._elem_names_by_id[this_id] = node.GetName()
            self._elem_paths_by_id[this_id] = node.GetPath()

        self._child_ids_by_parent_id = {}
        self._parent_ids_by_child_id = {}
        self._elem_names_by_id = {}
        self._elem_paths_by_id = {}
        self._simhier_tree.VisitNodes(HandleNode)
        self._elem_ids_by_path = {v: k for k, v in self._elem_paths_by_id.items()}

        # Sanity checks to ensure that no element path contains 'root.'
        for _,elem_path in self._elem_paths_by_id.items():
            assert elem_path.find('root.') == -1

        for elem_path,_ in self._elem_ids_by_path.items():
            assert elem_path.find('root.') == -1

        for elem_path in self._scalar_stats_elem_paths:
            assert elem_path.find('root.') == -1

        for elem_path in self._scalar_structs_elem_paths:
            assert elem_path.find('root.') == -1

        for elem_path in self._container_elem_paths:
            assert elem_path.find('root.') == -1

        for elem_path,_ in self._cids_by_elem_path.items():
            assert elem_path.find('root.') == -1

    def GetTree(self):
        return self._simhier_tree

    def GetMetaAtPath(self, elem_path, meta_name):
        return self._simhier_tree.GetMetaAtPath(elem_path, meta_name)

    def GetMetaForCollectionID(self, elem_id, meta_name):
        elem_path = self.GetElemPath(elem_id)
        return self.GetMetaAtPath(elem_path, meta_name)

    def GetParentID(self, elem_id):
        return self._parent_ids_by_child_id[elem_id]

    def GetChildIDs(self, elem_id):
        return self._child_ids_by_parent_id.get(elem_id, [])
    
    def GetElemPath(self, elem_id):
        return self._elem_paths_by_id[elem_id]
    
    def GetElemID(self, elem_path):
        return self._elem_ids_by_path.get(elem_path)
    
    def GetCollectionID(self, elem_path):
        return self._cids_by_elem_path.get(elem_path)

    def GetContainerIDs(self):
        return self._contig_cids | self._sparse_cids

    def GetCapacityByCollectionID(self, collectable_id):
        return self._capacities_by_cid.get(collectable_id)
    
    def GetCapacityByElemPath(self, elem_path):
        collectable_id = self.GetCollectionID(elem_path)
        return self.GetCapacityByCollectionID(collectable_id)
    
    def GetSparseFlagByCollectionID(self, collectable_id):
        return collectable_id in self._sparse_cids

    def GetSparseFlagByElemPath(self, elem_path):
        collectable_id = self.GetCollectionID(elem_path)
        return self.GetSparseFlagByCollectionID(collectable_id)

    def GetName(self, elem_id):
        return self._elem_names_by_id[elem_id]
    
    def GetElemPaths(self, leaves_only=False):
        if not leaves_only:
            return self._elem_paths_by_id.values()
        else:
            paths = []
            def HandleLeaf(leaf):
                paths.append(leaf.GetPath())

            self.GetTree().VisitLeaves(HandleLeaf)
            return paths

    def GetScalarStatsElemPaths(self):
        return copy.deepcopy(self._scalar_stats_elem_paths)
    
    def GetScalarStructsElemPaths(self):
        return copy.deepcopy(self._scalar_structs_elem_paths)
    
    def GetContainerElemPaths(self):
        return copy.deepcopy(self._container_elem_paths)

    def GetItemElemPaths(self):
        elem_paths = self.GetScalarStatsElemPaths() + self.GetScalarStructsElemPaths() + self.GetContainerElemPaths()
        elem_paths.sort()
        return elem_paths
    
    def GetWidgetType(self, elem_id):
        return self._widget_types_by_cid.get(elem_id, '')

class SimHierTree:
    class Node:
        def __init__(self, name, parent=None):
            self.name = name
            self.parent = parent
            self.children = {}
            self.id = 0  # assigned later (all nodes)
            self._path = None
            self._meta = {}

        def GetID(self):
            return self.id

        def GetName(self):
            return self.name

        def GetParent(self):
            return self.parent

        def GetChildren(self):
            return [child for child in self.children.values()]

        def AddChild(self, name):
            if name not in self.children:
                self.children[name] = SimHierTree.Node(name, parent=self)
            return self.children[name]

        def GetChild(self, name):
            return self.children.get(name)

        def GetPath(self):
            if not self._path:
                parts = []
                node = self
                while node and node.GetName():
                    parts.append(node.GetName())
                    node = node.GetParent()

                parts.reverse()
                self._path = '.'.join(parts)

            return self._path

        def SetMeta(self, name, value):
            self._meta[name] = value

        def GetMeta(self, name):
            return self._meta[name]

        def HasMeta(self, name):
            return name in self._meta

        def __repr__(self):
            return f"Node(name={self.name}, id={self.id})"

    def __init__(self):
        self.root = self.Node("")
        self._nodes_by_path = {}
        self._next_id = 1

    def Insert(self, path_str):
        parts = path_str.split(".")
        curr = self.root
        for part in parts:
            curr = curr.AddChild(part)

    def BuildFromList(self, paths):
        paths.sort()
        for p in paths:
            self.Insert(p)
        self.__AssignDfsIDs()
        self.__CacheNodesByPath()

    def GetRoot(self):
        return self.root

    def GetNodeAtPath(self, path_str):
        parts = path_str.split('.')
        curr = self.root
        for part in parts:
            next = curr.GetChild(part)
            if not next:
                raise Exception(f'SimHierTree path does not exist: "{path_str}"')
            curr = next

        return curr

    def GetMetaAtPath(self, path_str, meta_name):
        node = self.GetNodeAtPath(path_str)
        return node.GetMeta(meta_name)

    def SetMetaAtPath(self, path_str, meta_name, meta_value):
        node = self.GetNodeAtPath(path_str)
        node.SetMeta(meta_name, meta_value)

    def VisitLeaves(self, callback):
        def Recurse(node, callback):
            if node is not self.root and not node.children:
                callback(node)
            else:
                for child in node.children.values():
                    Recurse(child, callback)

        Recurse(self.root, callback)

    def VisitNodes(self, callback):
        def Recurse(node, callback):
            if node is not self.root:
                callback(node)
            for child in node.children.values():
                Recurse(child, callback)

        Recurse(self.root, callback)

    def __AssignDfsIDs(self):
        def Recurse(node):
            if node is not self.root:
                node.id = self._next_id
                self._next_id += 1
            for child in node.children.values():
                Recurse(child)

        Recurse(self.root)

    def __CacheNodesByPath(self):
        def Recurse(node, map):
            map[node.GetPath()] = node
            for child in node.children.values():
                Recurse(child, map)

        Recurse(self.root, self._nodes_by_path)
