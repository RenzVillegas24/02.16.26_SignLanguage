/*
 * @file test_sensors_module.cpp
 * @brief Callable sensor test helpers for the SignGlove GUI test screen.
 */
#include "test_sensors_module.h"

static const char *finger_names[5] = {
    "Thumb", "Index", "Middle", "Ring", "Pinky"
};

void test_sensors_read_once(const SensorData &raw, ProcessedSensorData &pd) {
    sensor_module_process(raw, pd);
    sensor_module_print_serial(pd);
}

void test_sensors_format_flex(const ProcessedSensorData &pd,
                              char *buf, size_t len) {
    int off = 0;
    off += snprintf(buf + off, len - off, "Flex Sensor Test\n\n");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        const char *state;
        if (pd.flex_pct[i] > 0)
            state = "Up";
        else if (pd.flex_pct[i] < 0)
            state = "Down";
        else
            state = "Flat";

        off += snprintf(buf + off, len - off,
                        "%-6s: %4d  %+4d%% %s\n",
                        finger_names[i], pd.flex_raw[i],
                        (int)pd.flex_pct[i], state);
    }
    if (!sensor_module_is_calibrated())
        off += snprintf(buf + off, len - off, "\n(Not calibrated)");
}

void test_sensors_format_hall(const ProcessedSensorData &pd,
                              char *buf, size_t len) {
    int off = 0;
    off += snprintf(buf + off, len - off, "Hall Effect (side)\n\n");
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        const char *state;
        if (pd.hall_pct[i] > 5)
            state = "Front";
        else if (pd.hall_pct[i] < -5)
            state = "Back";
        else
            state = "Normal";

        off += snprintf(buf + off, len - off,
                        "%-6s: %4d  %+4d%% %s\n",
                        finger_names[i], pd.hall_raw[i],
                        (int)pd.hall_pct[i], state);
    }
    if (!sensor_module_is_calibrated())
        off += snprintf(buf + off, len - off, "\n(Not calibrated)");
}

void test_sensors_format_mpu(const ProcessedSensorData &pd,
                             char *buf, size_t len) {
    snprintf(buf, len,
             "MPU6050 Live Data\n\n"
             "Accel:\n"
             "  X: %7.3f m/s2\n"
             "  Y: %7.3f m/s2\n"
             "  Z: %7.3f m/s2\n\n"
             "Gyro:\n"
             "  X: %7.2f d/s\n"
             "  Y: %7.2f d/s\n"
             "  Z: %7.2f d/s\n\n"
             "Pitch: %6.1f\n"
             "Roll:  %6.1f",
             pd.accel_x, pd.accel_y, pd.accel_z,
             pd.gyro_x, pd.gyro_y, pd.gyro_z,
             pd.pitch, pd.roll);
}
