"""Change only the ftop test app's real Shizuku grant on an emulator, using current UI bounds."""
import argparse
import re
import subprocess
import time
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--serial', required=True)
p.add_argument('--state', choices=['allow', 'deny'], required=True)
args = p.parse_args()
if not args.serial.startswith('emulator-'):
    raise SystemExit('This test helper only operates on an emulator.')
base = ['adb', '-s', args.serial]

def shell(*command):
    return subprocess.check_output(base + ['shell', *command], text=True, encoding='utf-8', errors='replace', timeout=25)

shell('am', 'start', '-n', 'moe.shizuku.privileged.api/moe.shizuku.manager.MainActivity')

def read_ui():
    shell('uiautomator', 'dump', '/sdcard/ftop-grant-test.xml')
    return ET.fromstring(shell('cat', '/sdcard/ftop-grant-test.xml'))

def tap(node):
    x1,y1,x2,y2 = map(int,re.findall(r'\d+',node.get('bounds','')))
    shell('input','tap',str((x1+x2)//2),str((y1+y2)//2))

def toggle_node(root):
    for node in reversed(list(root.iter('node'))):
        children = list(node.iter('node'))
        if any(n.get('text') == 'dev.ftop.widget' for n in children):
            switches = [n for n in children if n.get('checkable') == 'true']
            if len(switches) == 1:
                return switches[0]
    return None

node = None
for _ in range(3):
    root = read_ui()
    node = toggle_node(root)
    if node is not None:
        break
    links = [n for n in root.iter('node') if '已授权' in n.get('text','') or 'Authorized' in n.get('text','')]
    if not links:
        raise RuntimeError('Cannot find Shizuku authorization entry. No grant changed.')
    tap(links[0])
if node is None:
    raise RuntimeError('Cannot find ftop grant switch. No grant changed.')
desired = args.state == 'allow'
if (node.get('checked') == 'true') != desired:
    tap(node)
    time.sleep(.3)
    assert (toggle_node(read_ui()).get('checked') == 'true') == desired, 'Grant state did not change'
print('Shizuku ftop grant:', args.state)
