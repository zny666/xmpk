#!/usr/bin/env python3
from collections import Counter
import csv
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct
import tarfile

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1048576),b''):h.update(chunk)
    return h.hexdigest()


def csvfile(path,rows):
    with path.open('x',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)


def main():
    received=ROOT/'results/received';report=received/'reports/final-v2';summary=json.loads((report/'summary.json').read_text());index=json.loads((received/'_capture.json').read_text())
    if summary['status']!='complete' or summary['coverage']['complete_cases']!=60 or summary['incomplete_rows']:raise ValueError('reports incomplete')
    for item in index['files']:
        path=(received/item['path']).resolve()
        if not path.is_relative_to(received) or path.stat().st_size!=item['bytes'] or sha(path)!=item['sha256']:raise ValueError('captured file changed: '+item['path'])
    parameters={};top=[];sample_bits=[];all_samples=0;losses=[];small=[]
    for case in summary['cases']:
        if case['status']!='complete' or case['baseline_wall_ns_per_op']['n']!=7:raise ValueError('incomplete case')
        if abs(sum(x['self_pct'] for x in case['hotspots_self'])-100)>1e-6:raise ValueError('invalid self denominator')
        if len(case['passes'])!=15:raise ValueError('expected calibration + fourteen passes')
        for row in case['passes']:
            if row['status']!='complete':raise ValueError('incomplete pass')
            if row['kind']=='sample':
                raw=received/row['path']/'perf.data'
                with raw.open('rb') as f:
                    header=f.read(104)
                    if header[:8]!=b'PERFILE2':raise ValueError('unsupported raw perf format')
                    _,attr_size,attr_off,attr_bytes=struct.unpack_from('<QQQQ',header,8)
                    if not 64<=attr_size<=4096 or attr_bytes%attr_size or attr_bytes//attr_size>20:raise ValueError('invalid perf attrs')
                    for n in range(attr_bytes//attr_size):
                        f.seek(attr_off+n*attr_size);attr=f.read(attr_size);kind=struct.unpack_from('<I',attr,0)[0];config=struct.unpack_from('<Q',attr,8)[0];bits=struct.unpack_from('<Q',attr,24)[0];flags=struct.unpack_from('<Q',attr,40)[0]
                        if bits & sum(1<<bit for bit in [10,12,13,15,18,19,20]):raise ValueError('forbidden raw perf sample payload')
                        if not (flags&(1<<5)) or not(flags&(1<<6)):raise ValueError('kernel/hypervisor counting unexpectedly enabled')
                        if kind==0 and config==0 and not(bits&(1<<5)):raise ValueError('cycles callchain not recorded')
                        sample_bits.append({'path':str(raw.relative_to(received)),'type':kind,'config':config,'sample_type':bits})
                loss=row['loss']['total_lost_samples'];losses.append(loss)
                if loss is None:raise ValueError('loss counter unresolved')
                if row['samples']<1000:small.append({'case':case['id'],'pass':row['name'],'samples':row['samples']})
                all_samples+=row['samples']
        key=(case['algorithm'],case['parameter']);p=parameters.setdefault(key,{'algorithm':key[0],'parameter':key[1]})
        p[case['operation']+'_median_ms']=case['baseline_wall_ns_per_op']['median']/1e6
        p[case['operation']+'_CV_pct']=case['baseline_wall_ns_per_op']['cv']*100
        for rank,item in enumerate(case['hotspots_self'],1):
            top.append({'algorithm':key[0],'parameter':key[1],'operation':case['operation'],'rank':rank,'symbol':item['symbol'],'DSO':item['dso'],'category':item['category'],'period':item['period'],'samples':item['samples'],'self_percent':item['self_pct'],'cumulative_percent':item['cumulative_pct']})
    if len(parameters)!=20 or len(losses)!=120 or len(sample_bits)!=240:raise ValueError('raw profile coverage mismatch')
    output=ROOT/'reports';rows=sorted(parameters.values(),key=lambda x:(x['algorithm'],x['parameter']))
    csvfile(output/'operation-summary.csv',rows);csvfile(output/'all-self-hotspots.csv',top)
    links=lambda p:os.path.relpath(p,output)
    lines=['# 212：五算法原始 C 实现性能与 perf 数据总览','',
           '## 完成情况','',
           '- 已完成20组参数 × KeyGen/Sign/Verify，共60个操作场景。',
           '- 420批无perf基准计时、180批核心PMU stat、120批分支/缓存辅助stat、120份cycles用户态FP采样，另有60次校准。',
           f'- 合计{all_samples:,}个正式采样点；120份记录明确报告丢样共{sum(losses)}。',
           f'- 原始回传{len(index["files"]):,}个文件逐文件SHA-256检查通过；原始perf事件属性直接解码检查，无用户寄存器、栈字节、RAW/物理地址采集。',
           '- 仅本地归档；没有提交或推送到仓库，没有修改之前的实验或论文。','',
           '## 数据口径','',
           '- 官方 C reference/portable，-O3、rv64gc、无项目手工RVV/Zbb/Zvkned，保留上游SWAR/便携算法。MAYO明确是ref动态参数，UOV为classic完整密钥接口，不等同于旧Compact/Full优化实验。',
           '- MQOM官方C reference和optimized为同源，选择便携后端；`ct`指correlated-tree，不是constant-time。',
           '- 每参数一把Sign/Verify fixture密钥；每次KeyGen产生新钥。每场景7批的中位ns/op为性能基线；不是多钥均值，不与旧R18/R19结果混算。',
           '- 单批多次调用的缓存/首次调用效应取决于固定批次计数；采样批次与计时批次计数不同，其比值不是严格因果采集开销。',
           '- signed-message原API内部的展开、随机数、求解、复制保留；额外fixture准备和外部自签验签位于perf开关之外。',
           '- `self %`是全部用户态cycles样本period权重占比（含未知符号），不是精确墙钟耗时占比。`children %`含后代，允许重叠，不可相加为100%。',
           '- -O3允许内联，热点落在sign大函数时不代表其中没有有限域/矩阵子操作；共享库热点与未知符号均未删除。','',
           '## 参数级时间汇总（毫秒/操作）','',
           '|算法|参数|KeyGen中位|Sign中位|Verify中位|','|---|---|---:|---:|---:|']
    for row in rows:lines.append(f'|{row["algorithm"]}|{row["parameter"]}|{row["keygen_median_ms"]:.6f}|{row["sign_median_ms"]:.6f}|{row["verify_median_ms"]:.6f}|')
    lines+=['','## 各参数签名首要热点（其余函数均在完整CSV中）','','|算法|参数|Top1函数|self %|','|---|---|---|---:|']
    for case in summary['cases']:
        if case['operation']=='sign':
            f=case['hotspots_self'][0];lines.append(f'|{case["algorithm"]}|{case["parameter"]}|`{f["symbol"]}`|{f["self_pct"]:.2f}|')
    lines+=['','## 质量与已知限制','',
            '- 所有构建已通过本地自签验签和错误签名/消息/截断拒绝。按用户要求不再以KAT阻塞；SNOVA-I首个官方归档KAT与当前reference不一致的记录保留，未继续调查，故不声称标准一致性认证。',
            '- 本次未追求系统完全隔离；记录频率、温控绑定、抢占和环境状态，全部慢值保留。',
            '- FP调用链可能受库的编译方式影响；未展开/未知帧不可凭空恢复，函数占比只能在此采样配置下解释。',
            '- MQOM三级Verify的同名静态函数已依据原perf中的mmap、verbose报告与原ELF符号地址区分；原报告、地址证据和解析修复均留存，没有直接相加重叠children。',
            f'- {len(small)}个采样pass不足1000点（由固定校准计数导致），不追加挑选或宣称热点排名高精度：']
    for item in small:lines.append(f'  - {item["case"]} / {item["pass"]}: {item["samples"]}点')
    lines+=['','## 文件导航','',f'- [完整60场景报告及逐项质量检查]({links(report/"REPORT.md")})',f'- [完整JSON（包含原始行、所有函数和证据）]({links(report/"summary.json")})',f'- [每批计时、PMU及采样数据CSV]({links(report/"performance.csv")})','- [20参数操作时间CSV](operation-summary.csv)','- [全部场景所有self热点CSV](all-self-hotspots.csv)',f'- [原始采样目录]({links(received/summary["run"])})',f'- [ELF/共享库与Build-ID对应表]({links(received/"symbols/bindings.json")})',f'- [用户确认的自签验签口径]({links(received/"evidence/user-performance-priority.json")})',f'- [同名函数地址核对]({links(received/"evidence/symbol-address-review/resolutions.json")})','',
            '## 重现报告','',
            '从实验根目录执行：','```bash','python3 tools/report_reference.py results/received \\',f'  --run {summary["run"]} \\','  --output reports/recomputed','```',
            '输出目录必须是新目录，避免覆盖已有报告。原始perf与参考实现可以离线检查；本机为非RISC-V时不能直接执行目标ELF。','']
    (output/'SUMMARY.md').write_text('\n'.join(lines),encoding='utf-8')
    receipt={'status':'complete_local_data_and_reports','created_utc':datetime.now(timezone.utc).isoformat(),'coverage':summary['coverage'],'raw_sample_count':all_samples,'raw_perf_records':len(losses),'raw_perf_attribute_records_checked':len(sample_bits),'lost_samples_total':sum(losses),'low_sample_passes':small,'root_summary':'reports/SUMMARY.md','detailed_report':'results/received/reports/final-v2/REPORT.md','raw_capture_sha256':sha(ROOT/'archive/complete-capture.tar.gz'),'capture_files_verified':len(index['files']),'sample_event_attributes':sample_bits,'sampling_profile':'user-mode cycles with FP addresses only','selfverify_only_user_scope':True,'unchanged_original_capture':True,'no_remote_repository_push':True}
    with (ROOT/'results/final-verification.json').open('x') as f:json.dump(receipt,f,indent=2);f.write('\n')
    print(json.dumps({k:receipt[k] for k in ['status','raw_perf_records','raw_sample_count','lost_samples_total','capture_files_verified','root_summary']}))


if __name__=='__main__':main()
