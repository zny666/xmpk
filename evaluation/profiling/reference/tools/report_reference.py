#!/usr/bin/env python3
"""Offline R20 archive reporter. Standard library only; never invokes perf.

Usage: python3 report_reference.py ROOT --run raw/profile-ID \
           --output reports/NEW-ID [--allow-partial]
An incomplete strict invocation writes nothing and exits 2. Explicit partial
reports exit 0 but retain status=incomplete and every missing measurement row.
All paths in manifests are relative to ROOT, not to the run directory.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import sys
from urllib.parse import quote

ALGORITHMS = {
    'mayo': tuple('MAYO-' + str(n) for n in (1, 2, 3, 5)),
    'uov': ('UOV-Is', 'UOV-Ip', 'UOV-III', 'UOV-V'),
    'snova': ('SNOVA_I_B_AES', 'SNOVA_III_B_AES', 'SNOVA_V_B_AES'),
    'qruov': tuple(f'{c}q127L{n}' for c in (1, 3, 5) for n in (3, 10)),
    'mqom': tuple(f'mqom3_cat{c}_gf16_fast_ct' for c in (1, 3, 5)),
}
OPERATIONS = ('keygen', 'sign', 'verify')
CORE = '{cycles:u,instructions:u}'
AUX = ('{branches:u,branch-misses:u}',
       '{cpu/L1-dcache-loads/u,cpu/L1-dcache-load-misses/u}')
CATEGORIES = ('Algebra', 'hashAES', 'RNG', 'memory', 'other', 'unknown')
# Priority is deliberately explicit: unknown, RNG, memory, hashAES, Algebra,
# other. The DSO is NOT a category shortcut (libcrypto contains many things).
CATEGORY_RULES = (
    ('RNG', r'randombytes|getrandom|getentropy|(^|_)rng($|_)|(^|_)rand_bytes|ctr_drbg|ctrdrbg'),
    ('memory', r'(^|_)(memcpy|memmove|memset|memcmp|bzero|malloc|calloc|realloc|free)(_|@|$)'),
    ('hashAES', r'aes|rijndael|keccak|shake|sha3|sha256|sha512|sha2|(^|_)hash|(^|_)xof|evp_(digest|encrypt|decrypt|cipher)'),
    ('Algebra', r'gf(2|4|16|31|127|256)|(^|_)(fq|field|matrix|matmul|matvec|gauss|polynomial|poly|blas|bitslice)(_|[0-9]|$)|quadratic|publicmap'),
)
SAMPLE_BITS = {
    0: 'IP', 1: 'TID', 2: 'TIME', 3: 'ADDR', 4: 'READ', 5: 'CALLCHAIN',
    6: 'ID', 7: 'CPU', 8: 'PERIOD', 9: 'STREAM_ID', 10: 'RAW',
    11: 'BRANCH_STACK', 12: 'REGS_USER', 13: 'STACK_USER', 14: 'WEIGHT',
    15: 'DATA_SRC', 16: 'IDENTIFIER', 17: 'TRANSACTION', 18: 'REGS_INTR',
    19: 'PHYS_ADDR', 20: 'AUX', 21: 'CGROUP', 22: 'DATA_PAGE_SIZE',
    23: 'CODE_PAGE_SIZE', 24: 'WEIGHT_STRUCT',
}
FORBIDDEN = {'RAW', 'REGS_USER', 'STACK_USER', 'REGS_INTR', 'DATA_SRC', 'PHYS_ADDR', 'AUX'}
EXPORTS = ('events.txt', 'buildids.txt', 'report-self.txt', 'report-children.txt',
           'samples.txt', 'header.txt')
NOTES = """## 口径与限制

- 本报告只读取本机归档，不执行 perf、构建、联网或重新采样。原始文本、环境 samples、命令、perf.data 的位置及 SHA-256 均保留；链接指向输入归档，移动报告时应保留 root 布局。当前文件哈希不等于已验证上游真实性；预期哈希的匹配情况另列。
- baseline 仅 timing-0..6 的 worker observation.wall_ns / iterations；列出全部 7 trial、median/min/max、总体标准差/均值 CV。thread_cpu_ns 同样单列。绝不使用 perf-stat 或 whole_process_wall_ns 替代 baseline。
- 每参数仅一个新建 key fixture，Sign/Verify 跨 pass 共享，不是多 key 统计。KeyGen 每次生成新 key，全部生成 key 的签验在计时/perf gate 外；Sign 保留并在 gate 外验证全部输出。Verify 在每个进程内重复同一有效签名 fixture；签名输入在进程启动后于 gate 外生成，不保证跨 pass 字节相同。
- 每批新进程，不清空 CPU cache；内部 RNG、密钥展开、API/循环/时钟边界开销保留。冷启动、lazy initialization、first-touch 和不同 iterations 会影响 ns/op。sample/baseline 是观测比值，不是因果开销或可扣除的修正量。
- self 自耗时比例是 samples 的 period cycle weight / 全部 self period，不是精确 wall time。两个 sample pass 按 period weight 合并；function+DSO 唯一，unknown、共享 libcrypto 及其它 DSO 保留在全分母。绝不把缺失百分比补成 0 或凭空制造 unknown 样本。
- children 是重叠的 inclusive 占比，按各 pass self 总 period 加权其已打印百分比（受打印精度限制），不能加总为 100%，也不能与 self 混排。children 表内 Samples/Period 不冒充 inclusive 样本数/周期数。
- 核心 cycles/op、instructions/op 和同 pass IPC 仅用明确 100% running 且 runtime 有效的事件；IPC/分支 miss rate 要求同 pass、相同 runtime 的分子分母。事件不跨 pass 拼接；不支持、未计数、空值和 multiplexing 保留为缺失或受限，不当成 0。
- 少于 1000 samples 警告；即使达到门槛也不保证稳定热点排名。丢样只报告明确字段；missing loss 不等于 0。FP callchain 的完整性/全 source 覆盖不能仅凭导出文件证明，记为 unknown。annotate 仅采集器选中的符号，不代表全部源码。
- 原始 C reference 的 -O3、内联、符号折叠/克隆与优化会改变函数归属；不作每函数高精度时间结论。MQOM 的 ct 是 correlated-tree，不是 constant-time 声明；参考实现保留其上游 portable/SWAR 算法。
"""


class ParseError(ValueError):
    pass


class IncompleteError(ValueError):
    pass


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def number(value, label, integer=False):
    value = str(value).strip()
    pattern = r'\d+' if integer else r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
    if not re.fullmatch(pattern, value):
        raise ParseError(f'{label}: 无法解析数字 {value!r}')
    result = int(value) if integer else float(value)
    if not math.isfinite(result) or result < 0:
        raise ParseError(f'{label}: 非有限或负数 {value!r}')
    return result


def percentage(value, label):
    if not value.strip().endswith('%'):
        raise ParseError(f'{label}: 百分比缺少 %: {value!r}')
    result = number(value.strip()[:-1], label)
    if result > 100:
        raise ParseError(f'{label}: 百分比大于 100')
    return result


def parse_report(text, mode, source='<report>'):
    """Parse actual named table columns, never positional perf-version guesses.

    Unsupported script/report layouts fail with a file+line diagnostic. We do
    not silently fall back to unweighted script counts or invent percentages.
    """
    aliases = {'overhead': 'overhead', 'self': 'self', 'children': 'children',
               'samples': 'samples', 'period': 'period',
               'shared object': 'dso', 'symbol': 'symbol'}
    header = None
    rows = []
    event_totals = []
    callgraph_lines = []
    event_names = []
    for lineno, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        total = re.search(r'^#\s*Event count \(approx\.\):\s*(\d+)\s*$', stripped)
        if total:
            event_totals.append(int(total.group(1)))
        event = re.search(r"^#.*\bof event '([^']+)'", stripped)
        if event:
            event_names.append(event.group(1))
            if event.group(1) != 'cycles:u':
                raise ParseError(f'{source}:{lineno}: 不是 cycles:u，不能冒充 cycle weight: {event.group(1)!r}')
        # perf may print the standard ASCII callgraph between histogram rows.
        # Retain recognizable decorations separately, never count them as IPs.
        if header and stripped and (re.fullmatch(r'[|`+\- ]+', stripped) or re.fullmatch(r'[|`+ ]*-{2,}[^|]*', stripped)):
            callgraph_lines.append({'line': lineno, 'raw': line})
            continue
        if '|' not in line:
            if header and stripped and not stripped.startswith('#') and not re.fullmatch(r'[.\-\s]+', stripped):
                raise ParseError(f'{source}:{lineno}: 无法识别的非表格/callgraph 行: {stripped!r}')
            continue
        fields = [x.strip() for x in stripped.lstrip('#').strip().split('|')]
        names = [aliases.get(x.lower(), x) for x in fields]
        if 'dso' in names and 'symbol' in names:
            if header is not None:
                raise ParseError(f'{source}:{lineno}: 多表/多事件不能混合归一化')
            if len(set(names)) != len(names):
                raise ParseError(f'{source}:{lineno}: 重复表头 {fields!r}')
            required = {'dso', 'symbol', 'samples', 'period'}
            if not required.issubset(names) or (mode == 'self' and not {'self', 'overhead'}.intersection(names)) or (mode == 'children' and 'children' not in names):
                raise ParseError(f'{source}:{lineno}: 不支持的 {mode} 表头 {fields!r}')
            header = names
            continue
        if stripped.startswith('#') or re.fullmatch(r'[.\-|\s]+', stripped):
            continue
        if header is None:
            raise ParseError(f'{source}:{lineno}: 数据之前缺少实际表头')
        if len(fields) != len(header):
            raise ParseError(f'{source}:{lineno}: 表头 {len(header)} 列，数据 {len(fields)} 列: {line!r}')
        raw = dict(zip(header, fields))
        label = f'{source}:{lineno}'
        row = {'dso': raw['dso'] or '[unknown]',
               'symbol': re.sub(r'^\[[.kguH]+\]\s*', '', raw['symbol']).strip() or '[unknown]',
               'samples': number(raw['samples'], label + '/Samples', True),
               'period': number(raw['period'], label + '/Period', True),
               'raw': raw, 'line': lineno, 'source': source}
        for column in ('overhead', 'self', 'children'):
            if column in raw:
                row[column + '_pct'] = percentage(raw[column], label + '/' + column)
        rows.append(row)
    if header is None or not rows:
        raise ParseError(f'{source}: 无可解析的 {mode} tableheader/数据行；需原始 --field-separator | --show-nr-samples --show-total-period 导出；samples.txt 未被猜测解析')
    total = sum(row['period'] for row in rows) if mode == 'self' else None
    if mode == 'self':
        if total <= 0 or sum(row['samples'] for row in rows) <= 0:
            raise ParseError(f'{source}: self 样本或 period 分母为零')
        if event_totals and (len(event_totals) != 1 or event_totals[0] != total):
            raise ParseError(f'{source}: 全表 period={total} 与 event count={event_totals} 不符；不能假设缺失占比为零')
        for row in rows:
            printed = row.get('self_pct', row.get('overhead_pct'))
            if abs(printed - row['period'] * 100 / total) > 0.011:
                raise ParseError(f'{source}:{row["line"]}: printed self={printed}% 与全表 period 分母不一致；可能被过滤/多事件/格式不支持')
    return {'header': header, 'rows': rows, 'total_period': total,
            'event_count_headers': event_totals, 'event_names': event_names,
            'callgraph_lines_not_histogram_rows': callgraph_lines, 'source': source}


def category(dso, symbol):
    if not dso or dso.lower() in ('[unknown]', 'unknown', '[unknown kernel.kallsyms]') or not symbol or symbol.lower() in ('[unknown]', 'unknown') or re.fullmatch(r'(?:0x)?[0-9a-fA-F]+', symbol):
        return 'unknown'
    for name, pattern in CATEGORY_RULES:
        if re.search(pattern, symbol, re.I):
            return name
    return 'other'


def aggregate_self(tables):
    total = sum(table['total_period'] for table in tables)
    grouped = {}
    for table in tables:
        for row in table['rows']:
            key = (row['dso'], row['symbol'])
            entry = grouped.setdefault(key, {'dso': key[0], 'symbol': key[1], 'samples': 0,
                                            'period': 0, 'category': category(*key), 'evidence': []})
            entry['samples'] += row['samples']
            entry['period'] += row['period']
            entry['evidence'].append(row)
    result = sorted(grouped.values(), key=lambda x: (-x['period'], x['dso'], x['symbol']))
    cumulative = 0
    for row in result:
        cumulative += row['period']
        row.update(self_pct=100 * row['period'] / total,
                   cumulative_pct=100 * cumulative / total)
    return result


def aggregate_children(pairs):
    total = sum(weight for _, weight in pairs)
    grouped = {}
    for table, weight in pairs:
        seen = set()
        for row in table['rows']:
            key = (row['dso'], row['symbol'])
            if key in seen:
                raise ParseError(f'{table["source"]}: 重复 children function+DSO，无法判断重叠，拒绝相加: {key}')
            seen.add(key)
            entry = grouped.setdefault(key, {'dso': key[0], 'symbol': key[1],
                                            'weighted_numerator': 0, 'evidence': []})
            entry['weighted_numerator'] += row['children_pct'] * weight
            entry['evidence'].append(row)
    for entry in grouped.values():
        entry['children_pct'] = entry.pop('weighted_numerator') / total
        entry['denominator_period'] = total
    return sorted(grouped.values(), key=lambda x: (-x['children_pct'], x['dso'], x['symbol']))


def parse_stat(text, source='<perf-stat>'):
    rows = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        fields = line.split('|')  # Not comma CSV: event groups themselves contain commas.
        if len(fields) < 3:
            raise ParseError(f'{source}:{lineno}: perf-stat 需要 count|unit|event，实际 {line!r}')
        raw = [field.strip() for field in fields]
        row = {'line': lineno, 'source': source, 'raw_fields': fields,
               'count_raw': raw[0], 'unit': raw[1], 'event': raw[2],
               'count': None, 'runtime': None, 'running_pct': None,
               'nonmultiplexed': False, 'status': 'missing', 'extra_fields': raw[5:]}
        if raw[0].lower() in ('<not counted>', '<not supported>'):
            row['status'] = raw[0][1:-1].replace(' ', '_')
        elif raw[0]:
            row['count'] = number(raw[0], f'{source}:{lineno}/count')
            row['status'] = 'counted'
        if len(raw) > 3 and raw[3]:
            row['runtime'] = number(raw[3], f'{source}:{lineno}/runtime')
        if len(raw) > 4 and raw[4]:
            row['running_pct'] = number(raw[4].removesuffix('%'), f'{source}:{lineno}/%running')
            if row['running_pct'] > 100:
                raise ParseError(f'{source}:{lineno}: %running 超过 100')
        row['event_id'] = raw[2].strip('{} ').strip()
        row['nonmultiplexed'] = (row['count'] is not None and row['runtime'] is not None
                                and row['runtime'] > 0 and row['running_pct'] == 100)
        rows.append(row)
    if not rows:
        raise ParseError(f'{source}: perf-stat 无事件数据')
    return rows


def stat_metrics(rows, iterations):
    def event(name):
        found = [row for row in rows if row['event_id'] == name and row['unit'] in ('', 'count')]
        return found[0] if len(found) == 1 and found[0]['nonmultiplexed'] else None

    def ratio(numerator, denominator):
        n, d = event(numerator), event(denominator)
        if n and d and d['count'] > 0 and math.isclose(n['runtime'], d['runtime'], rel_tol=1e-9, abs_tol=1e-6):
            return n['count'] / d['count']
        return None

    cycles, instructions = event('cycles:u'), event('instructions:u')
    return {'cycles_per_op': cycles['count'] / iterations if cycles else None,
            'instructions_per_op': instructions['count'] / iterations if instructions else None,
            'ipc': ratio('instructions:u', 'cycles:u'),
            'branch_miss_rate': ratio('branch-misses:u', 'branches:u'),
            'l1d_load_miss_rate': ratio('cpu/L1-dcache-load-misses/u', 'cpu/L1-dcache-loads/u')}


def sample_safety(text, source='<events.txt>'):
    declarations = [(line, re.search(r'\bsample_type\s*[:=]\s*([^\n]+)', line, re.I)) for line in text.splitlines()]
    declarations = [(line, match.group(1)) for line, match in declarations if match]
    if not declarations:
        raise ParseError(f'{source}: 缺少 sample_type，原始 perf.data 安全性 unknown')
    decoded = [];profile_events = 0
    for line, declaration in declarations:
        metadata_only = line.lstrip().startswith('dummy:u:') and 'PERF_COUNT_SW_DUMMY' in line and 'PERF_TYPE_SOFTWARE' in line
        token = declaration.split(',', 1)[0].strip()
        if re.fullmatch(r'(?:0x[0-9a-fA-F]+|\d+)', token):
            mask = int(token, 16 if token.startswith('0x') else 10)
            if mask >> 25:
                raise ParseError(f'{source}: 未知 sample_type 位 {token}')
            names = {name for bit, name in SAMPLE_BITS.items() if mask & (1 << bit)}
        else:
            names = {name.strip().removeprefix('PERF_SAMPLE_') for name in token.split('|')}
            if not names.issubset(set(SAMPLE_BITS.values())):
                raise ParseError(f'{source}: 未知 sample_type {token!r}')
        unsafe = names & FORBIDDEN
        if unsafe:
            raise ParseError(f'{source}: 禁止的 secret-bearing sample_type: {sorted(unsafe)}')
        if not metadata_only:
            if not {'IP', 'PERIOD', 'CALLCHAIN'}.issubset(names):
                raise ParseError(f'{source}: 缺少 IP/PERIOD/CALLCHAIN，不能验证原始采样合同')
            profile_events += 1
        decoded.append(sorted(names))
    if profile_events != 1:raise ParseError(f'{source}: 必须有且仅有一个实际调用链采样事件，不能只凭dummy元数据通过')
    return {'status': 'checked_safe_sample_type', 'declarations': decoded,
            'callchain_completeness': 'unknown', 'full_source_coverage': 'unknown'}


def loss_evidence(sources):
    """Do not sum duplicate headers/stderr/script views of the same losses."""
    evidence, unparsed = [], []
    patterns = (
        ('total_lost_samples', r'^\s*#?\s*(?:Total\s+)?Lost\s+Samples\s*:\s*(\d+)\s*$'),
        ('lost_samples_message', r'\blost\s+(\d+)\s+samples\b'),
        ('record_lost', r'\bPERF_RECORD_LOST(?:_SAMPLES)?\b.*?\blost\s*[:=]\s*(\d+)\b'),
        ('total_lost_records', r'^\s*#?\s*(?:Total\s+)?Lost\s+(?:Records|Events)\s*:\s*(\d+)\s*$'),
    )
    for source, text in sources.items():
        for lineno, line in enumerate(text.splitlines(), 1):
            loss_content = re.sub(r'\bread_format\s*[:=]\s*[A-Z_| ]+', '', line)
            if not re.search(r'lost|loss', loss_content, re.I):
                continue
            for kind, pattern in patterns:
                match = re.search(pattern, line, re.I)
                if match:
                    evidence.append({'source': source, 'line': lineno, 'kind': kind,
                                     'count': int(match.group(1)), 'raw': line})
                    break
            else:
                unparsed.append({'source': source, 'line': lineno, 'raw': line})
    totals = [item['count'] for item in evidence if item['kind'] == 'total_lost_samples']
    consistent_total = totals[0] if totals and len(set(totals)) == 1 else None
    if consistent_total == 0 and any(item['count'] > 0 for item in evidence):
        consistent_total = None
    return {'total_lost_samples': consistent_total, 'evidence': evidence,
            'unparsed_loss_lines': unparsed, 'duplicate_views_not_summed': True,
            'status': 'explicit_total' if consistent_total is not None else 'unknown'}


def stats(values):
    if not values:
        return {'n': 0, 'median': None, 'min': None, 'max': None, 'cv': None}
    mean = statistics.mean(values)
    return {'n': len(values), 'median': statistics.median(values), 'min': min(values),
            'max': max(values), 'cv': statistics.pstdev(values) / mean if mean else None}


def environment_summary(environment):
    samples = environment.get('samples', [])
    result = {'sample_count': len(samples), 'errors': environment.get('errors'),
              'first_monotonic_ns': samples[0].get('monotonic_ns') if samples else None,
              'last_monotonic_ns': samples[-1].get('monotonic_ns') if samples else None}
    for kind in ('frequencies', 'temperatures', 'cooling_states'):
        values = {}
        for sample in samples:
            for name, value in sample.get(kind, {}).items():
                try:
                    value = float(value)
                except (TypeError, ValueError):
                    continue
                if math.isfinite(value):
                    values.setdefault(name, []).append(value)
        result[kind] = {name: {'min': min(items), 'max': max(items), 'observed': len(items)}
                        for name, items in values.items()}
    result['scope'] = '描述性范围；完整 CPU/IRQ/pressure/cgroup samples 见 result.json，不作频率/热/空闲合格推断'
    return result


def md(value):
    return str(value).replace('|', '\\|').replace('\n', ' ').replace('<', '&lt;').replace('>', '&gt;')


def display(value):
    return 'unknown' if value is None else (f'{value:.6g}' if isinstance(value, float) else str(value))


class Reporter:
    def __init__(self, root, run, output):
        self.root = Path(root).resolve(strict=True)
        self.run = self.resolve(run)
        self.output = self.resolve(output)
        if self.run.parent != self.root / 'raw' or not self.run.name.startswith('profile-') or not self.run.is_dir():
            raise ValueError('--run 必须是已存在的 root-relative raw/profile-... 目录')
        if self.output.parent != self.root / 'reports' or not self.output.parent.is_dir():
            raise ValueError('--output 必须是已存在 reports 下的新直接子目录')
        if self.output.exists():
            raise FileExistsError(f'禁止覆盖: {self.output}')
        self.issues = []
        self.artifacts = {}
        self.hash_checks = []
        self.used_pass_paths = set()

    def resolve(self, relative):
        path = Path(relative)
        if path.is_absolute() or '..' in path.parts or not path.parts:
            raise ValueError(f'需要安全的 root-relative 路径: {relative!r}')
        result = self.root / path
        if not result.resolve().is_relative_to(self.root):
            raise ValueError(f'路径越出输入 root: {relative!r}')
        return result

    def rel(self, path):
        return str(path.relative_to(self.root))

    def library_reference(self, reference, producer_roots):
        # build_mus records absolute library paths; build_qm uses relpath,
        # sometimes outside ROOT. Never follow an external producer-host path
        # on this host. Only rebase a declared path inside its recorded ROOT.
        path = Path(reference)
        if not path.is_absolute():
            return self.resolve(reference)
        for root in [self.root] + sorted(producer_roots, key=lambda p: len(p.parts), reverse=True):
            if path.is_relative_to(root):
                return self.resolve(path.relative_to(root))
        raise ValueError(f'外部 DSO 未包含在输入 root 内，身份哈希覆盖未完成（未访问外部路径）: {reference}')

    def issue(self, code, message, scope='run', warning=False):
        self.issues.append({'severity': 'warning' if warning else 'error',
                            'scope': scope, 'code': code, 'message': str(message)})

    def artifact(self, path, expected=None, role='raw'):
        relative = self.rel(path)
        if relative not in self.artifacts:
            if not path.resolve().is_relative_to(self.root):
                raise ValueError(f'归档符号链接越界: {relative}')
            item = {'path': relative, 'role': role, 'exists': path.is_file(),
                    'sha256': None, 'bytes': None}
            if item['exists']:
                item.update(sha256=sha(path), bytes=path.stat().st_size)
            self.artifacts[relative] = item
        item = self.artifacts[relative]
        if expected is not None:
            check = {'path': relative, 'expected': expected, 'actual': item['sha256'],
                     'match': bool(re.fullmatch(r'[0-9a-f]{64}', str(expected))) and expected == item['sha256']}
            self.hash_checks.append(check)
            if not check['match']:
                self.issue('hash_mismatch', f'{relative}: 预期 {expected}，实际 {item["sha256"]}')
        return item

    def load(self, path):
        self.artifact(path)
        with path.open(encoding='utf-8') as stream:
            result = json.load(stream)
        return result

    def text(self, path):
        self.artifact(path)
        return path.read_text(encoding='utf-8')

    def require(self, condition, code, message, scope='run'):
        if not condition:
            self.issue(code, message, scope)
        return condition

    def read_build(self, manifest):
        try:
            path = self.resolve(manifest['build_manifest'])
            self.require(bool(manifest.get('build_sha256')), 'build_hash_missing', '缺少 build_sha256')
            self.artifact(path, manifest.get('build_sha256'), 'build_manifest')
            build = self.load(path)
        except (OSError, ValueError, KeyError, TypeError) as exc:
            self.issue('build_manifest', exc)
            return {}, {}
        self.require(build.get('status') == 'reference_build_complete', 'build_status', 'build 未完成验证')
        entries = build.get('entries', [])
        index = {}
        for entry in entries:
            key = (str(entry.get('algorithm', '')).lower(), entry.get('parameter'))
            if key in index:
                self.issue('duplicate_build_entry', str(key))
            index[key] = entry
            self.require(entry.get('status') == 'validated' and entry.get('sanity', {}).get('status') == 'PASS',
                         'build_entry_status', f'{key}: 缺少 validated/sanity PASS')
        expected = {(a, p) for a, parameters in ALGORITHMS.items() for p in parameters}
        self.require(len(entries) == 20 and set(index) == expected, 'build_inventory',
                     f'build 必须覆盖固定五算法 20 参数；missing={sorted(expected-set(index))}; unexpected={sorted(set(index)-expected)}')
        source_files = build.get('source_files', {})
        self.require(bool(source_files), 'source_hash_coverage', '缺少 source_files 哈希清单')
        checks = list(source_files.items())
        if build.get('source_lock_sha256'):
            checks.append(('source-lock.json', build['source_lock_sha256']))
        else:
            self.issue('source_lock_hash', '缺少 source_lock_sha256')
        producer_roots = {Path(command['directory']) for entry in entries
                          for command in entry.get('compile_commands', [])
                          if command.get('directory') and Path(command['directory']).is_absolute()}
        for entry in entries:
            self.require(bool(entry.get('sha256')), 'binary_hash_coverage', f'{entry.get("binary")}: 缺少预期哈希')
            sources = entry.get('sources', [])
            self.require(bool(sources) and set(sources).issubset(source_files), 'entry_source_hash_coverage',
                         f'{entry.get("parameter")}: sources 缺失或 source_files 未覆盖所有已声明源码')
            if entry.get('binary'):
                checks.append((entry['binary'], entry.get('sha256')))
            audits = entry.get('object_audit', [])
            self.require(bool(audits) and {x.get('object') for x in audits} == set(entry.get('objects', [])),
                         'object_hash_coverage', f'{entry.get("parameter")}: object_audit 不覆盖所有 objects')
            checks.extend((x['object'], x.get('sha256')) for x in audits if 'object' in x)
            for library in entry.get('library_dependencies', []):
                try:
                    library_path = self.library_reference(library, producer_roots)
                    item = self.artifact(library_path, role='library_current_hash')
                    self.require(item['exists'], 'library_missing', library)
                    identity_path = self.root / 'evidence/dependency-identity.json'
                    if identity_path.is_file() and library_path.name == 'libcrypto.so.3':
                        identity = self.load(identity_path)
                        self.require(identity.get('status') == 'PASS', 'dependency_identity_status', str(identity_path))
                        self.artifact(library_path, identity.get('runtime_sha256'), 'library_bound_to_prebuild_dependency_identity')
                    else:
                        self.issue('library_baseline_hash_unknown', f'{library}: 仅记录归档当前哈希；build schema 未提供该库的预期哈希，buildids 仅原始证据', warning=True)
                except (OSError, ValueError) as exc:
                    self.issue('library_archive', exc)
            for obj in audits:
                self.require(obj.get('vector_mnemonics') == [], 'object_isa', f'{obj.get("object")}: 未验证无向量指令')
        checks.extend((command['log'], command.get('sha256')) for command in build.get('commands', []) if 'log' in command)
        for name, expected_hash in checks:
            try:
                self.require(bool(expected_hash), 'expected_hash_missing', name)
                self.artifact(self.resolve(name), expected_hash, 'identity')
            except (OSError, ValueError) as exc:
                self.issue('identity_artifact', exc)
        return build, index

    def expected_jobs(self, manifest):
        jobs = [(f'timing-{i}', 'timing', None) for i in range(7)]
        jobs += [(f'stat-{i}', 'stat', CORE) for i in range(3)]
        capabilities = manifest.get('perf_capabilities', [])
        cap = {x.get('event'): x.get('supported') for x in capabilities}
        self.require(cap.get(CORE) is True, 'core_capability', 'core PMU capability 未明确支持')
        self.require(all(isinstance(cap.get(event), bool) for event in AUX),
                     'aux_capabilities', '缺少辅助事件探测，无法确定 stat-10/11 设计')
        selected = [event for event in AUX if cap.get(event) is True]
        jobs += [(f'stat-{10+i}', 'stat', event) for i, event in enumerate(selected)]
        jobs += [(f'sample-{i}', 'sample', 'cycles:u') for i in range(2)]
        return jobs

    def observation(self, result, case, kind, count, scope):
        before = len(self.issues)
        self.require(result.get('returncode') == 0, 'returncode', f'采集 returncode={result.get("returncode")}', scope)
        self.require(result.get('kind') == kind and result.get('operation') == case['operation'] and result.get('iterations') == count,
                     'result_contract', 'result kind/operation/iterations 与设计不符', scope)
        obs = result.get('observation')
        if not isinstance(obs, dict):
            self.issue('observation_missing', 'worker observation 缺失（权限/运行失败不补零）', scope)
            return None, False
        self.require(obs.get('status') == 'ok' and str(obs.get('algorithm', '')).lower() == case['algorithm']
                     and obs.get('parameter') == case['parameter'] and obs.get('operation') == case['operation'],
                     'worker_identity', 'worker status/identity 不符', scope)
        self.require(type(count) is int and 1 <= count <= 100000 and obs.get('iterations') == count,
                     'iterations', 'worker iterations 与冻结 design 不符', scope)
        self.require(obs.get('cpu_start') == 3 and obs.get('cpu_end') == 3,
                     'cpu_affinity', 'cpu_start/end 不是 CPU3', scope)
        self.require(obs.get('perf_gated') is (kind in ('stat', 'sample')),
                     'perf_control', 'perf_gated 与 pass 种类不符', scope)
        for field in ('wall_ns', 'thread_cpu_ns'):
            self.require(type(obs.get(field)) is int and obs[field] > 0, 'observation_time', f'{field} 缺失/非正整数', scope)
        for field in ('voluntary_switches', 'involuntary_switches', 'minor_faults', 'major_faults'):
            self.require(type(obs.get(field)) is int and obs[field] >= 0, 'context_faults', f'{field} 缺失/非法', scope)
        expected_postcheck = 0 if case['operation'] == 'verify' else count
        self.require(obs.get('postcheck_outputs') == expected_postcheck and obs.get('verification_fixture_valid') is True,
                     'postcheck', f'postcheck_outputs 应为 {expected_postcheck}，fixture 应有效', scope)
        environment = result.get('environment')
        valid_environment = isinstance(environment, dict) and isinstance(environment.get('samples'), list) and bool(environment['samples']) and all(isinstance(s, dict) for s in environment['samples'])
        self.require(valid_environment and environment.get('errors') == [], 'environment', '环境 samples 缺失/错误', scope)
        valid = not any(x['severity'] == 'error' for x in self.issues[before:])
        return obs, valid

    def resolve_same_named_symbols(self, directory, row):
        evidence_path = self.root / 'evidence/symbol-address-review/resolutions.json'
        if not evidence_path.is_file():return
        evidence = self.load(evidence_path)
        matches = [p for p in evidence['passes'] if p['path'] == self.rel(directory)]
        if not matches:return
        if len(matches) != 1:raise ParseError('duplicate symbol-resolution pass')
        patch = matches[0]
        self.artifact(directory / 'perf.data', patch['perf_sha256'], 'symbol_resolution_perf')
        self.artifact(self.resolve(evidence['binary']), evidence['binary_sha256'], 'symbol_resolution_ELF')
        self.artifact(evidence_path.parent / 'source.json', evidence['source_record_sha256'], 'symbol_resolution_source')
        original = self.load(evidence_path.parent / 'source.json')
        for item in original['files']:
            self.artifact(evidence_path.parent / item['path'], item['sha256'], 'symbol_resolution_evidence')
        for mode in ('self', 'children'):
            changes = patch['reports'][mode]
            self.artifact(directory / ('report-' + mode + '.txt'), changes['sha256'], 'original_histogram')
            for change in changes['replacements']:
                entries = [r for r in row[mode + '_table']['rows'] if r['line'] == change['line']]
                if len(entries) != 1:raise ParseError('symbol-resolution original row missing')
                entry = entries[0]
                for field in ('dso', 'samples', 'period', 'children_pct'):
                    if entry.get(field) != change.get(field):raise ParseError('symbol-resolution numerical row changed')
                if entry['symbol'] != change['original_symbol']:raise ParseError('symbol-resolution original name changed')
                entry['symbol'] = change['symbol'];entry['symbol_instance_evidence'] = change
        self.issue('static_symbol_instances_resolved', '同名静态函数按已保存ELF与mmap地址区分；原始数值未改，children不直接相加', row['scope'], True)

    def sample(self, directory, row):
        scope = row['scope']
        data = self.artifact(directory / 'perf.data', role='perf.data')
        row['record'] = dict(data)
        self.require(data['exists'] and bool(data['bytes']), 'perf_data_missing', 'perf.data 缺失或为空', scope)
        texts = {}
        commands = self.load(directory / 'report-commands.json')
        for name in EXPORTS:
            found = [command for command in commands if command.get('output') == name]
            self.require(len(found) == 1 and found[0].get('returncode') == 0,
                         'perf_export', f'{name}: export 命令缺失/失败', scope)
            texts[self.rel(directory / name)] = self.text(directory / name)
            self.text(directory / (name + '.stderr'))
        events = texts[self.rel(directory / 'events.txt')]
        row['sample_safety'] = sample_safety(events, self.rel(directory / 'events.txt'))
        for command in commands:
            if command.get('output') == 'annotate-top.txt':
                self.text(directory / 'annotate-top.txt')
                self.text(directory / 'annotate-top.txt.stderr')
                if command.get('returncode') != 0:
                    self.issue('annotate_failed', 'annotate-top 导出失败，非全源码覆盖', scope, True)
        if not any(command.get('output') == 'annotate-top.txt' for command in commands):
            self.issue('annotate_missing', '没有 annotate-top 导出（可能没有已解析符号）', scope, True)
        for path in sorted(directory.iterdir()):
            if path.is_file() and (path.name.endswith('.stderr') or path.name == 'stderr.txt'):
                texts[self.rel(path)] = self.text(path)
        row['loss'] = loss_evidence(texts)
        if row['loss']['total_lost_samples'] is None:
            self.issue('loss_unknown', '无可靠一致 total lost samples；不能默认 0', scope, True)
        if row['loss']['unparsed_loss_lines']:
            self.issue('loss_unparsed', '存在格式未知的 loss 字段，已保留原文，不能推断无丢样', scope, True)
        if any(x['count'] > 0 for x in row['loss']['evidence']):
            self.issue('samples_lost', '检测到丢样；见 loss evidence，未对重复来源求和', scope, True)
        row['self_table'] = parse_report(texts[self.rel(directory / 'report-self.txt')], 'self', self.rel(directory / 'report-self.txt'))
        row['samples'] = sum(x['samples'] for x in row['self_table']['rows'])
        row['minimum_sample_warning'] = row['samples'] < 1000
        if row['samples'] < 1000:
            self.issue('low_samples', f'{row["samples"]} samples < 1000，不能据此断言热点排名稳定', scope, True)
        row['children_table'] = parse_report(texts[self.rel(directory / 'report-children.txt')], 'children', self.rel(directory / 'report-children.txt'))
        self.resolve_same_named_symbols(directory, row)
        # Inclusive duplicates without an address-resolved identity remain errors.
        aggregate_children([(row['children_table'], row['self_table']['total_period'])])

    def pass_row(self, case, name, kind, event, count, entry, directory):
        scope = case['id'] + '/' + name
        row = {'name': name, 'kind': kind, 'event': event, 'iterations_design': count,
               'path': self.rel(directory), 'scope': scope, 'status': 'incomplete',
               'observation': None, 'metrics': {}}
        before = len(self.issues)
        try:
            if name != 'calibration':
                if entry is None:
                    self.issue('pass_missing', 'manifest 缺少 pass；磁盘残留仍可审计但不算完成', scope)
                else:
                    actual = self.resolve(entry['path'])
                    self.require(actual == directory, 'pass_location', f'pass path 应为 {self.rel(directory)}，实际 {entry["path"]}', scope)
                    if actual in self.used_pass_paths:
                        self.issue('duplicate_pass_path', entry['path'], scope)
                    self.used_pass_paths.add(actual)
                    self.require(entry.get('kind') == kind and entry.get('event') == event and entry.get('iterations') == count and entry.get('returncode') == 0,
                                 'pass_manifest_contract', 'manifest pass kind/event/count/returncode 不符', scope)
            result = self.load(directory / 'result.json')
            row['result_path'] = self.rel(directory / 'result.json')
            row['returncode'] = result.get('returncode')
            row['whole_process_wall_ns_not_baseline'] = result.get('whole_process_wall_ns')
            row['argv'] = result.get('argv')
            row['observation'], row['observation_valid'] = self.observation(result, case, kind, count, scope)
            row['environment'] = environment_summary(result.get('environment') or {})
            self.require(result.get('events') == event, 'result_events', 'result.events 与 pass 设计不符', scope)
            self.text(directory / 'stdout.txt')
            self.text(directory / 'stderr.txt')
            if kind == 'stat':
                row['stat_events'] = parse_stat(self.text(directory / 'perf-stat.txt'), self.rel(directory / 'perf-stat.txt'))
                if row['observation_valid']:
                    row['metrics'] = stat_metrics(row['stat_events'], count)
                wanted = event.strip('{}').split(',')
                for wanted_event in wanted:
                    found = [x for x in row['stat_events'] if x['event_id'] == wanted_event]
                    self.require(len(found) == 1 and found[0]['nonmultiplexed'], 'stat_event_unusable',
                                 f'{wanted_event}: 缺失/重复/未计数/不支持/非 100% running/runtime unknown', scope)
                ratio_name = 'ipc' if event == CORE else 'branch_miss_rate' if event == AUX[0] else 'l1d_load_miss_rate'
                self.require(row['metrics'].get(ratio_name) is not None, 'stat_ratio_unavailable', f'{ratio_name}: 同 pass 分母/runtime 不一致或不可用', scope)
            elif kind == 'sample':
                self.sample(directory, row)
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
            self.issue('precise_parse_error' if isinstance(exc, ParseError) else 'pass_read_error', exc, scope)
        row['issues'] = self.issues[before:]
        if not any(x['severity'] == 'error' for x in row['issues']):
            row['status'] = 'complete'
        return row

    def case(self, key, entry, build_entry, jobs):
        algorithm, parameter, operation = key
        case = {'id': f'{algorithm}-{parameter}/{operation}', 'algorithm': algorithm,
                'parameter': parameter, 'operation': operation, 'status': 'incomplete',
                'passes': [], 'hotspots_self': [], 'hotspots_children': [], 'dso': [], 'categories': []}
        before = len(self.issues)
        if entry is None:
            self.issue('case_missing', 'run manifest 未包含该固定设计 case', case['id'])
            # Still emit the complete list of missing per-pass performance rows.
            entry = {}
        case['manifest_entry'] = entry
        case['metadata'] = entry.get('metadata')
        case['fixture_commitment'] = entry.get('same_fixture_commitment_sha256')
        self.require(entry.get('status') == 'complete', 'case_status', f'case status={entry.get("status")}', case['id'])
        self.require(bool(re.fullmatch(r'[0-9a-f]{64}', str(case['fixture_commitment']))),
                     'fixture_commitment', '缺少有效 fixture SHA-256 commitment', case['id'])
        metadata = entry.get('metadata') or {}
        self.require(str(metadata.get('algorithm', '')).lower() == algorithm and metadata.get('parameter') == parameter
                     and metadata.get('message_bytes') == 32 and metadata.get('api') == 'signed-message'
                     and bool(metadata.get('implementation'))
                     and all(type(metadata.get(k)) is int and metadata[k] > 0 for k in ('public_bytes', 'secret_bytes', 'signature_max_bytes')),
                     'case_metadata', 'metadata 身份/implementation/API/尺寸不完整', case['id'])
        self.require(bool(build_entry) and entry.get('binary') == build_entry.get('binary')
                     and entry.get('binary_sha256') == build_entry.get('sha256') and metadata == build_entry.get('metadata'),
                     'case_build_identity', 'case binary/hash/metadata 与 build 不符', case['id'])
        design = entry.get('design') or {}
        case['design'] = design
        self.require(design.get('timing_trials') == 7 and design.get('core_stat_trials') == 3 and design.get('record_passes') == 2
                     and design.get('frozen_before_repetitions') is True and design.get('max_retained_output_bytes') == 134217728,
                     'case_design', 'design 必须冻结 7 timing/3 core stat/2 sample，输出保留上限 128MiB', case['id'])
        n, ns = design.get('baseline_and_stat_iterations'), design.get('record_iterations')
        counts_valid = self.require(all(type(x) is int and 1 <= x <= 100000 for x in (n, ns)),
                                    'design_counts', 'design iterations 无效', case['id'])
        calibration = design.get('calibration_seconds_per_op')
        self.require(type(calibration) in (float, int) and math.isfinite(calibration) and calibration > 0,
                     'design_calibration', '缺少有效 calibration_seconds_per_op', case['id'])
        if counts_valid and all(type(metadata.get(k)) is int and metadata[k] > 0 for k in ('public_bytes', 'secret_bytes', 'signature_max_bytes')):
            stride = metadata['signature_max_bytes'] + 32 if operation == 'sign' else metadata['public_bytes'] + metadata['secret_bytes'] if operation == 'keygen' else 1
            case['retained_stride_bytes'] = stride
            self.require(max(n, ns) * stride <= 134217728, 'retention_cap',
                         '冻结 iterations 超出 worker 保留输出 128MiB 上限', case['id'])
        # Directory spelling uses the ORIGINAL manifest family case, not lowercase.
        family = str(entry.get('algorithm', algorithm)) + '-' + parameter
        directory = self.run / family / operation
        case['raw_directory'] = self.rel(directory)
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', family):
            self.issue('case_path', '非法 family 路径', case['id'])
            directory = self.run / (algorithm + '-' + parameter) / operation
        try:
            disk_design = self.load(directory / 'design.json')
            self.require(disk_design == design, 'design_disk_mismatch', 'design.json 与 manifest.design 不一致', case['id'])
        except (OSError, ValueError) as exc:
            self.issue('design_missing', exc, case['id'])
        pass_index = {}
        for item in entry.get('passes', []):
            name = Path(item.get('path', '')).name
            if name in pass_index:
                self.issue('duplicate_pass', name, case['id'])
            pass_index[name] = item
        expected_names = {job[0] for job in jobs}
        self.require(set(pass_index) == expected_names, 'pass_inventory',
                     f'missing={sorted(expected_names-set(pass_index))}; unexpected={sorted(set(pass_index)-expected_names)}', case['id'])
        case['passes'].append(self.pass_row(case, 'calibration', 'timing', None, 1, None, directory / 'calibration'))
        for name, kind, event in jobs:
            case['passes'].append(self.pass_row(case, name, kind, event, ns if kind == 'sample' else n,
                                               pass_index.get(name), directory / name))
        for kind in ('wall', 'thread_cpu'):
            values = [row['observation'][kind + '_ns'] / row['observation']['iterations']
                      for row in case['passes'] if row['name'].startswith('timing-') and row.get('observation_valid')]
            case['baseline_' + kind + '_ns_per_op'] = dict(stats(values), trials=values, expected_trials=7)
        baseline = case['baseline_wall_ns_per_op']['median']
        for row in case['passes']:
            if row.get('observation_valid'):
                obs = row['observation']
                row['wall_ns_per_op'] = obs['wall_ns'] / obs['iterations']
                row['thread_cpu_ns_per_op'] = obs['thread_cpu_ns'] / obs['iterations']
                if row['kind'] == 'sample' and baseline:
                    row['sampling_overhead_observation'] = {
                        'baseline_median_ns_per_op': baseline, 'profile_ns_per_op': row['wall_ns_per_op'],
                        'ratio': row['wall_ns_per_op'] / baseline,
                        'delta_ns_per_op': row['wall_ns_per_op'] - baseline,
                        'iterations_differ': ns != n, 'causal_correction': False}
        core_rows = [row for row in case['passes'] if row['name'] in ('stat-0', 'stat-1', 'stat-2')]
        case['core_stat_summary'] = {metric: stats([row['metrics'][metric] for row in core_rows if row['metrics'].get(metric) is not None])
                                     for metric in ('cycles_per_op', 'instructions_per_op', 'ipc')}
        self_tables = [row['self_table'] for row in case['passes'] if 'self_table' in row and row.get('observation_valid')
                       and row.get('sample_safety', {}).get('status') == 'checked_safe_sample_type']
        child_pairs = [(row['children_table'], row['self_table']['total_period']) for row in case['passes']
                       if 'children_table' in row and 'self_table' in row and row.get('observation_valid')
                       and row.get('sample_safety', {}).get('status') == 'checked_safe_sample_type']
        case['hotspots_self'] = aggregate_self(self_tables)
        try:
            case['hotspots_children'] = aggregate_children(child_pairs)
        except ParseError as exc:
            self.issue('precise_parse_error', exc, case['id'])
        case['sample_passes_aggregated'] = len(self_tables)
        case['children_passes_aggregated'] = len(child_pairs)
        case['sample_count'] = sum(row['samples'] for row in case['hotspots_self']) if self_tables else None
        case['period_denominator'] = sum(table['total_period'] for table in self_tables) if self_tables else None
        for field, output in (('dso', 'dso'), ('category', 'categories')):
            grouped = {}
            for row in case['hotspots_self']:
                group = grouped.setdefault(row[field], {field: row[field], 'period': 0, 'samples': 0})
                group['period'] += row['period']
                group['samples'] += row['samples']
            for group in grouped.values():
                group['self_pct'] = 100 * group['period'] / case['period_denominator']
            case[output] = sorted(grouped.values(), key=lambda x: -x['period'])
        case['issues'] = self.issues[before:]
        if not any(x['severity'] == 'error' for x in case['issues']):
            case['status'] = 'complete'
        return case

    def analyze(self):
        manifest = self.load(self.run / 'manifest.json')
        self.require(manifest.get('status') == 'complete' and manifest.get('development') is False,
                     'run_status', 'run 未 complete 或属于 smoke/development')
        self.require(manifest.get('errors') == [], 'run_errors', f'采集 errors={manifest.get("errors")}')
        protocol = manifest.get('protocol') or {}
        expected_protocol = {'parameter_count': 20, 'timing_trials': 7, 'core_stat_trials': 3,
                             'sample_passes': 2, 'worker_cpu': 3, 'observer_cpu': 7,
                             'host_alias': 'REDACTED_HOST', 'message_bytes': 32}
        self.require(all(protocol.get(k) == v for k, v in expected_protocol.items()) and protocol.get('operations') == list(OPERATIONS),
                     'protocol_design', 'protocol 不满足固定 212/20参数/60case 设计')
        try:
            self.require(self.load(self.root / 'protocol.json') == protocol, 'protocol_mismatch', '归档 protocol.json 与嵌入 protocol 不一致')
        except (OSError, ValueError) as exc:
            self.issue('protocol_missing', exc)
        host = manifest.get('host') or {}
        self.require(host.get('architecture') == 'riscv64' and bool(host.get('kernel')) and {3, 7}.issubset(set(host.get('affinity', []))),
                     'host_metadata', '缺少 RISC-V/kernel/CPU3+7 host metadata')
        build, build_index = self.read_build(manifest)
        jobs = self.expected_jobs(manifest)
        expected = {(a, p, op) for a, parameters in ALGORITHMS.items() for p in parameters for op in OPERATIONS}
        entries = manifest.get('cases', [])
        index = {}
        for entry in entries:
            key = (str(entry.get('algorithm', '')).lower(), entry.get('parameter'), entry.get('operation'))
            if key in index:
                self.issue('duplicate_case', str(key))
            index[key] = entry
        observed_parameters = {(key[0], key[1]) for key in index}
        observed_algorithms = {key[0] for key in index}
        coverage = {
            'five_algorithms': observed_algorithms == set(ALGORITHMS),
            'twenty_parameters': observed_parameters == {(a, p) for a, ps in ALGORITHMS.items() for p in ps},
            'sixty_unique_cases': len(entries) == 60 and len(index) == 60 and set(index) == expected,
            'expected_case_count': 60, 'manifest_case_count': len(entries), 'unique_case_count': len(index),
            'missing_cases': [list(key) for key in sorted(expected-set(index))],
            'unexpected_cases': [list(key) for key in sorted(set(index)-expected)],
        }
        for gate in ('five_algorithms', 'twenty_parameters', 'sixty_unique_cases'):
            self.require(coverage[gate], 'coverage_' + gate, '独立完整性门槛未满足: ' + gate)
        cases = [self.case(key, index.get(key), build_index.get(key[:2], {}), jobs) for key in sorted(expected)]
        for algorithm, parameters in ALGORITHMS.items():
            for parameter in parameters:
                family = [case for case in cases if (case['algorithm'], case['parameter']) == (algorithm, parameter)]
                if len({case['fixture_commitment'] for case in family}) != 1:
                    self.issue('fixture_coherency', f'{algorithm}/{parameter}: 三操作 fixture commitment 不一致')
                    for case in family:
                        case['status'] = 'incomplete'
        # Inventory includes orphan/failed exports and every raw text, not just top N.
        for path in sorted(self.run.rglob('*')):
            if path.is_file() or path.is_symlink():
                try:
                    self.artifact(path)
                except (OSError, ValueError) as exc:
                    self.issue('archive_inventory', exc)
        errors = [issue for issue in self.issues if issue['severity'] == 'error']
        coverage['complete_cases'] = sum(case['status'] == 'complete' for case in cases)
        coverage['all_cases_complete'] = coverage['complete_cases'] == 60
        coverage['hash_checks'] = len(self.hash_checks)
        coverage['hash_matches'] = sum(check['match'] for check in self.hash_checks)
        coverage['sample_records'] = sum('record' in row and row['record']['exists'] for case in cases for row in case['passes'])
        coverage['safe_sample_records'] = sum(row.get('sample_safety', {}).get('status') == 'checked_safe_sample_type' for case in cases for row in case['passes'])
        all_passes = [row for case in cases for row in case['passes']]
        coverage['expected_pass_rows_including_calibration'] = 60 * (1 + len(jobs))
        coverage['complete_pass_rows'] = sum(row['status'] == 'complete' for row in all_passes)
        coverage['valid_observations'] = sum(bool(row.get('observation_valid')) for row in all_passes)
        coverage['valid_controlled_perf_observations'] = sum(bool(row.get('observation_valid')) and row['kind'] in ('stat', 'sample') for row in all_passes)
        coverage['expected_controlled_perf_observations'] = 60 * sum(kind in ('stat', 'sample') for _, kind, _ in jobs)
        coverage['valid_baseline_trials'] = sum(case['baseline_wall_ns_per_op']['n'] for case in cases)
        coverage['expected_baseline_trials'] = 420
        coverage['sample_records_at_least_1000'] = sum(row.get('samples', 0) >= 1000 for row in all_passes)
        coverage['metadata_valid_cases'] = sum(not any(issue['code'] in ('case_metadata', 'case_build_identity') for issue in case['issues']) for case in cases)
        incomplete_cases = [{'case': case['id'], 'issues': case['issues']} for case in cases if case['status'] != 'complete']
        incomplete_rows = [{'case': case['id'], 'pass': row['name'], 'path': row['path'], 'issues': row['issues']}
                           for case in cases for row in case['passes'] if row['status'] != 'complete']
        return {'schema': 'r20-reference-report-v1', 'created_utc': datetime.now(timezone.utc).isoformat(),
                'status': 'incomplete' if errors or not coverage['all_cases_complete'] else 'complete',
                'quality': 'incomplete' if errors else 'warnings' if any(x['severity'] == 'warning' for x in self.issues) else 'no_reported_warnings',
                'rootPath': str(self.root), 'run': self.rel(self.run), 'output': self.rel(self.output),
                'coverage': coverage, 'run_manifest': manifest, 'build_manifest': build,
                'category_mapping': {'priority': ['unknown'] + [name for name, _ in CATEGORY_RULES] + ['other'],
                                     'regex_case_insensitive': dict(CATEGORY_RULES), 'dso_shortcut': False,
                                     'unknown_rule': '缺失/unknown DSO 或缺失/unknown/十六进制地址 symbol；始终在分母内'},
                'issues': self.issues, 'incomplete_rows': incomplete_rows, 'incomplete_cases': incomplete_cases,
                'aux_availability': [{'event': event, 'supported': next((probe.get('supported') for probe in manifest.get('perf_capabilities', []) if probe.get('event') == event), None)} for event in AUX],
                'hash_checks': self.hash_checks, 'artifacts': list(self.artifacts.values()), 'cases': cases}

    def link(self, path, base=None):
        base = base or self.output
        relative = os.path.relpath(self.root / path, base)
        return '[' + md(path) + '](' + quote(relative, safe='/.-_') + ')'

    def artifact_lines(self, artifacts, base):
        return ['| 原始文件 | bytes | SHA-256 |', '|---|---:|---|'] + [
            f'| {self.link(item["path"], base)} | {display(item["bytes"])} | {item["sha256"] or "missing"} |'
            for item in artifacts]

    def case_markdown(self, case, base, artifacts):
        metadata = case['metadata'] or {}
        design = case['design']
        lines = [f'# {case["id"]}', '', f'状态：**{case["status"]}**；self 合并 {case["sample_passes_aggregated"]}/2 pass，children 合并 {case["children_passes_aggregated"]}/2 pass。',
                 '', f'总 samples：{display(case["sample_count"])}；self period 分母：{display(case["period_denominator"])}。',
                 '', '## 身份与冻结设计', '',
                 f'- implementation：{md(display(metadata.get("implementation")))}；API：{md(display(metadata.get("api")))}。',
                 f'- public/secret/signature max/message bytes：{display(metadata.get("public_bytes"))}/{display(metadata.get("secret_bytes"))}/{display(metadata.get("signature_max_bytes"))}/{display(metadata.get("message_bytes"))}。',
                 f'- binary：{md(display(case["manifest_entry"].get("binary")))}；SHA-256：{display(case["manifest_entry"].get("binary_sha256"))}。',
                 f'- 单 fixture commitment：{display(case["fixture_commitment"])}（不归档私钥/签名输出）。',
                 f'- baseline/stat iterations：{display(design.get("baseline_and_stat_iterations"))}；sample iterations：{display(design.get("record_iterations"))}；frozen：{display(design.get("frozen_before_repetitions"))}。',
                 '', '## Baseline（只用 timing trial）', '', '| 时钟 ns/op | trials | median | min | max | CV |', '|---|---:|---:|---:|---:|---:|']
        for kind in ('wall', 'thread_cpu'):
            value = case['baseline_' + kind + '_ns_per_op']
            lines.append('| ' + kind + ' | ' + ' | '.join(display(value[x]) for x in ('n', 'median', 'min', 'max', 'cv')) + ' |')
        lines += ['', '## 全 pass 观测（unknown 不代表 0）', '',
                  '| pass | 状态 | iterations | wall ns/op | thread CPU ns/op | postcheck | gated | CPU start/end | voluntary/involuntary | minor/major | 环境 samples |',
                  '|---|---|---:|---:|---:|---:|---|---|---|---|---:|']
        for row in case['passes']:
            obs = row.get('observation') or {}
            lines.append('| ' + ' | '.join(md(x) for x in (
                row['name'], row['status'], display(obs.get('iterations')), display(row.get('wall_ns_per_op')),
                display(row.get('thread_cpu_ns_per_op')), display(obs.get('postcheck_outputs')), display(obs.get('perf_gated')),
                f'{display(obs.get("cpu_start"))}/{display(obs.get("cpu_end"))}',
                f'{display(obs.get("voluntary_switches"))}/{display(obs.get("involuntary_switches"))}',
                f'{display(obs.get("minor_faults"))}/{display(obs.get("major_faults"))}', display(row.get('environment', {}).get('sample_count')))) + ' |')
        lines += ['', '## perf-stat 同 pass 指标', '', '| pass | cycles/op | instructions/op | IPC | branch miss rate | L1D load miss rate |', '|---|---:|---:|---:|---:|---:|']
        for row in case['passes']:
            if row['kind'] == 'stat':
                lines.append('| ' + row['name'] + ' | ' + ' | '.join(display(row['metrics'].get(key)) for key in ('cycles_per_op', 'instructions_per_op', 'ipc', 'branch_miss_rate', 'l1d_load_miss_rate')) + ' |')
        lines += ['', '## Sampling overhead 观测（不作因果扣除）', '']
        for row in case['passes']:
            if row['kind'] == 'sample':
                overhead = row.get('sampling_overhead_observation', {})
                loss = row.get('loss', {})
                lines.append(f'- {row["name"]}: profile ns/op={display(overhead.get("profile_ns_per_op"))}；baseline median ns/op={display(overhead.get("baseline_median_ns_per_op"))}；比值={display(overhead.get("ratio"))}；iterations 不同={display(overhead.get("iterations_differ"))}；samples={display(row.get("samples"))}；lost samples={display(loss.get("total_lost_samples"))}；callchain/full source=unknown。')
        lines += ['', '## Self Top20（全分母，非 wall time）', '', '| # | DSO | 函数 | 分类 | samples | period | self % | 累计 % |', '|---:|---|---|---|---:|---:|---:|---:|']
        for i, row in enumerate(case['hotspots_self'][:20], 1):
            lines.append('| ' + ' | '.join(md(display(value)) for value in (i, row['dso'], row['symbol'], row['category'], row['samples'], row['period'], row['self_pct'], row['cumulative_pct'])) + ' |')
        if not case['hotspots_self']:
            lines.append('\n没有可验证的 self 全表；禁止补零。')
        lines += ['', '全部函数（不截断）：[self CSV](hotspots-self.csv)、[children CSV](hotspots-children.csv)；children 为重叠 inclusive，不能加总 100%。',
                  '', 'DSO 与互斥分类：[DSO.csv](DSO.csv)、[categories.csv](categories.csv)。完整数字、未知列、原始行及位置在根 summary.json。',
                  '', '## 缺失、错误与质量警告', '']
        lines += [f'- {issue["severity"]} {md(issue["scope"])} / {issue["code"]}: {md(issue["message"])}' for issue in case['issues']] or ['无 case 级错误；仍须查看全局身份/哈希门槛。']
        lines += ['', '## 原始文件全集（含失败/额外导出）', ''] + self.artifact_lines(artifacts, base)
        lines += ['', NOTES]
        return '\n'.join(lines) + '\n'

    def write(self, summary, allow_partial=False):
        summary['allow_partial'] = allow_partial
        if summary['status'] != 'complete' and not allow_partial:
            failures = [issue for issue in summary['issues'] if issue['severity'] == 'error']
            first = '; '.join(issue['scope'] + ': ' + issue['code'] + ' ' + issue['message'] for issue in failures[:8])
            raise IncompleteError(f'{len(failures)} 个错误，{len(summary["incomplete_rows"])} 个未完成 pass；严格模式不写输出。需明确 --allow-partial。{first}')
        self.output.mkdir(exist_ok=False)
        case_root = self.output / 'cases'
        case_root.mkdir()
        performance = []
        for case in summary['cases']:
            base = case_root / case['id'].replace('/', '--')
            base.mkdir()
            case['report'] = str((base / 'REPORT.md').relative_to(self.output))
            artifacts = [item for item in summary['artifacts'] if item['path'].startswith(case['raw_directory'] + '/')]
            self.write_csv(base / 'hotspots-self.csv', case['hotspots_self'],
                           ('dso', 'symbol', 'category', 'samples', 'period', 'self_pct', 'cumulative_pct', 'evidence'))
            self.write_csv(base / 'hotspots-children.csv', case['hotspots_children'],
                           ('dso', 'symbol', 'children_pct', 'denominator_period', 'evidence'))
            self.write_csv(base / 'DSO.csv', case['dso'], ('dso', 'samples', 'period', 'self_pct'))
            self.write_csv(base / 'categories.csv', case['categories'], ('category', 'samples', 'period', 'self_pct'))
            (base / 'REPORT.md').write_text(self.case_markdown(case, base, artifacts), encoding='utf-8')
            for row in case['passes']:
                obs = row.get('observation') or {}
                record = {'algorithm': case['algorithm'], 'parameter': case['parameter'], 'operation': case['operation'],
                          'case_status': case['status'], 'pass': row['name'], 'status': row['status'], 'path': row['path'],
                          'iterations_design': row['iterations_design'], 'iterations': obs.get('iterations'),
                          'wall_ns': obs.get('wall_ns'), 'thread_cpu_ns': obs.get('thread_cpu_ns'),
                          'wall_ns_per_op': row.get('wall_ns_per_op'), 'thread_cpu_ns_per_op': row.get('thread_cpu_ns_per_op'),
                          'postcheck_outputs': obs.get('postcheck_outputs'), 'perf_gated': obs.get('perf_gated'),
                          'cpu_start': obs.get('cpu_start'), 'cpu_end': obs.get('cpu_end'),
                          'voluntary_switches': obs.get('voluntary_switches'), 'involuntary_switches': obs.get('involuntary_switches'),
                          'minor_faults': obs.get('minor_faults'), 'major_faults': obs.get('major_faults'),
                          'environment_sample_count': row.get('environment', {}).get('sample_count'),
                          'samples': row.get('samples'), 'lost_samples': row.get('loss', {}).get('total_lost_samples'),
                          'cycles_per_op': row['metrics'].get('cycles_per_op'), 'instructions_per_op': row['metrics'].get('instructions_per_op'),
                          'ipc': row['metrics'].get('ipc'), 'branch_miss_rate': row['metrics'].get('branch_miss_rate'),
                          'l1d_load_miss_rate': row['metrics'].get('l1d_load_miss_rate'),
                          'sample_baseline_ratio': row.get('sampling_overhead_observation', {}).get('ratio'),
                          'issues': row['issues']}
                for kind in ('wall', 'thread_cpu'):
                    for metric in ('n', 'median', 'min', 'max', 'cv'):
                        record[f'baseline_{kind}_{metric}'] = case['baseline_' + kind + '_ns_per_op'][metric]
                performance.append(record)
        self.write_csv(self.output / 'performance.csv', performance, tuple(performance[0]))
        lines = ['# R20：212 原始 C reference 性能与全热点报告', '',
                 f'状态：**{summary["status"]}**；质量：**{summary["quality"]}**；允许部分输出：{allow_partial}。',
                 '', '只有全部独立门槛满足才标记 complete；缺失 case、失败 pass、身份或哈希错误均不能整体报绿。',
                 '', f'输入：{self.link(self.rel(self.run / "manifest.json"))}',
                 '', '## 独立覆盖门槛', '']
        lines += [f'- {key}: {value}' for key, value in summary['coverage'].items()]
        policy_path = self.root / 'evidence/user-performance-priority.json'
        if policy_path.is_file():
            policy = json.loads(policy_path.read_text());summary['validation_policy'] = policy
            lines += ['', '## 用户确认的验证口径与已知限制', '',
                      '- 本次以本地自签验签及负例拒绝作为性能采集门槛，KAT不再阻塞；本报告不是标准一致性认证。',
                      '- 已记录的KAT情况：MAYO四组通过；SNOVA-I-B-AES首个官方归档KAT签名未通过当前reference构建，原因未继续调查；本地自签验签通过。该差异不隐藏。',
                      f'- 完整决定与原始状态：{self.link("evidence/user-performance-priority.json")}。']
        lines += ['', '## 60 case 索引与计时', '', '| case | 状态 | baseline wall median ns/op | samples | 详细报告 |', '|---|---|---:|---:|---|']
        for case in summary['cases']:
            lines.append(f'| {case["id"]} | {case["status"]} | {display(case["baseline_wall_ns_per_op"]["median"])} | {display(case["sample_count"])} | [完整 case / Top20]({quote(case["report"], safe="/.-_")}) |')
        lines += ['', '机器可读全量：[summary.json](summary.json)；全部 trial/失败行：[performance.csv](performance.csv)。', '', NOTES,
                  '', '## 互斥分类映射（本工具启发式，非人工源码证明）', '',
                  '优先序：unknown → RNG → memory → hashAES → Algebra → other；按 symbol 忽略大小写匹配，第一个匹配获唯一分类。DSO 不作粗粒度 shortcut；libcrypto 等仍计全分母。unknown 规则：缺失/unknown DSO 或缺失/unknown/十六进制地址 symbol。', '']
        lines += [f'- {name}: `{pattern}`' for name, pattern in CATEGORY_RULES]
        lines += ['', '## 全部未完成 rows', '', '| case | pass | 原始路径 | 原因 |', '|---|---|---|---|']
        for row in summary['incomplete_rows']:
            reasons = '; '.join(issue['code'] + ': ' + issue['message'] for issue in row['issues'] if issue['severity'] == 'error')
            lines.append(f'| {md(row["case"])} | {row["pass"]} | {self.link(row["path"])} | {md(reasons)} |')
        lines += ['', '## 全局问题及全部质量警告', '']
        lines += [f'- {issue["severity"]} {md(issue["scope"])} / {issue["code"]}: {md(issue["message"])}'
                  for issue in summary['issues'] if issue['scope'] == 'run' or issue['severity'] == 'warning']
        lines += ['', '## 原始归档全集及当前哈希', '', '预期哈希对照结果完整保存于 summary.json/hash_checks。', '']
        lines += self.artifact_lines(summary['artifacts'], self.output)
        (self.output / 'REPORT.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        with (self.output / 'summary.json').open('x', encoding='utf-8') as stream:
            json.dump(summary, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write('\n')
        return self.output

    @staticmethod
    def write_csv(path, rows, fields):
        with path.open('x', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore')
            writer.writeheader()
            for row in rows:
                writer.writerow({key: json.dumps(value, ensure_ascii=False, allow_nan=False) if isinstance(value, (list, dict)) else value
                                 for key, value in row.items() if key in fields})


def main(argv=None):
    parser = argparse.ArgumentParser(description='R20 离线本机归档报告器（不执行 perf/构建/联网）')
    parser.add_argument('rootPath', type=Path, help='收到的目标同布局归档根目录')
    parser.add_argument('--run', required=True, help='root-relative raw/profile-...')
    parser.add_argument('--output', required=True, help='root-relative reports/新子目录，禁止覆盖')
    parser.add_argument('--allow-partial', action='store_true', help='明确接受 incomplete 报告，缺失/失败不补零')
    args = parser.parse_args(argv)
    try:
        reporter = Reporter(args.rootPath, args.run, args.output)
        summary = reporter.analyze()
        output = reporter.write(summary, args.allow_partial)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f'报告失败：{exc}', file=sys.stderr)
        return 2
    print(json.dumps({'status': summary['status'], 'quality': summary['quality'],
                      'complete_cases': summary['coverage']['complete_cases'],
                      'incomplete_rows': len(summary['incomplete_rows']), 'output': str(output)}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
