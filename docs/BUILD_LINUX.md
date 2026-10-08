# Linux/macOS build and run

From the repository root:

```bash
./auto.sh
```

```bash
./auto.sh --config Debug
./auto.sh --client gui
./auto.sh --client cli
./auto.sh --client web
./auto.sh --client server
./auto.sh --client none
./auto.sh --no-tests
./auto.sh --no-run
./auto.sh --use-external-vcpkg
./auto.sh --keep-dependencies
```

The shared browser frontend remains available through Web mode. The current non-Windows GUI target uses the native diagnostic surface; on headless Linux the scripts fall back appropriately.

The C++ project is `projects/native/`; the Web project is `projects/web/`.
