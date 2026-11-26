"""Resolve declared source dependencies once for export, context and evidence."""
from pathlib import Path

from io_utils import IGNORED, digest, object_digest, read_json, require, safe_path, source_files


class SourceGraph:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.files = set()
        self.edges = {}
        self.active = []
        self.done = set()

    def add(self, reference):
        require(isinstance(reference, str), 'dependency must be a repository-relative path')
        require(not Path(reference).parts or Path(reference).parts[0] != 'projects', f'private project dependency: {reference}')
        path = safe_path(self.root, reference)
        require(not path.resolve().relative_to(self.root).parts or path.resolve().relative_to(self.root).parts[0] != 'projects', f'private project dependency: {reference}')
        reference = path.relative_to(self.root).as_posix()
        require(not any(part in IGNORED for part in Path(reference).parts), f'generated/cache dependency: {reference}')
        require(path.name != '.env' and not (path.name.startswith('.env.') and path.name != '.env.example'), f'private environment dependency: {reference}')
        require(reference not in self.active, 'dependency cycle: ' + ' -> '.join([*self.active, reference]))
        if reference in self.done:
            return
        self.active.append(reference)
        children = []
        if path.is_dir():
            manifest = path / 'resource.json'
            if manifest.is_file():
                children = [manifest.relative_to(self.root).as_posix()]
            else:
                children = [p.relative_to(self.root).as_posix() for p in source_files(path)]
                require(children, f'empty dependency directory: {reference}')
        else:
            self.files.add(reference)
            if path.name == 'resource.json':
                resource = read_json(path)
                require(resource.get('schema_version') == 1 and resource.get('kind') == 'resource', f'{reference}: invalid resource schema')
                require(isinstance(resource.get('inputs'), list) and isinstance(resource.get('dependencies'), list), f'{reference}: inputs/dependencies must be lists')
                children = resource['inputs'] + resource['dependencies']
                require(children, f'{reference}: empty resource')
        self.edges[reference] = sorted(set(children))
        for child in children:
            self.add(child)
        self.active.pop()
        self.done.add(reference)

    def resolve(self, roots):
        for root in sorted(set(roots)):
            self.add(root)
        return sorted(self.files)

    def snapshot(self):
        return {'schema_version': 3,
                'files': {name: digest(safe_path(self.root, name)) for name in sorted(self.files)},
                'edges': {name: self.edges[name] for name in sorted(self.edges)}}

    def fingerprint(self):
        return object_digest(self.snapshot())
