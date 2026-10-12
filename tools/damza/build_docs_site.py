#!/usr/bin/env python3
"""Build docs/plugins/site/index.html: the whole plugin documentation as one
self-contained, offline page (sidebar, search, copy buttons, dark/light).

  tools/damza/build_docs_site.py                 write docs/plugins/site/index.html
  tools/damza/build_docs_site.py --fragment=F    also write F without the
                                                 <!doctype>/<html>/<head>/<body>
                                                 skeleton (for hosts that add their own)

Markdown is converted here (no runtime library): GitHub-style heading slugs,
links between .md files become in-page links, code is highlighted at build
time. Re-run after editing anything in docs/plugins/.
"""
import html, importlib.util, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, 'docs', 'plugins')
OUT_DIR = os.path.join(DOCS, 'site')
OUT = os.path.join(OUT_DIR, 'index.html')

# (markdown path relative to docs/plugins, page id, nav label, group)
PAGES = [
    ('README.md', 'home', 'Обзор', 'Начало'),
    ('guide/01-first-plugin.md', 'guide-01', 'Первый плагин', 'Гайды'),
    ('guide/02-events.md', 'guide-02', 'События', 'Гайды'),
    ('guide/03-messages.md', 'guide-03', 'Сообщения', 'Гайды'),
    ('guide/04-chats-and-users.md', 'guide-04', 'Чаты и пользователи', 'Гайды'),
    ('guide/05-ui.md', 'guide-05', 'Интерфейс', 'Гайды'),
    ('guide/06-commands-and-hooks.md', 'guide-06', 'Команды и хуки', 'Гайды'),
    ('guide/07-network-and-storage.md', 'guide-07', 'Сеть и хранение', 'Гайды'),
    ('guide/08-raw-mtproto.md', 'guide-08', 'Сырой MTProto', 'Гайды'),
    ('guide/09-debugging.md', 'guide-09', 'Отладка', 'Гайды'),
    ('guide/10-publishing.md', 'guide-10', 'Публикация', 'Гайды'),
    ('reference.md', 'reference', 'Справочник API', 'Справочник'),
    ('cookbook.md', 'cookbook', 'Рецепты', 'Справочник'),
    ('api/messages.md', 'api-messages', 'dzg.messages', 'Модули API'),
    ('api/chats.md', 'api-chats', 'dzg.chats', 'Модули API'),
    ('api/users.md', 'api-users', 'dzg.users · accounts', 'Модули API'),
    ('api/ui.md', 'api-ui', 'dzg.ui', 'Модули API'),
    ('api/mtproto.md', 'api-mtproto', 'dzg.api (MTProto)', 'Модули API'),
    ('api/plugins.md', 'api-plugins', 'dzg.plugins', 'Модули API'),
    ('api/builtin.md', 'api-builtin', 'dzg.builtin · commands', 'Модули API'),
    ('manager.md', 'manager', 'Менеджер плагинов', 'Клиент'),
    ('builtins.md', 'builtins', 'Встроенные плагины', 'Клиент'),
    ('ARCHITECTURE.md', 'architecture', 'Устройство (C++)', 'Клиент'),
]

TRANSLIT = dict(zip(
    'абвгдеёжзийклмнопрстуфхцчшщъыьэюя',
    ['a', 'b', 'v', 'g', 'd', 'e', 'e', 'zh', 'z', 'i', 'y', 'k', 'l', 'm', 'n', 'o', 'p', 'r', 's', 't',
     'u', 'f', 'h', 'ts', 'ch', 'sh', 'sch', '', 'y', '', 'e', 'yu', 'ya']))


def github_slug(text):
    text = text.strip().lower()
    text = re.sub(r'[^\w\- ]', '', text, flags=re.U)
    return text.replace(' ', '-')


def ascii_id(text):
    out = ''.join(TRANSLIT.get(c, c) for c in text)
    out = re.sub(r'[^a-z0-9_-]+', '-', out)
    out = re.sub(r'-{2,}', '-', out).strip('-')
    return out or 'section'


def strip_tags(value):
    return html.unescape(re.sub(r'<[^>]+>', '', value))


# --------------------------------------------------------------------------
# Syntax highlighting (build time)

JS_KEYWORDS = set('''const let var function return if else for while do of in new await async class
extends import export from default try catch finally throw typeof instanceof break continue switch
case yield delete void this super static get set interface type readonly keyof declare'''.split())
JS_LITERALS = {'null', 'undefined', 'true', 'false', 'NaN', 'Infinity'}
REGEX_PREFIX = set('(,=:[!&|?{};+-*%<>~^') | {''}


def span(cls, text):
    return '<span class="tk-%s">%s</span>' % (cls, html.escape(text, quote=False))


def highlight_js(code):
    out = []
    i, n = 0, len(code)
    prev = ''   # last significant char or word, to tell regex from division
    while i < n:
        c = code[i]
        if code.startswith('//', i):
            end = code.find('\n', i)
            end = n if end < 0 else end
            out.append(span('com', code[i:end])); i = end; continue
        if code.startswith('/*', i):
            end = code.find('*/', i + 2)
            end = n if end < 0 else end + 2
            out.append(span('com', code[i:end])); i = end; continue
        if c in '\'"':
            m = re.compile(r'%s(?:\\.|[^%s\\\n])*%s' % (c, c, c)).match(code, i)
            if m:
                out.append(span('str', m.group(0))); i = m.end(); prev = 'x'; continue
        if c == '`':
            j = i + 1
            while j < n and code[j] != '`':
                j += 2 if code[j] == '\\' else 1
            out.append(span('str', code[i:j + 1])); i = j + 1; prev = 'x'; continue
        if c == '/' and (prev in REGEX_PREFIX or prev in ('return', 'typeof', 'of', 'in')):
            m = re.compile(r'/(?![*/])(?:\\.|\[(?:\\.|[^\]\\\n])*\]|[^/\\\n\[])+/[dgimsuvy]*').match(code, i)
            if m:
                out.append(span('re', m.group(0))); i = m.end(); prev = 'x'; continue
        if c.isdigit():
            m = re.compile(r'0[xX][0-9a-fA-F_]+n?|\d[\d_]*(?:\.\d+)?(?:[eE][+-]?\d+)?n?').match(code, i)
            out.append(span('num', m.group(0))); i = m.end(); prev = 'x'; continue
        if c.isalpha() or c in '_$':
            m = re.compile(r'[\w$]+').match(code, i)
            word = m.group(0)
            if word == 'dzg':
                chain = re.compile(r'dzg(?:\.[A-Za-z_$][\w$]*)*').match(code, i)
                out.append(span('api', chain.group(0))); i = chain.end(); prev = 'x'; continue
            after_dot = i > 0 and code[i - 1] == '.'
            if word in JS_KEYWORDS and not after_dot:
                out.append(span('kw', word)); prev = word
            elif word in JS_LITERALS and not after_dot:
                out.append(span('lit', word)); prev = 'x'
            elif code.startswith('(', m.end()):
                out.append(span('fn', word)); prev = 'x'
            else:
                out.append(html.escape(word, quote=False)); prev = 'x'
            i = m.end(); continue
        out.append(html.escape(c, quote=False))
        if not c.isspace():
            prev = c
        i += 1
    return ''.join(out)


def highlight_json(code):
    def repl(m):
        if m.group('str'):
            cls = 'key' if m.group('colon') else 'str'
            return span(cls, m.group('str')) + html.escape(m.group('colon') or '')
        if m.group('num'):
            return span('num', m.group('num'))
        if m.group('lit'):
            return span('lit', m.group('lit'))
        return html.escape(m.group(0), quote=False)
    pattern = re.compile(r'(?P<str>"(?:\\.|[^"\\])*")(?P<colon>\s*:)?|(?P<num>-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)|(?P<lit>\btrue\b|\bfalse\b|\bnull\b)|[\s\S]')
    return pattern.sub(repl, code)


def highlight_sh(code):
    out = []
    for line in code.split('\n'):
        m = re.match(r'(\s*)(#.*)$', line)
        if m:
            out.append(html.escape(m.group(1)) + span('com', m.group(2)))
            continue
        parts = re.split(r'("[^"]*"|\'[^\']*\')', line)
        rendered = ''
        for k, part in enumerate(parts):
            rendered += span('str', part) if k % 2 else html.escape(part, quote=False)
        out.append(rendered)
    return '\n'.join(out)


def highlight(code, lang):
    lang = (lang or '').lower()
    if lang in ('js', 'javascript', 'ts', 'typescript', 'mjs', 'cpp', 'c++'):
        return highlight_js(code)
    if lang == 'json':
        return highlight_json(code)
    if lang in ('sh', 'bash', 'shell', 'console'):
        return highlight_sh(code)
    return html.escape(code, quote=False)


# --------------------------------------------------------------------------
# Markdown

class Context:
    def __init__(self, md_path, page_id, resolver):
        self.md_path = md_path        # relative to docs/plugins
        self.page_id = page_id
        self.resolver = resolver
        self.headings = []            # (level, id, text)
        self.slug_counts = {}


PLACEHOLDER = '\x00%d\x00'


def render_inline(text, ctx):
    store = []

    def keep(fragment):
        store.append(fragment)
        return PLACEHOLDER % (len(store) - 1)

    # Code spans.
    def code_spans(s):
        result, i = '', 0
        while i < len(s):
            if s[i] == '`':
                run = re.match(r'`+', s[i:]).group(0)
                end = s.find(run, i + len(run))
                while end >= 0 and (s[end + len(run):end + len(run) + 1] == '`'):
                    end = s.find(run, end + len(run) + 1)
                if end >= 0:
                    content = s[i + len(run):end].replace('\n', ' ')
                    if content.startswith(' ') and content.endswith(' ') and content.strip():
                        content = content[1:-1]
                    result += keep('<code>%s</code>' % html.escape(content, quote=False))
                    i = end + len(run)
                    continue
                result += run
                i += len(run)
                continue
            if s[i] == '\\' and i + 1 < len(s) and s[i + 1] in '\\`*_{}[]()#+-.!|~<>':
                result += keep(html.escape(s[i + 1]))
                i += 2
                continue
            result += s[i]
            i += 1
        return result

    text = code_spans(text)
    text = html.escape(text, quote=False)

    # Images and links.
    def image(m):
        return keep('<img alt="%s" src="%s">' % (html.escape(m.group(1)), html.escape(ctx.resolver.file_link(ctx, m.group(2)))))
    text = re.sub(r'!\[([^\]]*)\]\(([^)\s]+)\)', image, text)

    def link(m):
        label, url = m.group(1), html.unescape(m.group(2))
        href, external = ctx.resolver.link(ctx, url)
        attrs = ' target="_blank" rel="noopener"' if external else ''
        return keep('<a href="%s"%s>%s</a>' % (html.escape(href), attrs, inline_emphasis(label)))
    text = re.sub(r'\[((?:[^\[\]]|\[[^\]]*\])+)\]\(([^)\s]+)(?:\s+&quot;[^&]*&quot;)?\)', link, text)

    text = re.sub(r'(?<![\w/"=])(https?://[^\s<>()\x00]+[^\s<>().,;:!?\x00])',
                  lambda m: keep('<a href="%s" target="_blank" rel="noopener">%s</a>' % (m.group(1), m.group(1))), text)
    text = inline_emphasis(text)
    text = text.replace('  \n', '<br>\n')
    for _ in range(3):
        text = re.sub(r'\x00(\d+)\x00', lambda m: store[int(m.group(1))], text)
    return text


def inline_emphasis(text):
    text = re.sub(r'\*\*(?=\S)(.+?)(?<=\S)\*\*', r'<strong>\1</strong>', text, flags=re.S)
    text = re.sub(r'(?<![\w\\])__(?=\S)(.+?)(?<=\S)__(?!\w)', r'<strong>\1</strong>', text, flags=re.S)
    text = re.sub(r'(?<![\w*\\])\*(?=[^\s*])(.+?)(?<=[^\s*])\*(?![\w*])', r'<em>\1</em>', text, flags=re.S)
    text = re.sub(r'(?<![\w\\])_(?=[^\s_])(.+?)(?<=[^\s_])_(?!\w)', r'<em>\1</em>', text, flags=re.S)
    text = re.sub(r'~~(?=\S)(.+?)(?<=\S)~~', r'<del>\1</del>', text, flags=re.S)
    return text


FENCE = re.compile(r'^(\s*)(`{3,}|~{3,})\s*([\w+-]*)[^`]*$')
HEADING = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')
HR = re.compile(r'^\s{0,3}((\*\s*){3,}|(-\s*){3,}|(_\s*){3,})$')
LIST_ITEM = re.compile(r'^(\s*)([-*+]|\d+[.)])(\s+)(.*)$')
TABLE_SEP = re.compile(r'^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$')


def split_row(line):
    line = line.strip()
    if line.startswith('|'):
        line = line[1:]
    if line.endswith('|') and not line.endswith('\\|'):
        line = line[:-1]
    cells, current, i, in_code = [], '', 0, 0
    while i < len(line):
        c = line[i]
        if c == '\\' and i + 1 < len(line) and line[i + 1] == '|':
            current += '\\|'
            i += 2
            continue
        if c == '`':
            in_code ^= 1
        if c == '|' and not in_code:
            cells.append(current.strip())
            current = ''
        else:
            current += c
        i += 1
    cells.append(current.strip())
    return cells


def is_block_start(line):
    return bool(FENCE.match(line) or HEADING.match(line) or line.lstrip().startswith('>')
                or LIST_ITEM.match(line) or HR.match(line))


def render_blocks(lines, ctx, tight=False):
    out = []
    i, n = 0, len(lines)
    while i < n:
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        m = FENCE.match(line)
        if m:
            indent, fence, lang = len(m.group(1)), m.group(2), m.group(3)
            body = []
            i += 1
            while i < n and not re.match(r'^\s*%s%s*\s*$' % (re.escape(fence[0]), re.escape(fence)), lines[i]):
                l = lines[i]
                body.append(l[indent:] if l[:indent].strip() == '' else l.lstrip())
                i += 1
            i += 1
            code = '\n'.join(body)
            label = {'js': 'JavaScript', 'ts': 'TypeScript', 'json': 'JSON', 'sh': 'Shell', 'cpp': 'C++'}.get(lang.lower(), lang)
            out.append('<div class="code"><div class="code-bar"><span>%s</span><button type="button" class="copy">Копировать</button></div>'
                       '<pre><code class="lang-%s">%s</code></pre></div>' % (html.escape(label or 'Текст'), html.escape(lang or 'text'), highlight(code, lang)))
            continue
        m = HEADING.match(line)
        if m:
            level = len(m.group(1))
            inner = render_inline(m.group(2), ctx)
            text = strip_tags(inner)
            slug = github_slug(text)
            count = ctx.slug_counts.get(slug, 0)
            ctx.slug_counts[slug] = count + 1
            if count:
                slug = '%s-%d' % (slug, count)
            hid = ctx.resolver.heading_id(ctx.page_id, slug)
            ctx.headings.append((level, hid, text))
            if level == 1:
                out.append('<h1 id="%s">%s</h1>' % (hid, inner))
            else:
                out.append('<h%d id="%s">%s<a class="anchor" href="#%s" aria-label="Ссылка на раздел">#</a></h%d>' % (level, hid, inner, hid, level))
            i += 1
            continue
        if HR.match(line):
            out.append('<hr>')
            i += 1
            continue
        if line.lstrip().startswith('>'):
            quote = []
            while i < n and lines[i].strip() and (lines[i].lstrip().startswith('>') or not is_block_start(lines[i])):
                l = lines[i].lstrip()
                quote.append(re.sub(r'^>\s?', '', l) if l.startswith('>') else l)
                i += 1
            out.append('<blockquote>%s</blockquote>' % render_blocks(quote, ctx))
            continue
        if '|' in line and i + 1 < n and TABLE_SEP.match(lines[i + 1]):
            header = split_row(line)
            aligns = []
            for cell in split_row(lines[i + 1]):
                cell = cell.strip()
                aligns.append('center' if cell.startswith(':') and cell.endswith(':') else 'right' if cell.endswith(':') else '')
            i += 2
            rows = []
            while i < n and lines[i].strip() and '|' in lines[i]:
                rows.append(split_row(lines[i]))
                i += 1

            def cell_html(tag, value, k):
                align = aligns[k] if k < len(aligns) and aligns[k] else ''
                style = ' style="text-align:%s"' % align if align else ''
                return '<%s%s>%s</%s>' % (tag, style, render_inline(value, ctx), tag)
            head = ''.join(cell_html('th', c, k) for k, c in enumerate(header))
            body = ''.join('<tr>%s</tr>' % ''.join(cell_html('td', c, k) for k, c in enumerate(row[:len(header)] + [''] * (len(header) - len(row)))) for row in rows)
            out.append('<div class="table"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>' % (head, body))
            continue
        m = LIST_ITEM.match(line)
        if m:
            base = len(m.group(1))
            ordered = m.group(2)[0].isdigit()
            start = int(re.match(r'\d+', m.group(2)).group(0)) if ordered else 1
            items, loose = [], False
            while i < n:
                m = LIST_ITEM.match(lines[i])
                if not m or len(m.group(1)) != base or m.group(2)[0].isdigit() != ordered:
                    break
                content_indent = len(m.group(1)) + len(m.group(2)) + len(m.group(3))
                item = [m.group(4)]
                i += 1
                while i < n:
                    l = lines[i]
                    if not l.strip():
                        k = i + 1
                        while k < n and not lines[k].strip():
                            k += 1
                        if k < n and (len(lines[k]) - len(lines[k].lstrip())) >= content_indent:
                            item.append('')
                            loose = True
                            i += 1
                            continue
                        break
                    ind = len(l) - len(l.lstrip())
                    if ind >= content_indent:
                        item.append(l[content_indent:])
                    elif LIST_ITEM.match(l) and ind <= base:
                        break
                    elif ind > base and LIST_ITEM.match(l):
                        item.append(l[min(ind, content_indent):] if ind >= base + 2 else l.lstrip())
                    elif is_block_start(l):
                        break
                    else:
                        item.append(l.strip())
                    i += 1
                items.append(item)
                k = i
                while k < n and not lines[k].strip():
                    k += 1
                if k > i and k < n:
                    m2 = LIST_ITEM.match(lines[k])
                    if m2 and len(m2.group(1)) == base and m2.group(2)[0].isdigit() == ordered:
                        loose = True
                        i = k
            parts = []
            for item in items:
                check = re.match(r'^\[([ xX])\]\s+', item[0])
                prefix = ''
                if check:
                    item = [item[0][check.end():]] + item[1:]
                    prefix = '<span class="check%s" aria-hidden="true"></span>' % (' done' if check.group(1).strip() else '')
                inner = render_blocks(item, ctx, tight=not loose)
                parts.append('<li%s>%s%s</li>' % (' class="task"' if check else '', prefix, inner))
            tag = 'ol' if ordered else 'ul'
            start_attr = ' start="%d"' % start if ordered and start != 1 else ''
            out.append('<%s%s>%s</%s>' % (tag, start_attr, ''.join(parts), tag))
            continue
        para = [line.strip()]
        i += 1
        while i < n and lines[i].strip() and not is_block_start(lines[i]) and not (
                '|' in lines[i] and i + 1 < n and TABLE_SEP.match(lines[i + 1])):
            para.append(lines[i].strip())
            i += 1
        inner = render_inline('\n'.join(para), ctx)
        out.append(inner if tight else '<p>%s</p>' % inner)
    return '\n'.join(out)


# --------------------------------------------------------------------------
# Links between pages

class Resolver:
    def __init__(self):
        self.page_by_path = {os.path.normpath(p): pid for p, pid, _, _ in PAGES}
        self.ids = {}       # (page, slug) -> html id
        self.used = set()
        self.missing = set()

    def heading_id(self, page, slug):
        key = (page, slug)
        if key not in self.ids:
            base = '%s--%s' % (page, ascii_id(slug))
            hid, k = base, 1
            while hid in self.used:
                k += 1
                hid = '%s-%d' % (base, k)
            self.used.add(hid)
            self.ids[key] = hid
        return self.ids[key]

    def file_link(self, ctx, url):
        if re.match(r'^[a-z]+:', url) or url.startswith('#'):
            return url
        target = os.path.normpath(os.path.join(DOCS, os.path.dirname(ctx.md_path), url.split('#')[0]))
        return os.path.relpath(target, OUT_DIR).replace(os.sep, '/')

    def link(self, ctx, url):
        if re.match(r'^(https?|tg|mailto):', url):
            return url, True
        path, _, anchor = url.partition('#')
        if not path:
            page = ctx.page_id
        else:
            rel = os.path.normpath(os.path.join(os.path.dirname(ctx.md_path), path))
            page = self.page_by_path.get(rel)
            if not page:
                return self.file_link(ctx, url), True
        if not anchor:
            return '#' + page, False
        found = self.ids.get((page, anchor)) or self.ids.get((page, github_slug(anchor)))
        if not found:
            self.missing.add('%s -> %s' % (ctx.md_path, url))
        return '#' + (found or page), False


# --------------------------------------------------------------------------
# Page

LOGO = ('<svg viewBox="0 0 512 512" aria-hidden="true" focusable="false"><rect width="512" height="512" rx="116" fill="#0F1011"/>'
        '<path fill-rule="evenodd" d="M90 142H160A90 108 0 0 1 160 358H90V142ZM140 192H160A40 58 0 0 1 160 308H140V192Z" fill="#C8F135"/>'
        '<path d="M272 142H422V192L334 308H422V358H272V308L360 192H272V142Z" fill="#C8F135"/>'
        '<rect x="90" y="384" width="332" height="12" rx="6" fill="#C8F135" fill-opacity="0.4"/></svg>')

CSS = r"""
/* Layout: top bar; sidebar | article (72ch) | page outline. Dark-first: brand ground #0F1011, lime accent. */
:root {
  --bg: #0F1011; --surface: #15171A; --surface-2: #1C1F22; --line: #2A2E32;
  --fg: #E7EAE2; --muted: #969C92; --accent: #C8F135; --accent-strong: #C8F135; --on-accent: #0F1011;
  --tk-kw: #B9A8FF; --tk-str: #F0C27C; --tk-com: #6E766B; --tk-num: #7FD3E4; --tk-api: #C8F135; --tk-fn: #E7EAE2; --tk-key: #9FD8A8; --tk-re: #F59EB6;
  --shadow: 0 18px 40px rgba(0, 0, 0, 0.45);
  --font-display: "Unbounded", "Golos Text", system-ui, sans-serif;
  --font-body: "Golos Text", system-ui, -apple-system, "Segoe UI", Roboto, "Noto Sans", sans-serif;
  --font-mono: "JetBrains Mono", ui-monospace, "Cascadia Code", "SFMono-Regular", Menlo, Consolas, monospace;
  --top: 60px;
  color-scheme: dark;
}
@media (prefers-color-scheme: light) {
  :root:not([data-theme="dark"]) {
    --bg: #F5F6F0; --surface: #FFFFFF; --surface-2: #ECEEE5; --line: #D9DCD1;
    --fg: #17191B; --muted: #5C6258; --accent: #C8F135; --accent-strong: #4B6B00; --on-accent: #0F1011;
    --tk-kw: #6A4BD6; --tk-str: #9A5400; --tk-com: #858B80; --tk-num: #00708A; --tk-api: #4B6B00; --tk-fn: #17191B; --tk-key: #1E7A3C; --tk-re: #B02A5B;
    --shadow: 0 18px 40px rgba(30, 40, 10, 0.14);
    color-scheme: light;
  }
}
:root[data-theme="light"] {
  --bg: #F5F6F0; --surface: #FFFFFF; --surface-2: #ECEEE5; --line: #D9DCD1;
  --fg: #17191B; --muted: #5C6258; --accent: #C8F135; --accent-strong: #4B6B00; --on-accent: #0F1011;
  --tk-kw: #6A4BD6; --tk-str: #9A5400; --tk-com: #858B80; --tk-num: #00708A; --tk-api: #4B6B00; --tk-fn: #17191B; --tk-key: #1E7A3C; --tk-re: #B02A5B;
  --shadow: 0 18px 40px rgba(30, 40, 10, 0.14);
  color-scheme: light;
}
* { box-sizing: border-box; }
html { scroll-padding-top: calc(var(--top) + 16px); }
body { margin: 0; background: var(--bg); color: var(--fg); font: 16px/1.65 var(--font-body); -webkit-font-smoothing: antialiased; }
a { color: var(--accent-strong); text-decoration: underline; text-decoration-color: color-mix(in srgb, var(--accent-strong) 40%, transparent); text-underline-offset: 3px; }
a:hover { text-decoration-color: currentColor; }
:focus-visible { outline: 2px solid var(--accent-strong); outline-offset: 2px; border-radius: 4px; }

.topbar { position: sticky; top: env(safe-area-inset-top, 0px); z-index: 20; height: var(--top); display: flex; align-items: center; gap: 14px;
  padding-inline: 16px; background: color-mix(in srgb, var(--bg) 88%, transparent); backdrop-filter: blur(10px); border-bottom: 1px solid var(--line); }
.brand { display: flex; align-items: center; gap: 10px; color: var(--fg); text-decoration: none; flex-shrink: 0; }
.brand svg { width: 30px; height: 30px; border-radius: 8px; }
.brand b { font: 600 15px/1 var(--font-display); letter-spacing: 0.01em; }
.brand small { display: block; font: 500 11px/1.2 var(--font-mono); color: var(--muted); margin-top: 3px; }
.search { position: relative; flex: 1; max-width: 520px; margin-left: auto; }
.search input { width: 100%; height: 38px; padding: 0 40px 0 14px; border-radius: 10px; border: 1px solid var(--line); background: var(--surface);
  color: var(--fg); font: 15px var(--font-body); }
.search input::placeholder { color: var(--muted); }
.search kbd { position: absolute; right: 10px; top: 9px; font: 12px/18px var(--font-mono); color: var(--muted); border: 1px solid var(--line); border-radius: 5px; padding: 0 6px; }
.results { position: absolute; top: 46px; left: 0; right: 0; max-height: min(70vh, 560px); overflow: auto; background: var(--surface);
  border: 1px solid var(--line); border-radius: 12px; box-shadow: var(--shadow); padding: 6px; }
.results a { display: block; padding: 9px 12px; border-radius: 8px; color: var(--fg); text-decoration: none; }
.results a:hover, .results a.active { background: var(--surface-2); }
.results .where { display: block; font: 12px var(--font-mono); color: var(--muted); }
.results .snippet { display: block; font-size: 13.5px; color: var(--muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.results mark { background: var(--accent); color: var(--on-accent); border-radius: 3px; padding: 0 2px; }
.results .empty { padding: 12px; color: var(--muted); }
.icon-btn { width: 38px; height: 38px; border-radius: 10px; border: 1px solid var(--line); background: var(--surface); color: var(--fg);
  display: grid; place-items: center; cursor: pointer; flex-shrink: 0; font: 16px/1 var(--font-body); }
.icon-btn:hover { border-color: var(--muted); }
.menu-btn { display: none; }

.layout { display: grid; grid-template-columns: 260px minmax(0, 1fr) 220px; gap: 40px; max-width: 1440px; margin: 0 auto; padding-inline: 16px; }
.sidebar { position: sticky; top: calc(var(--top) + env(safe-area-inset-top, 0px)); align-self: start; height: calc(100vh - var(--top)); overflow: auto; padding-block: 24px 40px; }
.nav-group { margin-bottom: 22px; }
.nav-group h4 { margin: 0 0 6px; padding-left: 12px; font: 600 11px/1.4 var(--font-body); text-transform: uppercase; letter-spacing: 0.09em; color: var(--muted); }
.nav-group a { display: flex; gap: 10px; align-items: baseline; padding: 6px 12px; border-radius: 8px; color: var(--fg); text-decoration: none; font-size: 14.5px; }
.nav-group a:hover { background: var(--surface-2); }
.nav-group a .num { font: 12px var(--font-mono); color: var(--muted); font-variant-numeric: tabular-nums; }
.nav-group a.active { background: var(--surface-2); color: var(--accent-strong); font-weight: 600; }
.nav-group a.active .num { color: var(--accent-strong); }
.nav-group a.dim { opacity: 0.35; }
.nav-foot { margin-top: 28px; padding: 14px 12px 0; border-top: 1px solid var(--line); font-size: 13px; color: var(--muted); }

main { min-width: 0; padding-block: 28px 80px; }
.page { max-width: 760px; }
.page h1 { font: 600 clamp(28px, 4vw, 38px)/1.15 var(--font-display); letter-spacing: -0.01em; margin: 8px 0 18px; text-wrap: balance; }
.page h2 { font-size: 24px; line-height: 1.3; margin: 2.4em 0 0.6em; padding-top: 0.9em; border-top: 1px solid var(--line); text-wrap: balance; }
.page h3 { font-size: 19px; line-height: 1.35; margin: 2em 0 0.5em; text-wrap: balance; }
.page h4 { font-size: 16px; margin: 1.6em 0 0.4em; }
.page h2 code, .page h3 code { font-size: 0.86em; }
.anchor { margin-left: 8px; color: var(--muted); text-decoration: none; opacity: 0; font-weight: 400; }
h2:hover .anchor, h3:hover .anchor, h4:hover .anchor, .anchor:focus { opacity: 1; }
.page p, .page li { max-width: 72ch; }
.page ul, .page ol { padding-left: 1.4em; }
.page li { margin: 0.25em 0; }
.page li::marker { color: var(--muted); }
.page li.task { list-style: none; position: relative; }
.check { display: inline-block; width: 14px; height: 14px; border: 1.5px solid var(--muted); border-radius: 4px; margin: 0 8px -2px -22px; }
.check.done { background: var(--accent); border-color: var(--accent); }
.page hr { border: 0; border-top: 1px solid var(--line); margin: 2.5em 0; }
.page img { max-width: 100%; }
code { font-family: var(--font-mono); font-size: 0.87em; background: var(--surface-2); padding: 0.12em 0.36em; border-radius: 5px; overflow-wrap: anywhere; }
a code { color: inherit; }
blockquote { margin: 1.4em 0; padding: 12px 18px; background: var(--surface); border: 1px solid var(--line); border-left: 3px solid var(--accent-strong); }
blockquote p { margin: 0.4em 0; }
.table { overflow-x: auto; margin: 1.2em 0; border: 1px solid var(--line); border-radius: 10px; }
table { border-collapse: collapse; width: 100%; font-size: 14.5px; }
th { text-align: left; font: 600 12px/1.4 var(--font-body); text-transform: uppercase; letter-spacing: 0.06em; color: var(--muted); background: var(--surface); }
th, td { padding: 9px 14px; border-bottom: 1px solid var(--line); vertical-align: top; }
tbody tr:last-child td { border-bottom: 0; }
td code { white-space: nowrap; }
td:first-child code { white-space: normal; }
.code { margin: 1.2em 0; border: 1px solid var(--line); border-radius: 10px; background: var(--surface); overflow: hidden; }
.code-bar { display: flex; justify-content: space-between; align-items: center; padding: 6px 8px 6px 14px; border-bottom: 1px solid var(--line);
  font: 500 11.5px var(--font-mono); color: var(--muted); text-transform: uppercase; letter-spacing: 0.06em; }
.copy { font: 500 12px var(--font-body); color: var(--fg); background: var(--surface-2); border: 1px solid var(--line); border-radius: 7px; padding: 4px 10px; cursor: pointer; text-transform: none; letter-spacing: 0; }
.copy:hover { border-color: var(--muted); }
.copy.done { background: var(--accent); color: var(--on-accent); border-color: var(--accent); }
pre { margin: 0; padding: 14px 16px; overflow-x: auto; font: 13.5px/1.6 var(--font-mono); tab-size: 2; }
pre code { background: none; padding: 0; font-size: inherit; border-radius: 0; overflow-wrap: normal; }
.tk-kw { color: var(--tk-kw); } .tk-str { color: var(--tk-str); } .tk-com { color: var(--tk-com); font-style: italic; }
.tk-num, .tk-lit { color: var(--tk-num); } .tk-api { color: var(--tk-api); font-weight: 600; } .tk-fn { color: var(--tk-fn); }
.tk-key { color: var(--tk-key); } .tk-re { color: var(--tk-re); }
.file-ref { font-family: var(--font-mono); font-size: 0.9em; }

.page { container-type: inline-size; }
.hero { display: grid; grid-template-columns: minmax(0, 1fr); gap: 24px; align-items: center; margin: 4px 0 36px;
  padding: 28px; border: 1px solid var(--line); border-radius: 16px; background:
  radial-gradient(120% 90% at 0% 0%, color-mix(in srgb, var(--accent) 12%, transparent), transparent 60%), var(--surface); }
.hero h1 { margin: 0 0 12px; border: 0; }
.hero .lead { margin: 0 0 18px; color: var(--muted); font-size: 17px; }
.hero .stats { display: flex; flex-wrap: wrap; gap: 8px; margin: 0 0 20px; padding: 0; list-style: none; }
.hero .stats li { font: 500 12.5px/1 var(--font-mono); padding: 7px 10px; border-radius: 999px; border: 1px solid var(--line); background: var(--bg); color: var(--fg); }
.hero .stats b { color: var(--accent-strong); font-variant-numeric: tabular-nums; }
.hero .actions { display: flex; flex-wrap: wrap; gap: 10px; }
.btn { display: inline-flex; align-items: center; height: 40px; padding: 0 16px; border-radius: 10px; font-weight: 600; font-size: 14.5px; text-decoration: none;
  border: 1px solid var(--line); color: var(--fg); background: var(--bg); }
.btn.primary { background: var(--accent); color: var(--on-accent); border-color: var(--accent); }
.btn:hover { border-color: var(--muted); }
.hero .code { margin: 0; box-shadow: var(--shadow); }
@container (min-width: 980px) { .hero { grid-template-columns: minmax(0, 0.9fr) minmax(0, 1.1fr); } }
.hero .code pre { font-size: 13px; }

.pager { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 56px; max-width: 760px; }
.pager a { display: block; padding: 14px 16px; border: 1px solid var(--line); border-radius: 12px; text-decoration: none; color: var(--fg); min-width: 0; }
.pager a:hover { border-color: var(--accent-strong); }
.pager small { display: block; color: var(--muted); font-size: 12.5px; }
.pager .next { text-align: right; grid-column: 2; }
.foot { max-width: 760px; margin-top: 28px; padding-top: 16px; border-top: 1px solid var(--line); font-size: 13px; color: var(--muted); }

.outline { position: sticky; top: calc(var(--top) + env(safe-area-inset-top, 0px)); align-self: start; max-height: calc(100vh - var(--top)); overflow: auto; padding-block: 32px; font-size: 13.5px; }
.outline h4 { margin: 0 0 8px; font: 600 11px/1.4 var(--font-body); text-transform: uppercase; letter-spacing: 0.09em; color: var(--muted); }
.outline a { display: block; padding: 4px 0 4px 12px; border-left: 1px solid var(--line); color: var(--muted); text-decoration: none; line-height: 1.4; }
.outline a.l3 { padding-left: 24px; font-size: 13px; }
.outline a:hover { color: var(--fg); }
.outline a.active { color: var(--accent-strong); border-left-color: var(--accent-strong); }

@media (max-width: 1200px) { .layout { grid-template-columns: 250px minmax(0, 1fr); } .outline { display: none; } }
@media (max-width: 900px) {
  .layout { grid-template-columns: minmax(0, 1fr); gap: 0; }
  .menu-btn { display: grid; }
  .sidebar { position: fixed; z-index: 30; top: calc(var(--top) + env(safe-area-inset-top, 0px)); left: 0; bottom: 0; height: auto; width: min(300px, 86vw); align-self: stretch;
    padding: 20px 14px 40px; background: var(--bg); border-right: 1px solid var(--line); box-shadow: var(--shadow); transform: translateX(-105%); transition: transform 0.2s ease; }
  body.nav-open .sidebar { transform: none; }
  .hero { grid-template-columns: minmax(0, 1fr); padding: 20px; }
  .brand small { display: none; }
  .search kbd { display: none; }
}
@media (max-width: 520px) { .brand b { display: none; } .pager { grid-template-columns: minmax(0, 1fr); } .pager .next { grid-column: 1; } }
@media (prefers-reduced-motion: reduce) { * { transition: none !important; scroll-behavior: auto !important; } }
"""

SCRIPT = r"""
(() => {
  const root = document.documentElement;
  const pages = new Map([...document.querySelectorAll('article.page')].map((el) => [el.id, el]));
  const navLinks = [...document.querySelectorAll('.sidebar a[data-page]')];
  const outline = document.getElementById('outline');
  const index = JSON.parse(document.getElementById('search-index').textContent);
  const titles = Object.fromEntries(navLinks.map((a) => [a.dataset.page, a.dataset.title]));
  let current = null;
  let spy = null;

  const store = {
    get(key) { try { return localStorage.getItem(key); } catch (e) { return null; } },
    set(key, value) { try { localStorage.setItem(key, value); } catch (e) { /* storage blocked */ } },
  };
  const saved = store.get('dzg-docs-theme');
  if (saved === 'light' || saved === 'dark') root.dataset.theme = saved;
  document.getElementById('theme').addEventListener('click', () => {
    const dark = root.dataset.theme ? root.dataset.theme === 'dark' : !matchMedia('(prefers-color-scheme: light)').matches;
    root.dataset.theme = dark ? 'light' : 'dark';
    store.set('dzg-docs-theme', root.dataset.theme);
  });

  const pageOf = (id) => {
    if (pages.has(id)) return id;
    const el = document.getElementById(id);
    const page = el && el.closest('article.page');
    return page ? page.id : null;
  };

  function buildOutline(page) {
    if (spy) spy.disconnect();
    const heads = [...page.querySelectorAll('h2, h3')];
    outline.innerHTML = heads.length ? '<h4>На странице</h4>' : '';
    const links = new Map();
    for (const h of heads) {
      const a = document.createElement('a');
      a.href = '#' + h.id;
      a.textContent = h.firstChild ? h.textContent.replace(/#$/, '') : '';
      a.className = h.tagName === 'H3' ? 'l3' : 'l2';
      outline.appendChild(a);
      links.set(h, a);
    }
    if (!('IntersectionObserver' in window)) return;
    spy = new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting) continue;
        outline.querySelectorAll('a.active').forEach((x) => x.classList.remove('active'));
        const a = links.get(entry.target);
        if (a) a.classList.add('active');
      }
    }, { rootMargin: '-70px 0px -70% 0px' });
    heads.forEach((h) => spy.observe(h));
  }

  function show(id, push) {
    const pageId = pageOf(id) || 'home';
    if (pageId !== current) {
      for (const [key, el] of pages) el.hidden = key !== pageId;
      navLinks.forEach((a) => a.classList.toggle('active', a.dataset.page === pageId));
      current = pageId;
      buildOutline(pages.get(pageId));
    }
    const target = id && id !== pageId ? document.getElementById(id) : null;
    if (target) target.scrollIntoView();
    else window.scrollTo(0, 0);
    if (push) { try { history.pushState(null, '', '#' + (id || pageId)); } catch (e) { /* sandboxed */ } }
    document.body.classList.remove('nav-open');
  }

  document.addEventListener('click', (event) => {
    const a = event.target.closest('a[href^="#"]');
    if (!a) return;
    const id = decodeURIComponent(a.getAttribute('href').slice(1));
    if (!id || !pageOf(id)) return;
    event.preventDefault();
    closeResults();
    show(id, true);
  });
  window.addEventListener('hashchange', () => show(decodeURIComponent(location.hash.slice(1)), false));
  window.addEventListener('popstate', () => show(decodeURIComponent(location.hash.slice(1)), false));

  document.getElementById('menu').addEventListener('click', () => document.body.classList.toggle('nav-open'));

  document.addEventListener('click', (event) => {
    const button = event.target.closest('button.copy');
    if (!button) return;
    const code = button.closest('.code').querySelector('pre code');
    const done = () => {
      button.textContent = 'Скопировано';
      button.classList.add('done');
      setTimeout(() => { button.textContent = 'Копировать'; button.classList.remove('done'); }, 1600);
    };
    const fallback = () => {
      const range = document.createRange();
      range.selectNodeContents(code);
      const selection = getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      try { if (document.execCommand('copy')) { done(); return; } } catch (e) { /* not allowed */ }
      button.textContent = 'Выделено: Ctrl+C';
    };
    try {
      navigator.clipboard.writeText(code.innerText).then(done, fallback);
    } catch (e) {
      fallback();
    }
  });

  const input = document.getElementById('q');
  if (matchMedia('(max-width: 520px)').matches) input.placeholder = 'Поиск';
  const results = document.getElementById('results');
  let selected = 0;
  const escapeHtml = (s) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const mark = (text, words) => {
    let out = escapeHtml(text);
    for (const w of words) out = out.replace(new RegExp(w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi'), (m) => '<mark>' + m + '</mark>');
    return out;
  };
  function closeResults() { results.hidden = true; }
  function search() {
    const query = input.value.trim().toLowerCase();
    if (!query) {
      closeResults();
      navLinks.forEach((a) => a.classList.remove('dim'));
      return;
    }
    const words = query.split(/\s+/).filter(Boolean);
    const hits = [];
    for (const entry of index) {
      const head = entry.t.toLowerCase();
      const text = entry.x.toLowerCase();
      let score = 0;
      let ok = true;
      for (const w of words) {
        if (head.includes(w)) score += 10;
        else if (text.includes(w)) score += 1;
        else { ok = false; break; }
      }
      if (ok) hits.push({ entry, score });
    }
    hits.sort((a, b) => b.score - a.score);
    const pagesHit = new Set(hits.map((h) => h.entry.p));
    navLinks.forEach((a) => a.classList.toggle('dim', !pagesHit.has(a.dataset.page)));
    selected = 0;
    results.innerHTML = hits.length ? hits.slice(0, 30).map((h, k) => {
      const e = h.entry;
      const text = e.x.toLowerCase();
      const at = Math.max(0, text.indexOf(words[0]) - 40);
      const snippet = (at > 0 ? '…' : '') + e.x.slice(at, at + 140);
      return '<a href="#' + e.h + '"' + (k === 0 ? ' class="active"' : '') + '><span class="where">' + escapeHtml(titles[e.p] || e.p) + '</span>'
        + mark(e.t, words) + '<span class="snippet">' + mark(snippet, words) + '</span></a>';
    }).join('') : '<div class="empty">Ничего не нашлось. Попробуй имя метода: <code>send</code>, <code>panel</code>, <code>onUpdate</code></div>';
    results.hidden = false;
  }
  input.addEventListener('input', search);
  input.addEventListener('focus', () => { if (input.value.trim()) search(); });
  input.addEventListener('keydown', (event) => {
    const items = [...results.querySelectorAll('a')];
    if (event.key === 'Escape') { input.value = ''; search(); input.blur(); }
    else if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      if (!items.length) return;
      event.preventDefault();
      items[selected]?.classList.remove('active');
      selected = (selected + (event.key === 'ArrowDown' ? 1 : items.length - 1)) % items.length;
      items[selected].classList.add('active');
      items[selected].scrollIntoView({ block: 'nearest' });
    } else if (event.key === 'Enter' && items[selected]) {
      event.preventDefault();
      items[selected].click();
    }
  });
  document.addEventListener('keydown', (event) => {
    const typing = /^(INPUT|TEXTAREA)$/.test(document.activeElement?.tagName || '');
    if (event.key === '/' && !typing) { event.preventDefault(); input.focus(); input.select(); }
  });
  document.addEventListener('click', (event) => {
    if (!event.target.closest('.search')) closeResults();
  });

  show(decodeURIComponent(location.hash.slice(1)), false);
})();
"""


def load_reference_model():
    spec = importlib.util.spec_from_file_location('gen_reference', os.path.join(ROOT, 'tools', 'damza', 'gen_reference.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    model = module.Model()
    types = os.path.join(DOCS, 'types')
    for name in module.ORDER:
        path = os.path.join(types, name)
        if os.path.exists(path):
            model.add_file(name, open(path, encoding='utf-8').read())
    return model


def stats():
    model = load_reference_model()
    events = len(model.interfaces['EventMap']['members'])
    methods = set()
    namespaces = set()
    for name, entry in model.interfaces.items():
        if re.fullmatch(r'Dzg\w+', name) or name in ('AppInfo',):
            namespaces.add(name)
            for member in entry['members']:
                if member['kind'] == 'method':
                    methods.add((name, member['name']))
    for member in model.interfaces['Dzg']['members']:
        if member['kind'] == 'method':
            methods.add(('Dzg', member['name']))
    layer = '229'
    layer_file = os.path.join(ROOT, 'Telegram', 'SourceFiles', 'damza', 'plugins', 'tl', 'tl_schema_layer.h')
    if os.path.exists(layer_file):
        m = re.search(r'kEmbeddedLayer\s*=\s*(\d+)', open(layer_file, encoding='utf-8').read())
        if m:
            layer = m.group(1)
    return {'events': events, 'methods': len(methods), 'namespaces': len(namespaces), 'layer': layer}


HERO_CODE = """// main.js — плагин целиком
dzg.on('message.new', async (m) => {
  if (m.out || !/срочно/i.test(m.text)) return;
  dzg.ui.notify({
    title: m.chat.name,
    text: `${m.fromName}: ${m.text}`,
    onClick: () => dzg.chats.open(m.chat, m.id),
  });
});

dzg.commands.register('dice', ({ chat }) =>
  dzg.messages.send(chat, `🎲 ${1 + Math.floor(Math.random() * 6)}`));"""


def hero(numbers):
    code = ('<div class="code"><div class="code-bar"><span>JavaScript</span><button type="button" class="copy">Копировать</button></div>'
            '<pre><code class="lang-js">%s</code></pre></div>' % highlight_js(HERO_CODE))
    return ('<section class="hero" aria-label="DamZaGram Plugins">'
            '<div><h1 id="home--top">Плагины DamZaGram</h1>'
            '<p class="lead">JavaScript внутри Telegram-клиента: события, сообщения, чаты, интерфейс и весь MTProto. '
            'Сохранил файл — плагин уже перезапущен.</p>'
            '<ul class="stats"><li><b>%d</b> событий</li><li><b>%d</b> методов</li><li><b>%d</b> пространств <code>dzg.*</code></li>'
            '<li>MTProto слой <b>%s</b></li><li>ES2023 · QuickJS-ng</li></ul>'
            '<div class="actions"><a class="btn primary" href="#guide-01">Первый плагин за 15 минут</a>'
            '<a class="btn" href="#reference">Справочник</a><a class="btn" href="#cookbook">Рецепты</a></div></div>'
            '%s</section>' % (numbers['events'], numbers['methods'], numbers['namespaces'], numbers['layer'], code))


def section_index(page_id, page_html, title):
    """Split a rendered page into (heading id, heading text, plain text) chunks."""
    entries = []
    parts = re.split(r'(<h[1-4] id="[^"]+">.*?</h[1-4]>)', page_html, flags=re.S)
    head_id, head_text = page_id, title
    for part in parts:
        m = re.match(r'<h[1-4] id="([^"]+)">(.*?)</h[1-4]>', part, re.S)
        if m:
            head_id = m.group(1)
            head_text = strip_tags(re.sub(r'<a class="anchor".*?</a>', '', m.group(2))).strip()
            continue
        part = re.sub(r'<div class="code-bar">.*?</div>', ' ', part, flags=re.S)
        part = re.sub(r'</(p|li|h\d|td|th|tr|div|pre|blockquote|ul|ol)>|<br>', ' ', part)
        text = re.sub(r'\s+', ' ', strip_tags(part)).strip()
        if text or head_id != page_id:
            entries.append({'p': page_id, 'h': head_id, 't': head_text, 'x': text[:1200]})
    return entries


def build():
    resolver = Resolver()
    sources = {}
    # Pass 1: heading ids, so links can point to any page's sections.
    for path, page_id, _, _ in PAGES:
        text = open(os.path.join(DOCS, path), encoding='utf-8').read()
        sources[page_id] = text
        render_blocks(text.split('\n'), Context(path, page_id, resolver))
    # Pass 2: render with links resolved.
    resolver.missing.clear()
    numbers = stats()
    articles, index, titles = [], [], {}
    for k, (path, page_id, label, group) in enumerate(PAGES):
        ctx = Context(path, page_id, resolver)
        body = render_blocks(sources[page_id].split('\n'), ctx)
        title = next((t for level, _, t in ctx.headings if level == 1), label)
        titles[page_id] = title
        if page_id == 'home':
            body = re.sub(r'<h1 id="[^"]+">.*?</h1>', '', body, count=1, flags=re.S)
            body = hero(numbers) + body
        index += section_index(page_id, body, title)
        prev_page = PAGES[k - 1] if k > 0 else None
        next_page = PAGES[k + 1] if k + 1 < len(PAGES) else None
        pager = '<nav class="pager" aria-label="Соседние страницы">'
        if prev_page:
            pager += '<a class="prev" href="#%s"><small>Назад</small>%s</a>' % (prev_page[1], html.escape(prev_page[2]))
        if next_page:
            pager += '<a class="next" href="#%s"><small>Дальше</small>%s</a>' % (next_page[1], html.escape(next_page[2]))
        pager += '</nav>'
        source = os.path.relpath(os.path.join(DOCS, path), OUT_DIR).replace(os.sep, '/')
        foot = ('<p class="foot">Исходник страницы: <a href="%s">docs/plugins/%s</a> · DamZaGram by '
                '<a href="https://t.me/wetakush" target="_blank" rel="noopener">@wetakush</a></p>' % (source, path))
        articles.append('<article class="page" id="%s" data-title="%s"%s>%s%s%s</article>' % (
            page_id, html.escape(title), '' if page_id == 'home' else ' hidden', body, pager, foot))

    nav, groups = [], []
    for path, page_id, label, group in PAGES:
        if group not in groups:
            groups.append(group)
    for group in groups:
        nav.append('<div class="nav-group"><h4>%s</h4>' % html.escape(group))
        for path, page_id, label, g in PAGES:
            if g != group:
                continue
            num = ''
            m = re.match(r'guide-(\d+)', page_id)
            if m:
                num = '<span class="num">%s</span>' % m.group(1)
            nav.append('<a href="#%s" data-page="%s" data-title="%s"%s>%s<span>%s</span></a>' % (
                page_id, page_id, html.escape(label), ' class="active"' if page_id == 'home' else '', num, html.escape(label)))
        nav.append('</div>')
    nav.append('<div class="nav-foot">DamZaGram by <a href="https://t.me/wetakush" target="_blank" rel="noopener">@wetakush</a><br>'
               'Plugin API v2 · MTProto слой %s</div>' % numbers['layer'])

    index_json = json.dumps(index, ensure_ascii=False, separators=(',', ':')).replace('</', '<\\/')
    head = ('<title>Плагины DamZaGram</title>\n'
            '<meta name="description" content="Документация плагинной системы DamZaGram: гайды, рецепты, справочник API.">\n'
            '<link rel="preconnect" href="https://fonts.googleapis.com">\n'
            '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
            '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Golos+Text:wght@400;500;600&family=JetBrains+Mono:wght@400;500;600&family=Unbounded:wght@500;600&display=swap">\n'
            '<style>%s</style>\n' % CSS)
    body = ('<header class="topbar">'
            '<button type="button" class="icon-btn menu-btn" id="menu" aria-label="Разделы">'
            '<svg width="18" height="18" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h16M4 12h16M4 17h16" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg></button>'
            '<a class="brand" href="#home">%s<span><b>DamZaGram</b><small>plugins · API v2</small></span></a>'
            '<div class="search"><input id="q" type="search" placeholder="Поиск: метод, событие, рецепт…" autocomplete="off" aria-label="Поиск по документации">'
            '<kbd>/</kbd><div class="results" id="results" hidden></div></div>'
            '<button type="button" class="icon-btn" id="theme" aria-label="Сменить тему" title="Светлая / тёмная тема">'
            '<svg width="18" height="18" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="8.5" fill="none" stroke="currentColor" stroke-width="2"/>'
            '<path d="M12 3.5a8.5 8.5 0 0 1 0 17z" fill="currentColor"/></svg></button>'
            '</header>'
            '<div class="layout"><nav class="sidebar" aria-label="Разделы">%s</nav>'
            '<main>%s</main><aside class="outline" id="outline" aria-label="На странице"></aside></div>'
            '<script type="application/json" id="search-index">%s</script>'
            '<script>%s</script>' % (LOGO, ''.join(nav), ''.join(articles), index_json, SCRIPT))
    document = ('<!doctype html>\n<html lang="ru">\n<head>\n<meta charset="utf-8">\n'
                '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
                '%s</head>\n<body>\n%s\n</body>\n</html>\n' % (head, body))
    # Without the repository next to it, links to source files lead nowhere:
    # the fragment shows them as plain file names.
    fragment = head + re.sub(r'<a href="\.\./[^"]*"[^>]*>(.*?)</a>', r'<span class="file-ref">\1</span>', body) + '\n'
    for item in sorted(resolver.missing):
        print('warning: unresolved anchor %s' % item)
    return document, fragment


def main():
    document, fragment = build()
    os.makedirs(OUT_DIR, exist_ok=True)
    open(OUT, 'w', encoding='utf-8').write(document)
    print('wrote %s (%d KB)' % (os.path.relpath(OUT, ROOT), len(document.encode('utf-8')) // 1024))
    for arg in sys.argv[1:]:
        if arg.startswith('--fragment='):
            path = arg.split('=', 1)[1]
            open(path, 'w', encoding='utf-8').write(fragment)
            print('wrote fragment %s' % path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
