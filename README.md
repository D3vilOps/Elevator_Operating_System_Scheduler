# Final Project — Elevator Operating System Scheduler
**CS4352 Operating Systems | Spring 2026 | Group 9**

---

## Project Overview

This is a fork of the orignal assignment project for CS4352 Operating Systems project for Eric Rees from 2026 Spring Semester for group 9. This is my personal improvement work of the project after the fact for my own practice and learning as the lead developer of the original project.

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

```bash
make
```

This will produce an executable named `scheduler_os`.

---

## How to Run

Runs on the Texas Tech high powered perfomance center. 

```bash
sbatch /lustre/work/errees/courses/cs4352/final_project/Elevator_OS/submission_scripts/highrise_busy_grader.sh
```

**Access Grader:**

```bash
cat grader.log
```

---

