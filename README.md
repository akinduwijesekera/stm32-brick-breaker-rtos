# Brick Breaker — Real-Time Embedded Game on STM32 (FreeRTOS)

A real-time embedded implementation of Brick Breaker on an STM32F401RE microcontroller, built around a FreeRTOS task-oriented architecture. The project focuses on deterministic task scheduling, safe inter-task communication, and real-time performance analysis as much as on the game itself.

---

## Overview

The system runs three FreeRTOS tasks with distinct priorities and responsibilities:

- **Input Task** (high priority, 5 ms period) — samples the analogue joystick via ADC with DMA and reads the push button via GPIO, packaging input into messages for the game logic.
- **Game Task** (normal priority, 16 ms / ~60 Hz period) — owns the game state machine, physics, collision handling, scoring, and difficulty progression.
- **Render Task** (low priority, event-driven) — updates the 128×64 I2C OLED display from a consistent snapshot of game state.

Time-critical input handling is prioritised above rendering, so heavier display operations can never interfere with system responsiveness.

| Task | Priority | Model | Period / Trigger | Stack Size |
|------|----------|-------|-------------------|------------|
| InputTask | High | Periodic | 5 ms | 256 words |
| GameTask | Normal | Periodic | 16 ms | 512 words |
| RenderTask | Low | Event-driven | Semaphore | 512 words |

---

## Hardware

| Component | Interface |
|-----------|-----------|
| STM32F401RE (Nucleo) | Main MCU |
| SSD1306 128×64 OLED | I2C |
| Analogue joystick (X-axis) | ADC + DMA |
| Joystick push button | GPIO (pull-up, edge-detected) |
| UART → host PC | SEGGER SystemView trace streaming |

Joystick sampling runs continuously in DMA mode so ADC conversions never block the Input Task, and a first-order IIR filter smooths the raw signal before it's packaged into the input message.

---

## Inter-Task Communication

FreeRTOS primitives enforce a clear data-ownership model between tasks:

- **Message queue** (`inputQ`) — carries input data from Input Task → Game Task
- **Mutex** (`viewMutex`) — protects the shared render snapshot written by Game Task and read by Render Task
- **Binary semaphore** (`frameSem`) — signals Render Task when a new frame is ready, keeping rendering event-driven rather than polled

Game Task writes a complete, consistent snapshot of render state each update cycle; Render Task copies it under a short mutex hold before rendering from its local copy, minimising lock contention and avoiding flicker from partial updates.

---

## Game Design

- Explicit state machine: start screen → active play → level complete → game over / game complete
- 6 levels with progressively denser brick layouts and multi-hit bricks (1–3 hits)
- Difficulty scales through paddle width reduction (10% per level, clamped at a minimum) and increasing ball speed — avoided adding computational overhead, so difficulty scaling doesn't threaten real-time guarantees

| Level | Bricks | 2-hit | 3-hit | Total Hits | Score Available | Paddle Width |
|-------|--------|-------|-------|------------|------------------|--------------|
| 1 | 13 | 0 | 0 | 13 | 130 | 1.00× |
| 2 | 13 | 1 | 0 | 14 | 135 | 0.90× |
| 3 | 20 | 3 | 0 | 26 | 245 | 0.81× |
| 4 | 22 | 4 | 0 | 30 | 280 | 0.73× |
| 5 | 17 | 5 | 1 | 30 | 255 | 0.66× |
| 6 | 15 | 2 | 5 | 34 | 280 | 0.59× |

---

## Real-Time Performance

Runtime behaviour was analysed using SEGGER SystemView over UART, with a DWT cycle counter providing high-resolution timestamps.

| Task | Priority | Model | Period/Trigger | Min | Median | 75% | Max |
|------|----------|-------|------------------|-----|--------|-----|-----|
| InputTask | High | Periodic | 5 ms | 53.7 µs | 59.6 µs | 62.7 µs | 67.2 µs |
| GameTask | Normal | Periodic | 16 ms | 82.3 µs | 177.0 µs | 182.0 µs | 238.0 µs |
| RenderTask | Low | Event-driven | Semaphore | 0.04 ms | 4.92 ms | 4.94 ms | 4.95 ms |

Key findings:
- Input handling executes in a tightly bounded microsecond-scale window — ADC/DMA sampling imposes negligible CPU overhead
- Game Task runs deterministically within a sub-250 µs window, well inside its 16 ms budget
- Render Task is the longest-running but fully event-driven, so its ~5 ms OLED update never pre-empts higher-priority tasks
- CPU spends the majority of time idle during gameplay, confirming low overall utilisation and real-time margin

---

## Project Structure

Core/ Application source (tasks, game logic, drivers glue)
Drivers/ STM32 HAL, CMSIS, SSD1306 OLED driver
Middlewares/
Third_Party/
FreeRTOS/ RTOS kernel
SEGGER/ SystemView tracing (RTT, SysView)
STM32F401RETX_FLASH.ld
STM32F401RETX_RAM.ld
brickbreaker_backup.ioc STM32CubeMX configuration


---

## Building

This is an STM32CubeIDE project.

1. Clone the repository
2. Open STM32CubeIDE → File → Open Projects from File System → select the cloned folder
3. Build (the `.ioc` file regenerates HAL init code if needed)
4. Flash to an STM32F401RE Nucleo board
5. Wire the SSD1306 OLED (I2C1: PB8/PB9) and joystick (ADC1: PA0/PA1, button: PA6) per the pinout in the project report

For SystemView trace analysis, connect UART2 at 500000 baud and open the trace in SEGGER SystemView.

---

## Design Trade-offs & Limitations

- A multi-task RTOS architecture adds complexity over a single-threaded design but gives modularity and deterministic scheduling
- DMA-based joystick sampling reduces CPU load at the cost of some configuration complexity
- Power-ups, sound, and additional visual effects were intentionally excluded to preserve timing margin and simplicity
- Future extensions (dynamic difficulty, enhanced collision physics, additional input devices) would not require architectural changes given the modular task-based design

---

## Team

Built as coursework for UCL's ELEC0152 — Advanced Embedded Design for IoT Systems.
