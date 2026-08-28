// Mapping.h — turns raw controller values into drive commands.
//
// Deliberately free of any BLE or hardware dependency, so the response curves
// can be tuned without touching the radio code.
#pragma once

#include <Arduino.h>

// Trigger (0..1023) to 0.0..1.0.
float normalizeTrigger(uint16_t raw);

// Stick (0..65535, centre ~32768) to -1.0..+1.0.
float normalizeStick(uint16_t raw);

// Values below the threshold become 0; the rest is stretched back over the
// full range. Without that rescaling the value would jump from 0 to the
// deadzone size the moment the stick leaves the centre.
float applyDeadzone(float value, float deadzone);

// y = k*x^3 + (1-k)*x. Fine control around centre, full travel preserved.
float applyExpo(float value, float k);

// Result of evaluating the triggers.
struct DriveCommand {
  int8_t targetSpeed;  // -maxSpeed .. +maxSpeed
  bool   brake;        // sets bit 0 of the drive frame's last byte
};

// RT is throttle, LT is reverse, both together brake.
//
// Braking and negative speed are mutually exclusive: with the brake engaged
// the hub ignores the speed value entirely.
DriveCommand mapTriggers(uint16_t trigRT, uint16_t trigLT, int8_t maxSpeed);

// Left stick to -maxSteer..+maxSteer, with deadzone and expo applied.
int8_t mapStickToSteering(uint16_t joyLHori, int8_t maxSteer);
