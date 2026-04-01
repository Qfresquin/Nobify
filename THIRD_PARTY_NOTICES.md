# Third-Party Notices

This repository vendors a small set of third-party single-header libraries.

The notices below are a convenience summary only. The canonical license text for
each vendored component remains in the corresponding source file under
`vendor/`.

## Vendored Components

### `vendor/nob.h`

- upstream: <https://github.com/tsoding/nob.h>
- notice in file: public domain
- local path: `vendor/nob.h`

### `vendor/tinydir.h`

- upstream: <https://github.com/cxong/tinydir>
- notice in file: BSD-style 2-clause license
- local path: `vendor/tinydir.h`

### `vendor/subprocess.h`

- upstream: <https://github.com/sheredom/subprocess.h>
- notice in file: public domain / Unlicense-style notice
- local path: `vendor/subprocess.h`

### `vendor/stb_ds.h`

- upstream: <https://github.com/nothings/stb>
- notice in file: dual-licensed MIT or public domain
- local path: `vendor/stb_ds.h`

## Notes

- If you distribute source, keep the original notices embedded in the vendored
  files.
- If you distribute binaries, include this file or an equivalent notice bundle
  with the distribution materials.
- System packages used in CI or local development, such as `cmake`, `libcurl`,
  and `libarchive`, are not vendored in this repository and remain subject to
  their own upstream licenses.
