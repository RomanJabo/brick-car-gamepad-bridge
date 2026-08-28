#include "Mapping.h"

#include <XboxControllerNotificationParser.h>

#include "Config.h"

float normalizeTrigger(uint16_t raw) {
  const float maxTrig = (float)XboxControllerNotificationParser::maxTrig;  // 0x3FF
  float v = (float)raw / maxTrig;
  return constrain(v, 0.0f, 1.0f);
}

float normalizeStick(uint16_t raw) {
  const float maxJoy = (float)XboxControllerNotificationParser::maxJoy;  // 0xFFFF
  const float center = maxJoy / 2.0f;
  float v = ((float)raw - center) / center;
  return constrain(v, -1.0f, 1.0f);
}

float applyDeadzone(float value, float deadzone) {
  const float magnitude = fabsf(value);
  if (magnitude <= deadzone) return 0.0f;
  // Stretch back over the full range, otherwise the value would jump from 0
  // to the deadzone size at its edge.
  const float scaled = (magnitude - deadzone) / (1.0f - deadzone);
  return (value < 0.0f) ? -scaled : scaled;
}

float applyExpo(float value, float k) {
  return k * value * value * value + (1.0f - k) * value;
}

DriveCommand mapTriggers(uint16_t trigRT, uint16_t trigLT, int8_t maxSpeed) {
  const float forward = applyDeadzone(normalizeTrigger(trigRT), TRIGGER_DEADZONE);
  const float reverse = applyDeadzone(normalizeTrigger(trigLT), TRIGGER_DEADZONE);

  DriveCommand cmd{0, false};

  if (forward > 0.0f && reverse > 0.0f) {
    cmd.brake = true;  // both triggers pressed: unambiguously braking
    return cmd;
  }

  if (forward > 0.0f) {
    const int value = (int)lroundf(forward * (float)maxSpeed);
    cmd.targetSpeed = (int8_t)constrain(value, 0, (int)maxSpeed);
    return cmd;
  }

  if (reverse > 0.0f) {
    const int value = (int)lroundf(reverse * (float)maxSpeed);
    cmd.targetSpeed = (int8_t)-constrain(value, 0, (int)maxSpeed);
    return cmd;
  }

  return cmd;  // neither pressed: coast
}

int8_t mapStickToSteering(uint16_t joyLHori, int8_t maxSteer) {
  float v = applyDeadzone(normalizeStick(joyLHori), STICK_DEADZONE);
  v       = applyExpo(v, STEER_EXPO);
  const int value = (int)lroundf(v * (float)maxSteer);
  return (int8_t)constrain(value, -(int)maxSteer, (int)maxSteer);
}
