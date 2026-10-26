# Verification commands

```sh
python -m pip install -r requirements-dev.txt
python tools/validate_repo.py
python tools/run_host_tests.py
node firmware/tools/booth/test.cjs
node firmware/tools/booth/test_settings.cjs
```

Firmware builds are listed in [firmware/README.md](firmware/README.md). GitHub Actions runs the public export checks, portable C++ suites, sequencer tests, settings tests, and all three firmware builds.

C++ host suites use C++17. On Windows, set `CXX` to a MinGW `g++.exe` when it is not on PATH. The socket/HMAC host suite additionally runs on Windows with CNG; other listed host suites run on Windows and Linux.
