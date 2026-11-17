"""Interface JSON para o motor de reparação já validado, sem interface gráfica."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys

spec = importlib.util.spec_from_file_location("webm_repair", Path(__file__).with_name("repair.py"))
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

try:
    for command in ("ffmpeg", "ffprobe"):
        if not shutil.which(command):
            raise module.RepairError("Falta instalar " + command + ".")
    output = Path(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else None
    result = module.repair(Path(sys.argv[2]), output, check_only=sys.argv[1] == "check")
except Exception as error:
    result = {"estado": "erro", "motivo": str(error)}
print(json.dumps(result, ensure_ascii=False), flush=True)
sys.exit(1 if result["estado"] == "erro" else 0)
