# GPU Fan Control

Simple program to control NVIDIA GPU fan speeds based on temperature using a piecewise linear curve.
Requirements: 
  - NVML header and lib file (your distro's cuda package most likely includes these)

## Building

Example compile command:
```bash
gcc -o gpu-fan-control main.c -I/opt/cuda/include -lnvidia-ml
```

You may need to adjust the include path (`-I`) and library paths based on your system's CUDA installation.

## Usage

```bash
sudo ./gpu-fan-control [-p poll_rate_ms] temp1 fan1 temp2 fan2 [temp3 fan3 ...]
```

Arguments:
- `-p poll_rate_ms`: Optional temperature polling rate in milliseconds (default: 1000)
- `tempX`: Temperature point in Celsius
- `fanX`: Fan speed percentage (0-100) for corresponding temperature

Example:
```bash
sudo ./gpu-fan-control 30 30 50 60 70 100
```
This creates a curve where:
- At 30°C and below: 30% fan speed
- At 50°C: 60% fan speed
- At 70°C and above: 100% fan speed

