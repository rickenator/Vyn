# stdlib_demo

Demonstrates stdlib module discovery with an explicit `import core::math`.

## Run

From the repository root:

```bash
# Option 1: explicit stdlib root
VYB_STDLIB=stdlib ./build/vyb examples/stdlib_demo/main.vyb

# Option 2: executable-relative discovery (works from this repo layout)
./build/vyb examples/stdlib_demo/main.vyb
```

Expected output: `10`
