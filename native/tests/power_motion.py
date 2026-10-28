"""Focused native integration checks, isolated config and only owned HWNDs.

Run elevated, as required by the application's sensor manifest. No global input.
"""
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import subprocess
import sys
import time
import threading
from PIL import Image, ImageChops, ImageGrab

exe, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
u = C.windll.user32
u.SetProcessDpiAwarenessContext.argtypes = [C.c_void_p]
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageW.restype = W.LPARAM
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
u.GetDlgItem.argtypes = [W.HWND, C.c_int]
u.GetDlgItem.restype = W.HWND
u.SetWindowTextW.argtypes = [W.HWND, W.LPCWSTR]
class CopyData(C.Structure):
    _fields_ = [('dwData', C.c_size_t), ('cbData', W.DWORD), ('lpData', C.c_void_p)]

config_path = out / 'config.json'
config = dict(width=760, height=360, x=30, y=30, appearance='light', language='zh',
              floating=True, menuBar=False, motion=True, processSort='memory', opacity=1)
config_path.write_text(json.dumps(config), encoding='utf8')
p = subprocess.Popen([str(exe), '--config', str(config_path)], creationflags=subprocess.CREATE_NO_WINDOW)

def own(title):
    found = []
    @C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(hwnd, _):
        pid = W.DWORD()
        u.GetWindowThreadProcessId(hwnd, C.byref(pid))
        if pid.value == p.pid:
            name = C.create_unicode_buffer(256)
            u.GetWindowTextW(hwnd, name, 256)
            if name.value == title: found.append(hwnd)
        return True
    u.EnumWindows(cb, 0)
    return found[0] if found else None

def wait_for(fn, timeout=10):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        value = fn()
        if value: return value
        time.sleep(.08)
    raise AssertionError('Timed out: ' + str(fn))

hwnd = wait_for(lambda: own('top'))
def command(text):
    data = C.create_unicode_buffer(text)
    message = CopyData(0, C.sizeof(data), C.cast(data, C.c_void_p))
    assert u.SendMessageW(hwnd, 0x4A, 0, C.addressof(message)), text

def state():
    command('inspect:' + str(out / 'state.json'))
    return json.loads((out / 'state.json').read_text(encoding='utf8'))

def capture(name):
    path = out / (name + '.bmp')
    command('capture:' + str(path))
    im = Image.open(path).convert('RGB')
    im.save(out / (name + '.png'))
    return im

def click(which, key, asynchronous=False):
    s = state()
    hit = next(h for h in s[which]['hits'] if h['id'] == key or h['key'] == key)
    rect = hit['rect']; scale = s[which]['dpi'] * (s['layout']['scale'] if which == 'panel' else 1)
    x, y = int((rect[0]+rect[2])/2*scale), int((rect[1]+rect[3])/2*scale)
    win = own('top' + ('' if which == 'panel' else ' ' + which))
    (u.PostMessageW if asynchronous else u.SendMessageW)(win, 0x202, 0, (y<<16)|x)

def setup():
    command('settings'); click('settings', 'more'); click('settings', 'powerSetup', True)
    return wait_for(lambda: own('功耗估算参数'))

def set_edit(dialog, id, value):
    child = u.GetDlgItem(dialog, id)
    assert child
    value_buffer = C.create_unicode_buffer(value)
    assert u.SendMessageW(child, 0xC, 0, C.addressof(value_buffer))
    actual = C.create_unicode_buffer(256)
    u.SendMessageW(child, 0xD, 256, C.addressof(actual))
    assert actual.value == value, (id, actual.value, value)

def update_config(**values):
    current = json.loads(config_path.read_text(encoding='utf8'))
    current.update(values)
    config_path.write_text(json.dumps(current), encoding='utf8')
    time.sleep(1.3)

result = {}
try:
    wait_for(lambda: state()['sensorState'] == 'ready')
    s = state()
    wait_for(lambda: state()['memoryPowerState'] == 'ready')
    s = state()
    assert not s['estimatePower'] and s['displayPower'][2]['watts'] > 0
    command('settings'); click('settings', 'estimatePower')
    s = state()
    assert s['estimatePower'] and not s['displayPower'][2]['estimated']
    assert s['displayPower'][2]['model'] == 'intel-dram-rapl' and s['displayPower'][3]['model'] == 'hardware-reference-v1'
    assert s['memoryWatts'] > 0 and s['systemWatts'] == -1
    assert s['platformHardware']['kind'] == 2 and s['platformHardware']['ssd'] == 1
    assert s['platformHardware']['disksKnown'] and 0 <= s['platformHardware']['brightness'] <= 100
    base = s['automaticBaseline']
    assert abs(base['watts'] - sum(base[k] for k in ['board','display','storage','fans','conversion'])) < 1e-8
    assert abs(base['display'] - (2 + 4*s['platformHardware']['brightness']/100)) < 1e-8
    assert abs(base['storage'] - (.5 + 4.5*s['diskBusy']/100)) < 1e-8
    assert abs(s['displayPower'][3]['watts'] - sum(v['watts'] for v in s['displayPower'][:3]) - base['watts']) < 1e-8
    result['automaticSystem'] = s['displayPower'][3]
    result['automaticBaseline'] = base
    result['platformHardware'] = s['platformHardware']
    result['automaticMemory'] = s['displayPower'][2]
    result['hardware'] = s['memoryModules']
    click('settings', 'estimatePower')
    off = state()['displayPower']
    assert off[2]['watts'] > 0 and off[3]['watts'] == -1
    click('settings', 'estimatePower')
    u.ShowWindow(own('top settings'), 0)
    click('panel', 'memoryPower')
    tip = state()
    assert tip['detailMetric'] == 'memoryPower' and any('能量增量' in s for s in tip['detailLines'])
    (out/'memory-detail.json').write_text(json.dumps(tip['detailLines'],ensure_ascii=False),encoding='utf8')
    # Short bounded load in our own 128 MiB buffers. Record the shipped sampler,
    # not a separate sensor reader; no system settings or external data touched.
    def samples(count):
        values = []
        for _ in range(count):
            time.sleep(1.1)
            v = state()
            assert v['memoryPowerState'] == 'ready' and v['memoryPowerError'] == 0
            assert v['displayPower'][2]['model'] == 'intel-dram-rapl'
            values.append(v['memoryWatts'])
        return values
    baseline = samples(3)
    def copy_load():
        size = 64 * 1024 * 1024
        src, dst = C.create_string_buffer(size), C.create_string_buffer(size)
        until = time.monotonic() + 4.5
        while time.monotonic() < until: C.memmove(dst, src, size)
    worker = threading.Thread(target=copy_load)
    worker.start()
    loaded = samples(4)
    worker.join()
    recovery = samples(3)
    result['liveDramWatts'] = dict(baseline=baseline, loaded=loaded, recovery=recovery)
    assert max(loaded) > min(baseline) + .3, result['liveDramWatts']
    # Parameter entry through the shipped dialog; these are test-only references.
    dialog = setup()
    r = W.RECT(); u.GetWindowRect(dialog, C.byref(r)); time.sleep(.2)
    ImageGrab.grab(bbox=(r.left,r.top,r.right,r.bottom)).save(out/'parameters.png')
    set_edit(dialog, 1003, '10')
    u.SendMessageW(dialog, 0x111, 1, 0)
    time.sleep(.2)
    assert own('功耗估算参数'), 'Missing source accepted'
    set_edit(dialog, 1004, 'TEST ONLY: DC platform reference')
    u.SendMessageW(dialog, 0x111, 1, 0)
    wait_for(lambda: not own('功耗估算参数'))
    time.sleep(1.2)
    s = state()
    assert s['displayPower'][3]['estimated']
    assert abs(s['displayPower'][3]['watts'] - sum(v['watts'] for v in s['displayPower'][:3]) - 10) < 1e-8
    result['systemTestOnlyReference'] = s['displayPower'][3]
    click('panel', 'systemPower'); tip = state()
    assert tip['detailMetric'] == 'systemPower' and '基础参考值' in ''.join(tip['detailLines'])
    (out/'system-detail.json').write_text(json.dumps(tip['detailLines'],ensure_ascii=False),encoding='utf8')
    time.sleep(.4)
    frames = []
    for i in range(20):
        frames.append(capture(f'frame-{i:02d}'))
        time.sleep(.1)
    s = state(); scale = s['panel']['dpi'] * s['layout']['scale']
    def icon_rect(key):
        h = next(h for h in s['panel']['hits'] if h['key']==key)
        x,y = h['rect'][:2]
        return tuple(round(v*scale) for v in (x,y,x+20,y+22))
    result['changedIconFrames'] = {}
    for key in ['power','gpuPower','memoryPower','systemPower']:
        box = icon_rect(key)
        changes = sum(ImageChops.difference(frames[0].crop(box),im.crop(box)).getbbox() is not None for im in frames[1:])
        result['changedIconFrames'][key] = changes
        assert changes > 5, (key, changes)
    # Record the actual native frames, not an illustrated animation.
    frames[0].save(out/'live-motion-test.gif', save_all=True, append_images=frames[1:], duration=120, loop=0)
    update_config(motion=False)
    a=state(); time.sleep(.35); b=state()
    assert [v['phase'] for v in a['displayPower']] == [v['phase'] for v in b['displayPower']]
    assert a['fanAnimation']['angle'] == b['fanAnimation']['angle']
    result['motionOff'] = True
    update_config(motion=True, platformBaseWatts=-1, platformBaseSource='')
    click('panel', 'systemPower'); tip=state()
    assert '硬件基础模型' in ''.join(tip['detailLines'])
    assert '粗估' in ''.join(tip['detailLines'])
    (out/'automatic-system-detail.json').write_text(json.dumps(tip['detailLines'],ensure_ascii=False),encoding='utf8')
    sizes=[('standard',760,360),('compact',560,280),('tall',360,440),('small',340,210),('strip',460,64)]
    for name,w,h in sizes:
        dpi=state()['panel']['dpi']
        u.SetWindowPos(hwnd,W.HWND(-1),30,30,round(w*dpi),round(h*dpi),0x10)
        time.sleep(.2)
        capture(name)
        s=state()
        assert s['displayPower'][3]['model']=='hardware-reference-v1'
        if s['layout']['tier']!='strip':
            assert sum(h['key'].startswith('core:') for h in s['panel']['hits'])==len(s['cores'])
    # Automatic mode needs no saved numeric references and survives restart.
    command('quit'); p.wait(timeout=10)
    p=subprocess.Popen([str(exe),'--config',str(config_path)],creationflags=subprocess.CREATE_NO_WINDOW)
    hwnd=wait_for(lambda: own('top'))
    wait_for(lambda: state()['sensorState']=='ready')
    assert state()['estimatePower'] and state()['displayPower'][3]['model']=='hardware-reference-v1'
    saved=json.loads(config_path.read_text(encoding='utf8'))
    assert saved.get('platformBaseWatts',-1)==-1 and saved.get('memoryReferenceWatts',-1)==-1
    command('hide'); time.sleep(1.3); assert state()['samplerDemand']==0
    command('show'); time.sleep(1.3); assert state()['samplerDemand']!=0
    result.update(settingsToggle=True, parametersValidated=True, detailText=True, sizeCases=len(sizes), restart=True, hiddenPause=True)
    (out/'result.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf8')
except Exception:
    import traceback
    (out/'error.txt').write_text(traceback.format_exc(),encoding='utf8')
    raise
finally:
    dialog=own('功耗估算参数')
    if dialog: u.SendMessageW(dialog,0x111,2,0)
    if p.poll() is None:
        command('quit'); p.wait(timeout=10)
