from pathlib import Path
import json, sys
root = Path(sys.argv[1])
flavor = sys.argv[2]
data = json.loads((root / "source-list.json").read_text())
for path in data["include_dirs"]:
    print("+incdir+" + str(root / flavor / "cva6" / path))
for path in data["sources"]:
    print(root / "tops/xmpk_cva6_system_top.sv" if path == "@SYSTEM_TOP@" else root / flavor / "cva6" / path)
