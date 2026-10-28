"""Package only deliverables and reproducible source; exclude local signing keys."""
from pathlib import Path
import hashlib
import zipfile
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parent
DIST=ROOT/"dist"
apk=DIST/"ftop-xiaomi.apk"
assert apk.is_file(), "Build first: .\\build.ps1"
source=DIST/"ftop-xiaomi-source.zip"
with zipfile.ZipFile(source,"w",zipfile.ZIP_DEFLATED) as z:
    for name in ("AndroidManifest.xml","build.ps1","dependencies.ps1","test.ps1","test-features.ps1","test-network.ps1","package.py","README.md","THIRD-PARTY-NOTICES.txt",".gitignore"):
        z.write(ROOT/name,name)
    for directory in ("src","res","tests"):
        for p in sorted((ROOT/directory).rglob("*")):
            if p.is_file() and "__pycache__" not in p.parts:
                z.write(p,p.relative_to(ROOT))
version=ET.parse(ROOT/"AndroidManifest.xml").getroot().attrib["{http://schemas.android.com/apk/res/android}versionName"]
release=DIST/f"ftop-xiaomi-{version}.zip"
with zipfile.ZipFile(release,"w",zipfile.ZIP_DEFLATED) as z:
    z.write(apk,apk.name)
    z.write(ROOT/"README.md","README.md")
    z.write(ROOT/"THIRD-PARTY-NOTICES.txt","THIRD-PARTY-NOTICES.txt")
    z.write(source,source.name)
checksums=[]
for p in (apk,source,release):
    checksums.append(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}")
    if p.suffix==".zip":
        with zipfile.ZipFile(p) as z:
            assert z.testzip() is None
            assert not any(".local" in n or n.endswith(".p12") for n in z.namelist())
    print(f"{p.name}: {p.stat().st_size:,} bytes")
(DIST/"SHA256SUMS.txt").write_text("\n".join(checksums)+"\n",encoding="ascii")
(DIST/"THIRD-PARTY-NOTICES.txt").write_bytes((ROOT/"THIRD-PARTY-NOTICES.txt").read_bytes())
