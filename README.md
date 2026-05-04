# Final Project — Elevator Operating System Scheduler
**CS4352 Operating Systems | Spring 2026 | Group 9**

---

## Project Overview

This project implements a scheduler for a simulated Elevator Operating System. The simulation environment is provided as a separate process. Our program communicates with it through a defined HTTP-based API to continuously retrieve passenger input, make scheduling decisions, and issue elevator assignments in real time.

The scheduler is written in C/C++ and uses multithreading to coordinate three concurrent responsibilities: input communication, scheduling computation, and output communication.

---

## Group Members

| Name | Role | GitHub |
|------|------|--------|
| Matthew Cabrera | Project Manager | mattcabrera03 |
| Triston Schwab | Lead Developer | D3vilOps |
| Triston Barrientos | QA / Verification Lead | TKB100 |
| Caleb Brasuell | Documentation & Analysis Lead | calebbrasuell1-afk |

---

## How to Compile

> *(To be updated by Lead Developer once initial code is complete)*

```bash
make
```

This will produce an executable named `scheduler_os`.

---

## How to Run

```bash
./scheduler_os <path_to_building_file> <port_number>
```

**Example:**
```bash
./scheduler_os testing.bldg 5432
```

- `path_to_building_file` — path to a `.bldg` configuration file describing the elevator system
- `port_number` — port where the Elevator Operating System API is running

---

## Repository Structure

```
/
├── README.md                        # This file
├── makefile                         # Build configuration
├── QA_Test_Case_Framework.docx      # QA/Verification test plan (Week 1)
├── testing.bldg                     # Testing .bldg file to be used to practice with  (Week 2)
└── Elevator_Scheduler.cpp           # Current .cpp file that will be updated throuhgout progression of project
```

---

## Project Status

| Component | Status |
|-----------|--------|
| Repository setup | ✅ Complete |
| QA Test Case Framework | ✅ Complete |
| Initial code structure | ✅ Complete |
| API communication | ✅ Complete |
| Multithreading implementation | ✅ Complete |
| Scheduler logic | ✅ Complete |
| Testing and validation | 🔄 In Progress |
| Final project report | 🔄 In Progress |

---

## Weekly Check-Ins

Weekly status reports are submitted every Monday to RaiderCanvas by the Project Manager per course requirements.
