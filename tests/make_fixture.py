from pathlib import Path
import importlib.util
import subprocess
import sys

spec = importlib.util.spec_from_file_location("repair", Path(__file__).parent.parent / "src/repair.py")
repair = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = repair
spec.loader.exec_module(repair)
folder = Path(sys.argv[1])
folder.mkdir(parents=True, exist_ok=True)
healthy = folder / "healthy.webm"
subprocess.run(["ffmpeg", "-nostdin", "-y", "-v", "error", "-f", "lavfi", "-i",
                "sine=frequency=440:sample_rate=48000:duration=0.3", "-ac", "2", "-c:a", "libopus", str(healthy)], check=True)
def strip(payload):
    result = []
    for ident, value in repair.fields(payload):
        if ident in (repair.CRC, repair.SEEKHEAD, repair.CUES): continue
        if ident in (repair.SEGMENT, repair.TRACKS, repair.ENTRY): value = strip(value)
        if ident == 0x63A2: continue
        result.append(repair.encode(ident, value))
    return b"".join(result)
(folder / "broken.webm").write_bytes(strip(healthy.read_bytes()))
