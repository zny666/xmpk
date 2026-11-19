#!/usr/bin/env python3
"""Synthetic Python-only fixtures; no worker, compiler, perf or network calls."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('report_reference', Path(__file__).with_name('report_reference.py'))
r = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(r)

SELF0 = """# Samples: 1K of event 'cycles:u'
# Event count (approx.): 10000
# Overhead|Samples|Period|Shared Object|Symbol
70.00%|700|7000|bench|[.] gf16_mul
30.00%|300|3000|[unknown]|[unknown]
"""
SELF1 = """# Samples: 3K of event 'cycles:u'
# Event count (approx.): 30000
# Overhead|Samples|Period|Shared Object|Symbol
10.00%|300|3000|bench|[.] gf16_mul
90.00%|2700|27000|libcrypto.so.3|[.] EVP_DigestUpdate
"""
CHILD = """# Children|Self|Samples|Period|Shared Object|Symbol|Vendor column
90.00%|70.00%|700|7000|bench|[.] gf16_mul|keep-me
80.00%|0.00%|0|0|libcrypto.so.3|[.] EVP_DigestUpdate|opaque
30.00%|30.00%|300|3000|[unknown]|[unknown]|retained
"""
STAT = '2000||cycles:u|1000000|100.00||\n4000||instructions:u|1000000|100.00|2.00|insn per cycle\n'
EVENTS = 'cycles:u: type: 0, sample_type: IP|TID|TIME|CALLCHAIN|CPU|PERIOD, disabled: 1, exclude_kernel: 1\n'


def put(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value) if isinstance(value, (dict, list)) else value, encoding='utf-8')


def fixture(root):
    """Write run_reference.py's real schema, with explicitly fake measurements."""
    (root / 'reports').mkdir()
    run = root / 'raw/profile-fixture'
    run.mkdir(parents=True)
    protocol = {'parameter_count': 20, 'timing_trials': 7, 'core_stat_trials': 3,
                'sample_passes': 2, 'worker_cpu': 3, 'observer_cpu': 7,
                'host_alias': 'REDACTED_HOST', 'message_bytes': 32, 'operations': list(r.OPERATIONS)}
    put(root / 'protocol.json', protocol)
    put(root / 'source-lock.json', {'fixture_only': True})
    put(root / 'native/fixture.c', 'not real source; synthetic fixture\n')
    build = {'status': 'reference_build_complete', 'entries': [],
             'source_files': {'native/fixture.c': r.sha(root / 'native/fixture.c')},
             'source_lock_sha256': r.sha(root / 'source-lock.json'), 'commands': []}
    manifest = {'status': 'complete', 'development': False, 'errors': [], 'protocol': protocol,
                'host': {'architecture': 'riscv64', 'kernel': 'synthetic', 'affinity': [3, 7]},
                'perf_capabilities': [{'event': event, 'supported': True, 'returncode': 0, 'stdout': '', 'stderr': ''}
                                      for event in (r.CORE,) + r.AUX], 'cases': [],
                'build_manifest': 'build/fixture/manifest.json'}
    for algorithm, parameters in r.ALGORITHMS.items():
        for parameter in parameters:
            binary = f'build/fixture/{algorithm}-{parameter}/bench'
            obj = f'build/fixture/{algorithm}-{parameter}/bench.o'
            put(root / binary, 'FAKE executable: NEVER execute\n')
            put(root / obj, 'FAKE object: NEVER execute\n')
            meta = {'algorithm': algorithm.upper(), 'parameter': parameter, 'implementation': 'fixture-only',
                    'public_bytes': 32, 'secret_bytes': 32, 'signature_max_bytes': 64,
                    'message_bytes': 32, 'api': 'signed-message'}
            entry = {'algorithm': algorithm.upper(), 'parameter': parameter, 'binary': binary,
                     'sha256': r.sha(root / binary), 'metadata': meta, 'status': 'validated',
                     'sanity': {'status': 'PASS'}, 'objects': [obj], 'sources': ['native/fixture.c'],
                     'object_audit': [{'object': obj, 'sha256': r.sha(root / obj), 'vector_mnemonics': []}]}
            build['entries'].append(entry)
            for operation in r.OPERATIONS:
                directory = run / f'{algorithm.upper()}-{parameter}' / operation
                design = {'calibration_seconds_per_op': 1e-8, 'baseline_and_stat_iterations': 2,
                          'record_iterations': 4, 'max_retained_output_bytes': 134217728,
                          'timing_trials': 7, 'core_stat_trials': 3, 'record_passes': 2,
                          'frozen_before_repetitions': True}
                put(directory / 'design.json', design)
                case = {'algorithm': algorithm.upper(), 'parameter': parameter, 'operation': operation,
                        'binary': binary, 'binary_sha256': entry['sha256'], 'metadata': meta,
                        'same_fixture_commitment_sha256': 'f' * 64, 'status': 'complete',
                        'passes': [], 'design': design}
                jobs = [('calibration', 'timing', None, 1)]
                jobs += [(f'timing-{i}', 'timing', None, 2) for i in range(7)]
                jobs += [(f'stat-{i}', 'stat', r.CORE, 2) for i in range(3)]
                jobs += [(f'stat-{10+i}', 'stat', event, 2) for i, event in enumerate(r.AUX)]
                jobs += [(f'sample-{i}', 'sample', 'cycles:u', 4) for i in range(2)]
                for name, kind, event, count in jobs:
                    folder = directory / name
                    obs = {'status': 'ok', 'algorithm': algorithm.upper(), 'parameter': parameter,
                           'operation': operation, 'iterations': count,
                           'wall_ns': 20 + 2 * int(name[-1]) if name.startswith('timing-') else 20 * count,
                           'thread_cpu_ns': 18 * count, 'cpu_start': 3, 'cpu_end': 3,
                           'voluntary_switches': 0, 'involuntary_switches': 1,
                           'minor_faults': 2, 'major_faults': 0,
                           'postcheck_outputs': 0 if operation == 'verify' else count,
                           'verification_fixture_valid': True, 'perf_gated': kind in ('stat', 'sample')}
                    result = {'kind': kind, 'operation': operation, 'iterations': count,
                              'events': event, 'argv': ['SYNTHETIC', '<private-memory-fixture>'],
                              'returncode': 0, 'whole_process_wall_ns': 999999999,
                              'observation': obs, 'environment': {'samples': [
                                  {'monotonic_ns': 1, 'frequencies': {'policy0': '100'}, 'temperatures': {'zone0': '30000'}, 'cooling_states': {}}], 'errors': []}}
                    put(folder / 'result.json', result)
                    put(folder / 'stdout.txt', json.dumps(obs) + '\n')
                    put(folder / 'stderr.txt', '')
                    if name != 'calibration':
                        case['passes'].append({'kind': kind, 'event': event, 'iterations': count,
                                               'path': str(folder.relative_to(root)), 'returncode': 0})
                    if kind == 'stat':
                        stat = STAT if event == r.CORE else (
                            '500||branches:u|1000000|100.00||\n5||branch-misses:u|1000000|100.00||\n' if event == r.AUX[0] else
                            '250||cpu/L1-dcache-loads/u|1000000|100.00||\n5||cpu/L1-dcache-load-misses/u|1000000|100.00||\n')
                        put(folder / 'perf-stat.txt', stat)
                    if kind == 'sample':
                        put(folder / 'perf.data', 'SYNTHETIC fixture bytes, NOT a perf recording\n')
                        exports = {'events.txt': EVENTS, 'buildids.txt': 'fixture build id\n',
                                   'report-self.txt': SELF0 if name == 'sample-0' else SELF1,
                                   'report-children.txt': CHILD, 'samples.txt': 'synthetic unused script\n',
                                   'header.txt': '# Total Lost Samples: 0\n', 'annotate-top.txt': 'fixture annotate\n'}
                        commands = []
                        for filename, text in exports.items():
                            put(folder / filename, text)
                            put(folder / (filename + '.stderr'), '')
                            commands.append({'argv': ['SYNTHETIC'], 'returncode': 0,
                                             'output': filename, 'stderr': filename + '.stderr'})
                        put(folder / 'report-commands.json', commands)
                manifest['cases'].append(case)
    put(root / manifest['build_manifest'], build)
    manifest['build_sha256'] = r.sha(root / manifest['build_manifest'])
    put(run / 'manifest.json', manifest)
    return manifest


class ParserTests(unittest.TestCase):
    def test_actual_self_header(self):
        table = r.parse_report(SELF0, 'self')
        self.assertEqual(table['total_period'], 10000)
        self.assertEqual(table['rows'][0]['symbol'], 'gf16_mul')
        self.assertEqual(table['rows'][1]['symbol'], '[unknown]')

    def test_reordered_columns_unknown_retained(self):
        table = r.parse_report('# Symbol|Vendor|Period|Self|Shared Object|Samples\n[.] foo|opaque|123|100.00%|lib.so|7\n', 'self')
        self.assertEqual(table['rows'][0]['raw']['Vendor'], 'opaque')
        self.assertEqual(table['rows'][0]['samples'], 7)

    def test_interleaved_callgraph_is_preserved_not_counted(self):
        graph = '             |\n             ---gf16_mul\n                |--33.33%-- called_function\n'
        table = r.parse_report(SELF0.replace('30.00%|300', graph + '30.00%|300'), 'self')
        self.assertEqual(len(table['rows']), 2)
        self.assertEqual(table['total_period'], 10000)
        self.assertEqual(len(table['callgraph_lines_not_histogram_rows']), 3)
        self.assertIn('called_function', table['callgraph_lines_not_histogram_rows'][-1]['raw'])
        with self.assertRaisesRegex(r.ParseError, '不是 cycles:u'):
            r.parse_report(SELF0.replace("'cycles:u'", "'instructions:u'"), 'self')

    def test_children_multi_column_and_overlap(self):
        children = r.parse_report(CHILD, 'children')
        rows = r.aggregate_children([(children, 10000), (children, 30000)])
        self.assertEqual(sum(row['children_pct'] for row in rows), 200)
        self.assertEqual(rows[0]['evidence'][0]['raw']['Vendor column'], 'keep-me')
        self.assertEqual(rows[0]['children_pct'], 90)

    def test_weighted_self_not_average_of_percentages(self):
        rows = r.aggregate_self([r.parse_report(SELF0, 'self'), r.parse_report(SELF1, 'self')])
        by_symbol = {row['symbol']: row for row in rows}
        self.assertEqual(by_symbol['gf16_mul']['self_pct'], 25)
        self.assertEqual(by_symbol['[unknown]']['self_pct'], 7.5)
        self.assertEqual(by_symbol['EVP_DigestUpdate']['self_pct'], 67.5)
        self.assertEqual(rows[-1]['cumulative_pct'], 100)
        self.assertEqual(len(rows), 3)

    def test_same_function_different_dso_not_collapsed(self):
        table = r.parse_report('# Overhead|Samples|Period|Shared Object|Symbol\n50%|1|10|a|foo\n50%|1|10|b|foo\n', 'self')
        self.assertEqual(len(r.aggregate_self([table])), 2)

    def test_duplicate_self_rows_sum_by_identity(self):
        table = r.parse_report('# Overhead|Samples|Period|Shared Object|Symbol\n50%|1|10|a|foo\n50%|1|10|a|foo\n', 'self')
        self.assertEqual(r.aggregate_self([table])[0]['samples'], 2)

    def test_unsupported_table_is_precise_error(self):
        with self.assertRaisesRegex(r.ParseError, 'myfile:'):
            r.parse_report('100% foo\n', 'self', 'myfile')
        with self.assertRaisesRegex(r.ParseError, 'myfile:4/Samples:'):
            r.parse_report(SELF0.replace('70.00%|700|7000', '70.00%|seven|7000'), 'self', 'myfile')

    def test_filtered_percentages_not_fabricated(self):
        with self.assertRaisesRegex(r.ParseError, '分母不一致'):
            r.parse_report('# Overhead|Samples|Period|Shared Object|Symbol\n50%|1|10|a|foo\n', 'self')
        with self.assertRaisesRegex(r.ParseError, 'event count'):
            r.parse_report(SELF0.replace('10000', '20000'), 'self')

    def test_multi_event_tables_rejected(self):
        with self.assertRaisesRegex(r.ParseError, '多表/多事件'):
            r.parse_report(SELF0 + SELF1, 'self')

    def test_children_duplicate_not_added(self):
        children = r.parse_report(CHILD + '90%|70%|700|7000|bench|[.] gf16_mul|again\n', 'children')
        with self.assertRaisesRegex(r.ParseError, '重复 children'):
            r.aggregate_children([(children, 10000)])

    def test_categories_mutually_exclusive_and_unknown(self):
        for symbol, category in [('getrandom', 'RNG'), ('randombytes_aes', 'RNG'), ('__memcpy_avx', 'memory'),
                                 ('EVP_DigestUpdate', 'hashAES'), ('gf16_mul', 'Algebra'), ('foo', 'other'), ('0x123abc', 'unknown')]:
            self.assertEqual(r.category('libcrypto.so', symbol), category)
        self.assertEqual(r.category('[unknown]', 'gf16_mul'), 'unknown')

    def test_stat_empty_unit_and_extra_metric(self):
        rows = r.parse_stat(STAT)
        self.assertEqual(rows[0]['raw_fields'][1], '')
        self.assertEqual(rows[1]['extra_fields'], ['2.00', 'insn per cycle'])
        self.assertEqual(r.stat_metrics(rows, 2)['ipc'], 2)
        self.assertEqual(r.stat_metrics(rows, 2)['cycles_per_op'], 1000)

    def test_stat_markers_and_missing_fields(self):
        rows = r.parse_stat('<not counted>||cycles:u|0|0||\n<not supported>||instructions:u||||\n||unknown||||\n')
        self.assertEqual([row['status'] for row in rows], ['not_counted', 'not_supported', 'missing'])
        self.assertIsNone(r.stat_metrics(rows, 2)['ipc'])
        self.assertIsNone(rows[2]['count'])

    def test_stat_multiplexed_or_no_running_not_metrics(self):
        for text in (STAT.replace('100.00', '99.99'), STAT.replace('100.00', '')):
            self.assertIsNone(r.stat_metrics(r.parse_stat(text), 2)['cycles_per_op'])
            self.assertIsNone(r.stat_metrics(r.parse_stat(text), 2)['ipc'])

    def test_branch_rate_same_pass_coherent_runtime(self):
        text = '500||branches:u|1000|100||\n5||branch-misses:u|1000|100||\n'
        self.assertEqual(r.stat_metrics(r.parse_stat(text), 2)['branch_miss_rate'], .01)
        self.assertIsNone(r.stat_metrics(r.parse_stat(text.replace('5||branch-misses:u|1000', '5||branch-misses:u|999')), 2)['branch_miss_rate'])
        self.assertIsNone(r.stat_metrics(r.parse_stat(text.splitlines()[0]), 2)['branch_miss_rate'])

    def test_event_group_commas_and_unknown_event_preserved(self):
        rows = r.parse_stat('5||{cycles:u,instructions:u}|1000|100||\n')
        self.assertEqual(rows[0]['event'], '{cycles:u,instructions:u}')
        self.assertIsNone(r.stat_metrics(rows, 1)['ipc'])

    def test_invalid_stat_number_error(self):
        with self.assertRaisesRegex(r.ParseError, '/count'):
            r.parse_stat('garbage||cycles:u|1000|100\n')

    def test_sample_type_safe_symbolic_numeric(self):
        self.assertEqual(r.sample_safety(EVENTS)['status'], 'checked_safe_sample_type')
        mask = sum(1 << bit for bit in (0, 1, 2, 5, 7, 8))
        self.assertEqual(r.sample_safety('sample_type: ' + hex(mask))['callchain_completeness'], 'unknown')
        self.assertEqual(r.sample_safety('sample_type: ' + str(mask))['status'], 'checked_safe_sample_type')

    def test_all_forbidden_sample_types_including_raw(self):
        for token in r.FORBIDDEN:
            with self.subTest(token=token), self.assertRaisesRegex(r.ParseError, '禁止'):
                r.sample_safety(EVENTS.replace('IP|TID', 'IP|' + token + '|TID'))
        with self.assertRaisesRegex(r.ParseError, '禁止'):
            r.sample_safety('sample_type: 0x521')  # IP, CALLCHAIN, PERIOD, RAW.

    def test_missing_unknown_sample_types_not_safe(self):
        for text in ('no sample attributes', 'sample_type: IP|PERIOD|CALLCHAIN|NEW', 'sample_type: 0x80000000'):
            with self.assertRaises(r.ParseError):
                r.sample_safety(text)

    def test_loss_missing_not_zero(self):
        loss = r.loss_evidence({'stderr': 'written 42 samples\n'})
        self.assertIsNone(loss['total_lost_samples'])
        self.assertEqual(loss['status'], 'unknown')

    def test_loss_format_flag_is_not_a_loss_counter(self):
        loss = r.loss_evidence({'events': 'cycles:u: sample_type: IP|PERIOD, read_format: ID|LOST, disabled: 1\n',
                               'report': '# Total Lost Samples: 0\n'})
        self.assertEqual(loss['total_lost_samples'], 0)
        self.assertEqual(loss['unparsed_loss_lines'], [])
        self.assertIsNone(r.loss_evidence({'events': 'read_format = ID|LOST, disabled = 1\n'})['total_lost_samples'])
        mixed = r.loss_evidence({'events': 'read_format: ID|LOST, mysterious lost payload\n'})
        self.assertEqual(len(mixed['unparsed_loss_lines']), 1)

    def test_resolved_static_symbol_rows_remain_distinct(self):
        text = '# Children|Self|Samples|Period|Shared Object|Symbol\n'
        text += '20%|5%|1|500|bench|[.] same_name [ELF+0x100]\n'
        text += '10%|0%|0|0|bench|[.] same_name [ELF+0x200]\n'
        table = r.parse_report(text, 'children')
        values = r.aggregate_children([(table, 10000)])
        self.assertEqual(len(values), 2)
        self.assertEqual([v['children_pct'] for v in values], [20, 10])

    def test_loss_totals_and_duplicate_sources_not_summed(self):
        loss = r.loss_evidence({'header': '# Total Lost Samples: 3\n', 'stderr': 'lost 3 samples\n'})
        self.assertEqual(loss['total_lost_samples'], 3)
        self.assertEqual(len(loss['evidence']), 2)
        self.assertEqual(r.loss_evidence({'header': '# Total Lost Samples: 0\n'})['total_lost_samples'], 0)
        self.assertIsNone(r.loss_evidence({'header': '# Total Lost Samples: 0\n', 'script': 'PERF_RECORD_LOST: id: 1 lost: 4\n'})['total_lost_samples'])

    def test_stats_all_trials_population_cv(self):
        result = r.stats(list(range(10, 17)))
        self.assertEqual(result['n'], 7)
        self.assertEqual(result['median'], 13)
        self.assertEqual(result['min'], 10)
        self.assertEqual(result['max'], 16)
        self.assertAlmostEqual(result['cv'], 2 / 13)
        self.assertIsNone(r.stats([])['median'])


class ArchiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='r20-report-python-fixture-')
        cls.root = Path(cls.temp.name)
        cls.manifest = fixture(cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def reporter(self, suffix):
        return r.Reporter(self.root, 'raw/profile-fixture', 'reports/' + suffix)

    def test_full_sixty_cases_and_outputs(self):
        reporter = self.reporter('full')
        summary = reporter.analyze()
        errors = [x for x in summary['issues'] if x['severity'] == 'error']
        self.assertEqual(errors, [])
        self.assertEqual(summary['status'], 'complete')
        self.assertEqual(summary['coverage']['complete_cases'], 60)
        self.assertEqual(summary['coverage']['safe_sample_records'], 120)
        self.assertEqual(summary['incomplete_rows'], [])
        self.assertEqual(len(summary['cases'][0]['passes']), 15)
        baseline = summary['cases'][0]['baseline_wall_ns_per_op']
        self.assertEqual(baseline['trials'], list(range(10, 17)))
        self.assertEqual(baseline['median'], 13)
        sample = next(row for row in summary['cases'][0]['passes'] if row['name'] == 'sample-0')
        self.assertEqual(sample['sampling_overhead_observation']['ratio'], 20 / 13)
        self.assertTrue(sample['sampling_overhead_observation']['iterations_differ'])
        self.assertEqual(sample['record']['sha256'], r.sha(self.root / sample['record']['path']))
        output = reporter.write(summary)
        for filename in ('REPORT.md', 'summary.json', 'performance.csv'):
            self.assertTrue((output / filename).is_file())
        loaded = json.loads((output / 'summary.json').read_text())
        self.assertEqual(loaded['status'], 'complete')
        self.assertEqual(len(list((output / 'cases').iterdir())), 60)
        for case in loaded['cases']:
            case_dir = (output / case['report']).parent
            self.assertIn('Self Top20', (case_dir / 'REPORT.md').read_text())
            for filename in ('hotspots-self.csv', 'hotspots-children.csv', 'DSO.csv', 'categories.csv'):
                self.assertTrue((case_dir / filename).is_file())
        with (output / 'performance.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len(rows), 900)
        self.assertEqual(rows[1]['baseline_wall_median'], '13.0')
        with self.assertRaises(FileExistsError):
            self.reporter('full')
        with self.assertRaises(FileExistsError):
            reporter.write(summary)

    def test_missing_case_even_complete_manifest_fails(self):
        path = self.root / 'raw/profile-fixture/manifest.json'
        original = path.read_text()
        modified = json.loads(original)
        modified['cases'].pop()
        try:
            put(path, modified)
            reporter = self.reporter('partial')
            summary = reporter.analyze()
            self.assertEqual(summary['status'], 'incomplete')
            self.assertFalse(summary['coverage']['sixty_unique_cases'])
            self.assertTrue(summary['coverage']['twenty_parameters'])
            self.assertGreaterEqual(len(summary['incomplete_rows']), 15)
            with self.assertRaises(r.IncompleteError):
                reporter.write(summary)
            self.assertFalse(reporter.output.exists())
            reporter.write(summary, allow_partial=True)
            self.assertEqual(json.loads((reporter.output / 'summary.json').read_text())['status'], 'incomplete')
            self.assertIn('未完成 rows', (reporter.output / 'REPORT.md').read_text())
        finally:
            put(path, original)

    def test_permission_failure_low_samples_and_bad_safety(self):
        case = self.manifest['cases'][0]
        pass_path = self.root / next(x['path'] for x in case['passes'] if x['path'].endswith('/stat-0'))
        sample_path = self.root / next(x['path'] for x in case['passes'] if x['path'].endswith('/sample-0'))
        paths = [pass_path / 'result.json', pass_path / 'stderr.txt', sample_path / 'events.txt']
        original = [path.read_text() for path in paths]
        try:
            result = json.loads(original[0])
            result.update(returncode=255, observation=None)
            put(paths[0], result)
            put(paths[1], 'No permission to enable cycles:u event\n')
            put(paths[2], EVENTS.replace('IP|', 'RAW|IP|'))
            summary = self.reporter('failure').analyze()
            self.assertEqual(summary['status'], 'incomplete')
            self.assertGreaterEqual(len(summary['incomplete_rows']), 2)
            self.assertTrue(any('禁止' in issue['message'] for issue in summary['issues']))
            failing_case = next(c for c in summary['cases'] if c['algorithm'] == 'mayo' and c['parameter'] == 'MAYO-1' and c['operation'] == 'keygen')
            self.assertEqual(failing_case['sample_passes_aggregated'], 1)
            self.assertTrue(any(item['path'].endswith('stderr.txt') for item in summary['artifacts']))
        finally:
            for path, text in zip(paths, original):
                put(path, text)

    def test_low_samples_unknown_loss_and_unsupported_export(self):
        case = self.manifest['cases'][0]
        directory = self.root / next(x['path'] for x in case['passes'] if x['path'].endswith('/sample-0'))
        paths = [directory / 'report-self.txt', directory / 'header.txt', directory / 'report-children.txt']
        original = [path.read_text() for path in paths]
        try:
            put(paths[0], SELF0.replace('|700|', '|70|').replace('|300|', '|30|'))
            put(paths[1], '# no loss statistics available\n')
            put(paths[2], 'unsupported perf report layout\n')
            summary = self.reporter('low-samples').analyze()
            self.assertEqual(summary['status'], 'incomplete')
            self.assertTrue(any(issue['code'] == 'low_samples' for issue in summary['issues']))
            self.assertTrue(any(issue['code'] == 'loss_unknown' for issue in summary['issues']))
            self.assertTrue(any(issue['code'] == 'precise_parse_error' and 'report-children.txt' in issue['message'] for issue in summary['issues']))
            measured_case = next(c for c in summary['cases'] if c['id'] == 'mayo-MAYO-1/keygen')
            row = next(row for row in measured_case['passes'] if row['name'] == 'sample-0')
            self.assertEqual(row['samples'], 100)
            self.assertTrue(row['minimum_sample_warning'])
            self.assertIsNone(row['loss']['total_lost_samples'])
            self.assertEqual(measured_case['sample_passes_aggregated'], 2)
            self.assertEqual(measured_case['children_passes_aggregated'], 1)
        finally:
            for path, text in zip(paths, original):
                put(path, text)

    def test_duplicate_cases_do_not_satisfy_sixty_case_gate(self):
        path = self.root / 'raw/profile-fixture/manifest.json'
        original = path.read_text()
        modified = json.loads(original)
        modified['cases'][-1] = modified['cases'][0]
        try:
            put(path, modified)
            summary = self.reporter('duplicate').analyze()
            self.assertEqual(summary['status'], 'incomplete')
            self.assertEqual(summary['coverage']['manifest_case_count'], 60)
            self.assertEqual(summary['coverage']['unique_case_count'], 59)
            self.assertFalse(summary['coverage']['sixty_unique_cases'])
            self.assertTrue(any(issue['code'] == 'duplicate_case' for issue in summary['issues']))
        finally:
            put(path, original)

    def test_independent_algorithm_and_parameter_gates(self):
        path = self.root / 'raw/profile-fixture/manifest.json'
        original = path.read_text()
        modified = json.loads(original)
        modified['cases'] = [case for case in modified['cases'] if case['algorithm'] != 'MQOM']
        try:
            put(path, modified)
            summary = self.reporter('missing-family').analyze()
            for gate in ('five_algorithms', 'twenty_parameters', 'sixty_unique_cases'):
                self.assertFalse(summary['coverage'][gate])
        finally:
            put(path, original)

    def test_hash_mismatch_cannot_report_green(self):
        path = self.root / 'native/fixture.c'
        original = path.read_text()
        try:
            put(path, original + 'modified\n')
            summary = self.reporter('hash-failure').analyze()
            self.assertEqual(summary['status'], 'incomplete')
            self.assertTrue(any(issue['code'] == 'hash_mismatch' for issue in summary['issues']))
        finally:
            put(path, original)

    def test_relative_paths_root_not_run_and_traversal_rejected(self):
        reporter = self.reporter('paths')
        first_path = self.manifest['cases'][0]['passes'][0]['path']
        self.assertEqual(reporter.resolve(first_path), self.root / first_path)
        for value in ('../outside', '/tmp/outside'):
            with self.assertRaises(ValueError):
                reporter.resolve(value)
        with self.assertRaises(ValueError):
            r.Reporter(self.root, 'raw/profile-fixture', 'raw/new-report')
        with self.assertRaises(ValueError):
            r.Reporter(self.root, 'raw/profile-fixture', 'reports/a/b')

    def test_relocated_absolute_library_references_stay_inside_root(self):
        reporter = self.reporter('libraries')
        original_root = Path('/producer/archive/R20')
        library = 'upstream/openssl/lib/libcrypto.so'
        self.assertEqual(reporter.library_reference(str(original_root / library), {original_root}), self.root / library)
        self.assertEqual(reporter.library_reference(str(self.root / library), set()), self.root / library)
        with self.assertRaisesRegex(ValueError, '外部 DSO'):
            reporter.library_reference('/outside/libcrypto.so', {original_root})
        with self.assertRaises(ValueError):
            reporter.library_reference('../../other-experiment/libcrypto.so', {original_root})

    def test_aux_numbering_is_packed_not_assumed(self):
        reporter = self.reporter('aux')
        manifest = {'perf_capabilities': [
            {'event': r.CORE, 'supported': True}, {'event': r.AUX[0], 'supported': False},
            {'event': r.AUX[1], 'supported': True}]}
        jobs = reporter.expected_jobs(manifest)
        self.assertIn(('stat-10', 'stat', r.AUX[1]), jobs)
        self.assertFalse(any(job[0] == 'stat-11' for job in jobs))
        self.assertEqual(reporter.issues, [])

    def test_cli_rejects_missing_input_without_creating_output(self):
        with contextlib.redirect_stderr(io.StringIO()):
            result = r.main([str(self.root), '--run', 'raw/profile-absent', '--output', 'reports/not-created', '--allow-partial'])
        self.assertEqual(result, 2)
        self.assertFalse((self.root / 'reports/not-created').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
