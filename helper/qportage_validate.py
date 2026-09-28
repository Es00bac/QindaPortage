# SPDX-License-Identifier: GPL-3.0-or-later
"""Plan validation for qindaportage-apply.

AGENT-CONTRACT: these rules mirror src/ops/plan.cpp (namespace Validate and
PlanStep::validate). The helper runs as root and trusts nothing the GUI or a
model produced; it must never accept what the C++ side rejects. Keep both in
step and extend tests/cpp/tst_plan.cpp and helper/tests/test_apply.py together.
"""

import re

CAT_RE = re.compile(r'^[A-Za-z0-9_][A-Za-z0-9+_.-]*$')
PN_RE = re.compile(r'^[A-Za-z0-9_][A-Za-z0-9+_-]*$')
VERSION_RE = re.compile(r'^\d+(?:\.\d+)*[a-z]?(?:_(?:alpha|beta|pre|rc|p)\d*)*(?:-r\d+)?$')
USEDEP_RE = re.compile(r'^(!|-)?[A-Za-z0-9][A-Za-z0-9+_@-]*(?:\([+-]\))?[=?]?$')
FLAG_RE = re.compile(r'^[+-]?[A-Za-z0-9][A-Za-z0-9+_@-]*$')
KEYWORD_RE = re.compile(r'^(~?[a-z0-9][a-z0-9-]*|\*\*|~\*|\*)$')
LICENSE_RE = re.compile(r'^(-?@?[A-Za-z0-9][A-Za-z0-9+_.-]*|\*|-\*)$')
REPO_RE = re.compile(r'^[A-Za-z0-9_][A-Za-z0-9_-]*$')
SET_RE = re.compile(r'^@[A-Za-z0-9_][A-Za-z0-9_.-]*$')
PATH_RE = re.compile(r'^[A-Za-z0-9_+][A-Za-z0-9._+-]*(/[A-Za-z0-9_+][A-Za-z0-9._+-]*)*$')
TXN_RE = re.compile(r'^[0-9]{8}-[0-9]{6}-[0-9a-f]{4}$')

CONFIG_KINDS = {
    'use': 'package.use',
    'keyword': 'package.accept_keywords',
    'license': 'package.license',
    'unmask': 'package.unmask',
    'mask': 'package.mask',
}
STEP_KINDS = ('use', 'keyword', 'license', 'unmask', 'mask', 'patch', 'ebuild',
              'emerge', 'unmerge', 'depclean', 'sync')
ATOM_KINDS = ('use', 'keyword', 'license', 'unmask', 'mask', 'patch')

# AGENT-GUARD: no --jobs/--load-average (operator parallelism applies), no
# --ask (not interactive), no --nodeps.
EMERGE_FLAGS = {
    '--oneshot', '--update', '--deep', '--newuse', '--changed-use', '--noreplace',
    '--usepkg', '--usepkgonly', '--getbinpkg', '--getbinpkgonly', '--buildpkg',
    '--keep-going', '--verbose', '--changed-deps', '--changed-slot',
    '--complete-graph', '--emptytree', '--fetchonly',
}
EMERGE_YES_NO = {
    '--with-bdeps', '--select', '--keep-going', '--rebuild-if-new-slot',
    '--binpkg-respect-use', '--changed-deps', '--usepkg', '--getbinpkg', '--buildpkg',
}
EMERGE_ATOM_VALUED = {'--exclude', '--usepkg-exclude', '--useoldpkg-atoms'}
MAX_CONTENT = 1024 * 1024


class ValidationError(ValueError):
    pass


def split_name_version(pf):
    """'gtk+-3.24.5-r1' -> ('gtk+', '3.24.5-r1'); None when no version."""
    for i in range(len(pf) - 1, 0, -1):
        if pf[i - 1] == '-' and pf[i].isdigit() and VERSION_RE.match(pf[i:]):
            return pf[:i - 1], pf[i:]
    return None


def parse_atom(text):
    """Returns a dict (op, cat, pn, version, slot, repo, use) or raises."""
    if not isinstance(text, str) or not text or any(c.isspace() for c in text):
        raise ValidationError('bad atom %r' % (text,))
    s = text
    if s.startswith('!'):
        raise ValidationError('blockers are not allowed here')
    use = None
    if s.endswith(']'):
        open_at = s.rfind('[')
        if open_at < 0:
            raise ValidationError('unbalanced USE dependency')
        use = s[open_at + 1:-1]
        if not all(USEDEP_RE.match(u) for u in use.split(',')):
            raise ValidationError('bad USE dependency')
        s = s[:open_at]
    repo = None
    if '::' in s:
        s, repo = s.split('::', 1)
        if not REPO_RE.match(repo):
            raise ValidationError('bad repository')
    slot = None
    if ':' in s:
        s, slot = s.split(':', 1)
        core = slot[:-1] if slot.endswith(('=', '*')) else slot
        if core and not all(CAT_RE.match(p) for p in core.split('/', 1)):
            raise ValidationError('bad slot')
    op = ''
    for candidate in ('<=', '>=', '<', '>', '=', '~'):
        if s.startswith(candidate):
            op, s = candidate, s[len(candidate):]
            break
    if op == '=' and s.endswith('*'):
        op, s = '=*', s[:-1]
    if '/' not in s:
        raise ValidationError('missing category')
    cat, rest = s.split('/', 1)
    if not CAT_RE.match(cat):
        raise ValidationError('bad category')
    version = None
    pn = rest
    if op:
        split = split_name_version(rest)
        if not split:
            raise ValidationError('operator without version')
        pn, version = split
        if op == '~' and re.search(r'-r\d+$', version):
            raise ValidationError('~ with a revision')
    if not PN_RE.match(pn):
        raise ValidationError('bad package name')
    return {'op': op, 'cat': cat, 'pn': pn, 'version': version, 'slot': slot,
            'repo': repo, 'use': use}


def is_atom(text):
    try:
        parse_atom(text)
        return True
    except ValidationError:
        return False


def allowed_emerge_option(option):
    if option in EMERGE_FLAGS:
        return True
    if '=' not in option:
        return False
    name, value = option.split('=', 1)
    if name in EMERGE_YES_NO:
        return value in ('y', 'n')
    if name == '--backtrack':
        return value.isdigit() and int(value) <= 1000
    if name in EMERGE_ATOM_VALUED:
        return is_atom(value)
    return False


def safe_relative_path(path):
    if not isinstance(path, str) or not PATH_RE.match(path):
        return False
    return all(part not in ('.', '..') for part in path.split('/'))


def sanitize_rationale(text):
    s = ' '.join(str(text or '').split())
    return s if len(s) <= 240 else s[:237] + '...'


def _strings(value, what):
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
        raise ValidationError('%s must be a list of strings' % what)
    return value


def validate_step(step):
    """Validates one step dict in place-free fashion; returns it normalised."""
    if not isinstance(step, dict):
        raise ValidationError('step is not an object')
    kind = step.get('kind')
    if kind not in STEP_KINDS:
        raise ValidationError('unknown kind %r' % (kind,))
    atom = step.get('atom', '')
    values = _strings(step.get('values'), 'values')
    options = _strings(step.get('options'), 'options')
    path = step.get('path', '')
    content = step.get('content', '')
    if not isinstance(atom, str) or not isinstance(path, str) or not isinstance(content, str):
        raise ValidationError('atom, path and content must be strings')
    if len(content) > MAX_CONTENT:
        raise ValidationError('content larger than 1 MiB')

    parsed = None
    if kind in ATOM_KINDS:
        parsed = parse_atom(atom)
        if kind in CONFIG_KINDS and parsed['use'] is not None:
            raise ValidationError('package.* entries cannot carry [USE] dependencies')

    if kind == 'use':
        if not values or not all(FLAG_RE.match(v) for v in values):
            raise ValidationError('bad or missing USE flags')
    elif kind == 'keyword':
        if not values or not all(KEYWORD_RE.match(v) for v in values):
            raise ValidationError('bad or missing keywords')
    elif kind == 'license':
        if not values or not all(LICENSE_RE.match(v) for v in values):
            raise ValidationError('bad or missing licenses')
    elif kind in ('unmask', 'mask'):
        if values:
            raise ValidationError('mask entries take no values')
    elif kind == 'patch':
        if parsed['op'] not in ('', '=') or parsed['slot'] or parsed['repo'] or parsed['use']:
            raise ValidationError('patch atom must be cat/pn or =cat/pn-ver')
        if not safe_relative_path(path) or '/' in path:
            raise ValidationError('bad patch file name')
        if not path.endswith(('.patch', '.diff')):
            raise ValidationError('patch file must end in .patch or .diff')
        if not content:
            raise ValidationError('empty patch')
    elif kind == 'ebuild':
        validate_overlay_path(path)
        if not content:
            raise ValidationError('empty file')
    elif kind in ('emerge', 'unmerge', 'depclean'):
        if kind != 'depclean' and not values:
            raise ValidationError('no packages')
        for v in values:
            if kind == 'emerge' and SET_RE.match(v):
                continue
            parse_atom(v)
        for o in options:
            if kind != 'emerge' or not allowed_emerge_option(o):
                raise ValidationError('option %r is not allowed' % o)
    elif kind == 'sync':
        if not all(REPO_RE.match(v) for v in values):
            raise ValidationError('bad repository name')
        if options:
            raise ValidationError('sync takes no options')
    return {'kind': kind, 'atom': atom, 'values': values, 'options': options,
            'path': path, 'content': content,
            'rationale': sanitize_rationale(step.get('rationale', ''))}


def validate_overlay_path(path):
    if not safe_relative_path(path):
        raise ValidationError('bad overlay path')
    parts = path.split('/')
    if len(parts) < 3:
        raise ValidationError('overlay path must be cat/pn/<file>')
    cat, pn, name = parts[0], parts[1], parts[2]
    if not CAT_RE.match(cat) or not PN_RE.match(pn):
        raise ValidationError('bad category/package')
    if len(parts) == 3:
        is_ebuild = (name.startswith(pn + '-') and name.endswith('.ebuild')
                     and is_atom('=%s/%s' % (cat, name[:-7])))
        if not is_ebuild and name != 'metadata.xml':
            raise ValidationError('%r is neither pn-ver.ebuild nor metadata.xml' % name)
    elif name != 'files':
        raise ValidationError('only files/ may hold extra overlay files')


def validate_plan(plan):
    if not isinstance(plan, dict):
        raise ValidationError('plan is not an object')
    steps = plan.get('steps')
    if not isinstance(steps, list) or not steps:
        raise ValidationError('the plan has no steps')
    out = []
    for i, step in enumerate(steps, 1):
        try:
            out.append(validate_step(step))
        except ValidationError as e:
            raise ValidationError('step %d: %s' % (i, e))
    return out


def config_line(step):
    tokens = [step['atom']]
    for v in step['values']:
        tokens.append(v[1:] if step['kind'] == 'use' and v.startswith('+') else v)
    return ' '.join(tokens)
