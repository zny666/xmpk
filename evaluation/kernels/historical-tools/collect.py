"""Collect the final routed reports after both existing Vivado jobs exit."""
from pathlib import Path
import io
import json
import subprocess
import tarfile

root = Path(__file__).resolve().parents[1]
jobs = json.loads((root / "results/remote-jobs.json").read_text())["jobs"]
wait = "\n".join(f"tail --pid={int(pid)} -f /dev/null" for pid in jobs.values())
script = wait + """
python3 - <<'REMOTE'
from pathlib import Path
import io, tarfile, sys
root = Path.home() / 'xmpk-hardware-final-20261113'
with tarfile.open(fileobj=sys.stdout.buffer, mode='w|gz') as archive:
 for p in sorted((root / 'results').rglob('*')):
  if p.is_file() and (p.suffix in {'.rpt', '.json', '.txt', '.xdc'} or p.name in {'PASS', 'ERROR'}):
   archive.add(p, arcname=str(p.relative_to(root / 'results')))
 for p in root.glob('vivado*.log'):
  archive.add(p, arcname=p.name)
REMOTE
"""
p = subprocess.run(
    ["ssh", "-o", "BatchMode=yes", "reviewer@REDACTED_IP", "bash"],
    input=script.encode(), capture_output=True, check=True,
)
with tarfile.open(fileobj=io.BytesIO(p.stdout), mode="r:gz") as archive:
    # Python 3.9 does not provide extraction filters. This archive contains
    # reports only: reject links and paths escaping the destination.
    destination = (root / "results").resolve()
    for member in archive.getmembers():
        assert member.isfile() or member.isdir(), member.name
        assert (destination / member.name).resolve().is_relative_to(destination), member.name
    archive.extractall(destination)
for kind in ("copro", "system"):
    folder = root / "results" / f"final-{kind}"
    assert (folder / "PASS").exists(), f"Missing PASS: {folder}"
    assert not (folder / "ERROR").exists(), (folder / "ERROR").read_text()
print("FINAL_REPORTS_COLLECTED", len(p.stdout), "bytes")
