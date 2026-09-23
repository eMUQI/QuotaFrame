"""Export website device metadata without downloading firmware."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bridge" / "src"))
from quotaframe_bridge.targets import TARGETS

if __name__ == "__main__":
    print(json.dumps({"targets": [
        {"id": target.id, "name": target.web_flash.name,
         "description": target.web_flash.description, "asset": target.web_flash.asset}
        for target in TARGETS if target.web_flash is not None
    ]}))
