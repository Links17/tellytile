"""Small deterministic Markdown primitives for authored documents and citations."""
from pathlib import Path
import re
from urllib.parse import unquote

from io_utils import require

REFERENCE = re.compile(r'^ {0,3}\[([^\]\n]+)\]:\s*(\S+)\s*$', re.M)
INLINE = re.compile(r'\]\(([^)]+)\)|href="([^"]+)"')
NAMED = re.compile(r'\[([^\]\n]+)\]\[([^\]\n]*)\]')


def label_key(label):
    return ' '.join(label.split()).casefold()


def prose(text):
    """Code examples do not declare links or participate in document navigation."""
    output = []
    fence = None
    for line in text.splitlines(keepends=True):
        marker = re.match(r'^ {0,3}(`{3,}|~{3,})', line)
        if marker and fence is None:
            fence = marker[1]
        elif marker and fence and marker[1][0] == fence[0] and len(marker[1]) >= len(fence) and line.strip() == marker[1]:
            fence = None
            continue
        elif fence is None:
            output.append(line)
    return re.sub(r'(`+)[^\n]*?\1', '', ''.join(output))


def reference_definitions(text, label):
    definitions = {}
    for match in REFERENCE.finditer(prose(text)):
        key = label_key(match[1])
        require(key not in definitions, f'{label}: duplicate reference label: {key}')
        definitions[key] = match[2].removeprefix('<').removesuffix('>')
    return definitions


def reference_url(cell, definitions, label):
    match = NAMED.fullmatch(cell.strip())
    require(match is not None, f'{label}: named Markdown source reference required: {cell}')
    key = label_key(match[2] or match[1])
    require(key in definitions, f'{label}: undefined reference: {key}')
    return definitions[key]


def document_links(text, label):
    definitions = reference_definitions(text, label)
    text = prose(text)
    links = [next(value for value in match if value) for match in INLINE.findall(text)]
    for match in NAMED.finditer(text):
        key = label_key(match[2] or match[1])
        require(key in definitions, f'{label}: undefined reference: {key}')
        links.append(definitions[key])
    return [*links, *definitions.values()]


def local_reference(entry, url):
    """Resolve a local citation to a repository-relative path, not its anchor."""
    require('://' not in url and not url.startswith(('/', 'mailto:', 'data:')),
            f'{entry}: a repository source is required: {url}')
    # Path normalization remains lexical; safe_path performs boundary validation.
    import posixpath
    return posixpath.normpath((Path(entry).parent / unquote(url.split('#')[0])).as_posix())


def sections(text, label):
    """Return top-level section spans; headings inside fenced examples are text."""
    headings = []
    offset = 0
    fence = None
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        marker = re.match(r'^(`{3,}|~{3,})', stripped)
        if marker:
            token = marker[1]
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence) and stripped == token:
                fence = None
        elif fence is None and line.startswith('## '):
            name = line[3:].strip()
            require(name not in {row[0] for row in headings}, f'{label}: duplicate section: {name}')
            headings.append((name, offset, offset+len(line)))
        offset += len(line)
    require(fence is None, f'{label}: unterminated Markdown fence')
    return {name: (start, body, headings[index+1][1] if index+1 < len(headings) else len(text))
            for index, (name, start, body) in enumerate(headings)}
