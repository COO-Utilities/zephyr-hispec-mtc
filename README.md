# Zephyr HISPEC MTC (Multichannel Temperature Controller)

## Quick Start

```bash
# Create and activate virtual environment
python3 -m venv ~/zephyr-venv
source ~/zephyr-venv/bin/activate  # On Windows: ~/zephyr-venv/Scripts/activate

# Install west
pip install west

# Create workspace
west init -m https://github.com/COO-Utilities/zephyr-hispec-mtc.git --mr main my-project
cd my-project && west update

# Build for hardware
west build -b nucleo_h563zi demo --pristine
west flash
```
## Links

- [Zephyr Documentation](https://docs.zephyrproject.org/)
- [West Tool](https://docs.zephyrproject.org/latest/develop/west/index.html)
- [Device Tree Guide](https://docs.zephyrproject.org/latest/build/dts/index.html)
