"""Application briefs and workspace boundaries; hardware facts stay in the catalog."""
from html import escape
from pathlib import Path

from io_utils import require


def destination(root, output):
    root = Path(root).resolve()
    requested = Path(output).expanduser().absolute()
    resolved = requested.resolve()
    if requested.is_relative_to(root):
        relative = requested.relative_to(root)
        require(len(relative.parts) >= 2 and relative.parts[0] == 'projects',
                'In-repository applications must be under projects/<name>.')
        require('..' not in relative.parts, 'Project output must not contain parent traversal.')
        current = root
        for part in relative.parts:
            current /= part
            require(not current.is_symlink(), 'Project workspace paths must not traverse symlinks.')
    elif resolved.is_relative_to(root):
        require(False, 'Project output must not enter the repository through a symlink.')
    require(not resolved.exists(), 'Output must be a new directory.')
    return resolved


def brief(value, name):
    value = {} if value is None else value
    require(isinstance(value, dict), 'Project brief must be an object.')
    require(set(value) <= {'name', 'purpose', 'target_users', 'scenarios', 'bom'},
            'Unknown project brief field; keep credentials outside the brief.')
    data = {'name': name, 'purpose': None, 'target_users': [], 'scenarios': [], 'bom': [], **value}
    require(isinstance(data['name'], str) and data['name'].strip(), 'Project name must be nonempty text.')
    require(data['purpose'] is None or isinstance(data['purpose'], str) and data['purpose'].strip(),
            'Project purpose must be nonempty text or null when unknown.')
    for key in ('target_users', 'scenarios'):
        require(isinstance(data[key], list) and all(isinstance(item, str) and item.strip() for item in data[key]),
                f'Project {key} must be a list of nonempty strings.')
    require(isinstance(data['bom'], list), 'Project BOM must be a list.')
    for item in data['bom']:
        require(isinstance(item, dict) and set(item) == {'item', 'quantity', 'source'},
                'Each BOM entry requires item, quantity and source.')
        require(isinstance(item['item'], str) and item['item'].strip(), 'BOM item must be nonempty text.')
        require(item['quantity'] is None or type(item['quantity']) is int and item['quantity'] > 0,
                'BOM quantity must be a positive integer or null when unknown.')
        require(item['source'] is None or isinstance(item['source'], str) and item['source'].strip(),
                'BOM source must be nonempty text or null when unknown.')
    return data


def application_rules(data, existing=''):
    """Generate navigation from project.yaml; never infer missing application facts."""
    value = brief(data.get('brief'), data['device'])
    def literal(text):
        # Render user prose as plain text, never as injected Markdown/HTML rules.
        text = escape(str(text)).replace('\n', ' ').replace('\r', ' ')
        return ''.join(f'&#{ord(char)};' if char in r'\`[]*_|' else char for char in text)
    def items(values):
        return '\n'.join('- ' + literal(item) for item in values) if values else '- unknown'
    bom = []
    for item in value['bom']:
        quantity = 'unknown' if item['quantity'] is None else item['quantity']
        source = 'unknown' if item['source'] is None else item['source']
        bom.append(f"- {literal(item['item'])}; quantity: {literal(quantity)}; source: {literal(source)}")
    start = '<!-- CODECRAFT_APPLICATION_START -->'
    end = '<!-- CODECRAFT_APPLICATION_END -->'
    generated = (
        start + '\n'
        'Generated from project.yaml. Edit its brief and run `python3 codecraft.py lock-project .` to refresh this view.\n\n'
        '## Project\n\n'
        f"Name: {literal(value['name'])}\n\nPurpose: {literal(value['purpose'] or 'unknown')}\n\n"
        f"Device: {literal(data['device'])}; Target: {literal(data['target'])}; example: {literal(data['example'])}.\n\n"
        '## Target users\n\n' + items(value['target_users']) + '\n\n'
        '## Scenarios\n\n' + items(value['scenarios']) + '\n\n'
        '## Bill of materials\n\n' + ('\n'.join(bom) if bom else '- unknown') + '\n\n'
        'The brief records user intent, not verified hardware compatibility. Missing quantities, sources, revisions and requirements remain unknown.\n'
        + end
    )
    if start in existing or end in existing:
        require(existing.count(start) == existing.count(end) == 1 and existing.index(start) < existing.index(end),
                'Invalid generated application context markers; preserve custom rules and repair the markers before relocking.')
        before, rest = existing.split(start)
        _, after = rest.split(end)
        return before + generated + after
    if existing.strip():
        return existing.rstrip() + '\n\n' + generated + '\n'
    return (
        '# Application context and rules\n\n' + generated + '\n\n## Development rules\n\n'
        'Read README.md, project.yaml and [the portable workflow](docs/development/application-workflow.md). Preserve source citations and native APIs. Keep application changes in this project. '
        'Verify/update the lock before building. Device writes require an identified device and explicit authorization; builds and packages never perform them. '
        'Keep secrets out of source and locks. Maintain README.md and README.zh_CN.md together; all other engineering documents use English only.\n'
    )
