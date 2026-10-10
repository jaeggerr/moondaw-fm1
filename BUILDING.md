# Building

## SDL2 and browser (no hardware, no Docker)

```
make sim      # needs SDL2 (pkg-config sdl2)
make web      # needs emcc, or zig
make test
```

Open the browser build from a local server (AudioWorklet needs a secure
context): `python3 -m http.server -d build/web 8791`.

## Firmware

As Felucca (its BUILDING.md has the details), minus Pillow:

- Docker, running. The JieLi toolchain is Linux x86-64 only; each tool runs
  in a `linux/amd64` container.
- `tools/get_toolchain.sh` installs the toolchain to `~/.jieli/toolchain`.
- The JieLi AC79 SDK, for three files of the package:

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the two locations. Outputs in
`build/`: `moondaw.bin` (app), `loader/ota.bin` (update loader),
`moondaw.fwsc` (package).
