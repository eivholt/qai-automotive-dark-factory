"""Render G/A/R/disconnected checks through a temporary loopback MOCK bridge."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
from dark_factory.bridge import BridgeConfig, BridgeServer


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, default=Path("C:/Program Files/Epic Games/UE_5.8"))
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/gpu-bridge-validation")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = args.engine_root / "Engine/Binaries/Win64/UnrealEditor.exe"
    results = []
    for decision in ("G", "A", "R", "ERROR"):
        print(f"Rendered {decision} check: starting temporary loopback mock", flush=True)
        server = BridgeServer(BridgeConfig(port=0, mock_decisions=(decision if decision != "ERROR" else "A",)))
        port = server.server_address[1]
        thread = None
        if decision == "ERROR":
            server.server_close()  # No listening backend for the disconnected case.
        else:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
        try:
            command = [str(executable), str(ROOT / "unreal/DarkFactory.uproject"), "-game", "-windowed",
                       "-ResX=1600", "-ResY=900", "-NoSplash", "-DFBridgeSmoke", f"-DFExpected={decision}",
                       f"-DFEndpoint=http://127.0.0.1:{port}/inspect", "-DFSeed=42"]
            with (args.output / f"{decision}.log").open("w", encoding="utf-8") as log:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=90, check=True)
            report = json.loads((ROOT / f"unreal/Saved/Smoke/bridge_{decision}.json").read_text(encoding="utf-8-sig"))
            log = (ROOT / "unreal/Saved/Logs/DarkFactory.log").read_text(encoding="utf-8", errors="replace")
            stats = re.search(r"DF_CAPTURE_STATS gpu_preview=(\d+) async_readbacks=(\d+) sync_readbacks=(\d+) stale_discarded=(\d+) cpu_preview_uploads=(\d+)", log)
            if not stats:
                raise RuntimeError(f"{decision}: missing current capture statistics")
            report["capture_stats"] = dict(zip(("gpu_preview", "async_readbacks", "sync_readbacks", "stale_discarded", "cpu_preview_uploads"), map(int, stats.groups())))
            values = report["capture_stats"]
            report["live_gpu_path_passed"] = values["gpu_preview"] > 0 and values["async_readbacks"] > 0 and values["sync_readbacks"] == 0 and values["cpu_preview_uploads"] == 0
            (args.output / f"{decision}.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            results.append(report)
            if not report["passed"] or not report["live_gpu_path_passed"]:
                raise RuntimeError(f"{decision}: live bridge/capture check failed")
            print(f"Rendered {decision}: passed; asynchronous readbacks={values['async_readbacks']}", flush=True)
        finally:
            if thread:
                server.shutdown()
                thread.join(timeout=5)
                server.server_close()
    (args.output / "summary.json").write_text(json.dumps({"passed": True, "scope": "Local mock plumbing only; no Cosmos or IQ9 inference", "checks": results}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
