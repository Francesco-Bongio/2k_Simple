# Project: Joint Degree Matrix (JDM) Graph Generation and Verification

This project provides tools to work with **Joint Degree Matrices (JDMs)** in the context of graph theory. It allows you to:

- Generate a **random JDM** using the Erdős-Rényi model (`random_jdm.c`)
- Generate a graph that matches a JDM (`ibrido.c`)
- Compute a JDM from an existing graph (`random_jdm.c` also contains this logic)
- Compare a computed JDM to a reference one (`compare_jdm.c`)

All tools are written in C and rely on the `igraph` and `glib` libraries.

---

## Requirements

- `GLib`
- `igraph`
- `make`

Install required libraries on Debian/Ubuntu with:

```bash
sudo apt install libglib2.0-dev libigraph-dev
```

---

## File Overview

### `random_jdm.c`
Generates a **random undirected graph** using the Erdős-Rényi model and computes its **Joint Degree Matrix (JDM)**.

- Output: a `.nkk` file with the JDM, containing rows of the format:
  ```
  k,l,value
  ```
  where `k` and `l` are degrees and `value` is the number of edges between nodes of degree `k` and `l`.

### `ibrido.c`
Builds a **graph that satisfies a given JDM**. It uses a fast custom graph representation (FastGraph) and tries to match the target matrix.

- Input: a `.nkk` JDM file (from `random_jdm.c` or manually written)
- Output: a graph in edge list format (`generated.graph`)

### `compare_jdm.c`
Checks whether a generated graph truly respects the input JDM.

- Inputs:
  - a `.nkk` file
  - a `.graph` file (edge list)
- Output: prints discrepancies or confirms correctness

### `Makefile`
Provides build commands for all executables:
- `random_jdm`
- `ibrido`
- `compare_jdm`
- and an extra binary `funziona` (not part of the core workflow)

---

## Compilation

To compile all programs:

```bash
make
```

---

## Example Workflow

1. **Generate a random JDM** from an Erdős-Rényi graph:

```bash
./random_jdm 100 0.05 > my_jdm.nkk
```
This creates a 100-node random graph with 5% edge probability and prints the JDM to stdout.

2. **Generate a graph** that matches the JDM:

```bash
./ibrido my_jdm.nkk
```
This creates a graph from the `.nkk` file and writes it to `generated.graph`.

3. **Verify the result**:

```bash
./compare_jdm my_jdm.nkk generated.graph
```
This compares the original JDM with the one computed from the generated graph and reports differences, if any.

---

## File Formats

### `.nkk` (Joint Degree Matrix)
CSV format:
```
k,l,value
```
where `value` is the number of (k,l)-degree node pairs.

### `.graph` (Edge list)
CSV format:
```
u,v
```
Each line represents an undirected edge.

---

## Cleanup

```bash
make clean
```
Removes executables and temporary files.

---

## Author
This project was developed in the context of research for a thesis related to joint degree matrix graph generation.

