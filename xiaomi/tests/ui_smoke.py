"""Run the platform UiAutomation tests on an explicitly selected test device.
Live metrics generate frequent accessibility events, so the tests read the view tree
without waiting for the global accessibility event stream to become idle.
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--serial",required=True)
args=parser.parse_args()
out=ROOT/"scratch"/"tests"/args.serial
out.mkdir(parents=True,exist_ok=True)
command=["adb","-s",args.serial]
result=subprocess.run(command+["shell","am","instrument","-w","dev.ftop.widget.tests/dev.ftop.widget.tests.UiTests"],capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=90)
(out/"ui-instrumentation.txt").write_text(result.stdout+result.stderr,encoding="utf-8")
print(result.stdout)
if result.returncode or '\"status\": \"passed\"' not in result.stdout:
    raise SystemExit("UI tests failed; see ui-instrumentation.txt")
subprocess.run(command+["pull","/sdcard/Android/data/dev.ftop.widget/files/ui-tests/.",str(out)],check=True)
report=json.loads((out/"ui-results.json").read_text(encoding="utf-8"))
report["serial"]=args.serial
report["device"]=subprocess.check_output(command+["shell","getprop","ro.product.model"],text=True).strip()
(out/"ui-results.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
