import os
import runpy
import sys


def main() -> int:
    # Compatibility wrapper: keep imports working for older experiments.
    script = os.path.join(os.path.dirname(__file__), "material", "atlas_material.py")
    sys.argv[0] = script
    runpy.run_path(script, run_name="__main__")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


