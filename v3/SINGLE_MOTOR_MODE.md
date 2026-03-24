# Single Motor Mode Configuration (v3.1.5)

## Status: Only Motor X (Index 0) Active
To focus on the hardware characterization and avoid noise/power issues with the other motor drivers, the system has been restricted to **Motor X** only.

### Changes:
1.  **Hardware Init:** `driverY`, `driverZ`, and `driverE` are still instantiated but their `rms_current` is not set or kept at minimum, and they are not enabled.
2.  **Logic Update:** The `TaskCore1` loop and all UI commands (`homeall`, etc.) only address `steppers[0]`.
3.  **UI:** The motor cards for Y, Z, and E might still be visible but will not respond to commands.

### How to re-enable all motors:
- In `MotorControl.cpp`, uncomment the initialization lines in `initMotors()`.
- In `main_v3.cpp`, change the loop limit in `setup()` and `status` from `1` back to `4`.

---
## Calibration Routine (Robust Version)
The new routine performs a full sweep:
1.  Search Trigger (CW).
2.  Exit Trigger (CW).
3.  Back off 1000 steps.
4.  Search Trigger (CCW).
5.  Exit Trigger (CCW).
6.  Center = Average of both sweeps.
