# RED / RIO / A-RIO Custom Simulation Guide

This repository contains custom queue disciplines and simulation scripts on top of ns-3.46.1.

## 1) Extra files you added (and where they must be)

Place these files exactly at these paths inside ns-3 root (`ns-3.46.1/`):

- `scratch/red-simulation.cc`
- `scratch/red-wireless-simulation.cc`

- `scratch/rio-initial-simulation.cc`
- `scratch/rio-simulation.cc`
- `scratch/rio-report-simulation.cc`
- `scratch/rio-wireless-simulation.cc`
- `scratch/rio-wireless-report-simulation.cc`

- `scratch/ario-simulation.cc`
- `scratch/ario-report-simulation.cc`
- `scratch/ario-wireless-simulation.cc`
- `scratch/ario-wireless-report-simulation.cc`

- `src/traffic-control/model/rio-queue-disc.h`
- `src/traffic-control/model/rio-queue-disc.cc`
- `src/traffic-control/model/a-rio-queue-disc.h`
- `src/traffic-control/model/a-rio-queue-disc.cc`

## 2) Build-system wiring required

Make sure `src/traffic-control/CMakeLists.txt` includes these files:

- In `SOURCE_FILES`:
  - `model/rio-queue-disc.cc`
  - `model/a-rio-queue-disc.cc`
- In `HEADER_FILES`:
  - `model/rio-queue-disc.h`
  - `model/a-rio-queue-disc.h`

If these entries are missing, ns-3 will compile without your custom queue discs and simulations using `RioQueueDisc` / `ARioQueueDisc` will fail.

## 3) Build commands

From ns-3 root:

```bash
./ns3 configure
./ns3 build
```

Or just run directly (ns-3 auto-builds changed targets):

```bash
./ns3 run "scratch/red-simulation --case=1 --assuredRate=75 --simTime=40"
```

## 4) Run commands

### RED baseline

```bash
./ns3 run "scratch/red-simulation --case=1 --assuredRate=75 --simTime=40"
./ns3 run "scratch/red-simulation --case=3 --assuredRate=100 --simTime=40"
```

### RIO (static)

```bash
./ns3 run "scratch/rio-simulation --case=1 --assuredRate=75 --simTime=40"
./ns3 run "scratch/rio-simulation --case=3 --assuredRate=100 --simTime=40"
```

### A-RIO (adaptive)

```bash
./ns3 run "scratch/ario-simulation --case=1 --assuredRate=75 --simTime=40"
./ns3 run "scratch/ario-simulation --case=3 --assuredRate=100 --simTime=40"
```
Or you can run the scripts that runs these simulation fles for all cases:

```bash
# keep both scripts in the ns3 root
# to get basic infos
./run.sh      

# to get report infos
./run_report.sh
```
## 5) Generate graphs
Keep the python scripts inside the scratch folder. Then run: 
```bash
# go inside the scratch folder
cd scratch

# create a virtual environment (if you already don't have one in the scratch folder)
pip3 -m venv venv

# activate the virtual environment
source venv/bin/activate

# install numpy and matplotlib
pip3 install numpy matplotlib

# now run python scripts
# for basic graphs (avgQ vs assured_rate, discarded_green_ratio vs assured_rate)
python3 generate_graph.py

# Or you can:
chmod +x generate_graph.py
./generate_graph.py

# for report graphs
python3 generate_report_graphs.py

# Or you can:
chmod +x generate_report_graphs.py
./generate_report_graphs.py

```

## 6) Important simulation notes

- `ario-simulation.cc` now uses **per-packet trTCM marking** (`AF11/AF12/AF13`) before enqueue.
- `a-rio-queue-disc.cc` expects DSCP mapping:
  - AF11 -> green (precedence 0)
  - AF12 -> yellow (precedence 1)
  - AF13 -> red (precedence 2)
- For short runs (`simTime <= 20`), throughput/fairness output is not meaningful because warm-up is 20s.

## 7) Common issues

1. `TypeId not found: ns3::RioQueueDisc` / `ns3::ARioQueueDisc`
- Cause: CMake list missing queue-disc source/header registration.
- Fix: verify `src/traffic-control/CMakeLists.txt` entries in section 2.

2. Build succeeds but no drop stats by color
- Cause: DSCP marking path not active in simulation.
- Fix: use the provided `rio-simulation.cc` / `ario-simulation.cc` scripts unchanged.

3. Unrealistic utilization numbers
- Usually due to using too-short `simTime` or comparing pre-warmup windows.

## 8) Suggested commit checklist

- [ ] Queue disc files copied to `src/traffic-control/model/`
- [ ] Simulation files copied to `scratch/`
- [ ] CMake entries present in `src/traffic-control/CMakeLists.txt`
- [ ] At least one RED, one RIO, and one A-RIO run completed successfully
- [ ] Outputs saved (optional: append to CSV/log file)

