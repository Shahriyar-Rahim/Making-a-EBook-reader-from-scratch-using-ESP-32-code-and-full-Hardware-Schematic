#include "power_manager.h"
#include "ui_engine.h"
#include <Wire.h>

#include <driver/adc.h>

// INA219 registers
#define INA219_REG_CONFIG        0x00
#define INA219_REG_SHUNTVOLTAGE  0x01
#define INA219_REG_BUSVOLTAGE    0x02
#define INA219_REG_POWER         0x03
#define INA219_REG_CURRENT       0x04
#define INA219_REG_CALIBRATION   0x05

static bool ina219_initialized = false;
static bool bq25895_initialized = false;
static bool charger_enabled = false;
static int current_charge_limit = 80;
static unsigned long last_runtime_calc = 0;

PowerMetrics power_metrics = {
    .ina219_present = false,
    .bq25895_present = false,
    .charger_control_available = false,
    .battery_voltage = 0.0f,
    .battery_current = 0.0f,
    .battery_power = 0.0f,
    .battery_pct = 0,
    .charging = false,
    .charge_limit = 80,
    .estimated_runtime_minutes = 0,
    .charge_limit_engaged = false,
};

static bool i2c_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static void ina219_write_register(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(INA219_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value >> 8);
    Wire.write(value & 0xFF);
    Wire.endTransmission();
}

static uint16_t ina219_read_register(uint8_t reg) {
    Wire.beginTransmission(INA219_I2C_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)INA219_I2C_ADDRESS, (uint8_t)2);
    uint16_t value = (Wire.read() << 8) | Wire.read();
    return value;
}

static bool init_ina219() {
    if (!i2c_probe(INA219_I2C_ADDRESS)) return false;
    ina219_write_register(INA219_REG_CALIBRATION, 4096);
    ina219_write_register(INA219_REG_CONFIG, 0x019F); // 32V, 320mV, 12-bit
    ina219_initialized = true;
    power_metrics.ina219_present = true;
    return true;
}

static void read_ina219() {
    if (!ina219_initialized) return;
    int16_t shunt = (int16_t)ina219_read_register(INA219_REG_SHUNTVOLTAGE);
    uint16_t bus = ina219_read_register(INA219_REG_BUSVOLTAGE);
    int16_t current = (int16_t)ina219_read_register(INA219_REG_CURRENT);
    int16_t power = (int16_t)ina219_read_register(INA219_REG_POWER);

    power_metrics.battery_voltage = (bus >> 3) * 4.0f / 1000.0f;
    power_metrics.battery_current = current * 0.1f;
    power_metrics.battery_power = power * 2.0f;
}

static bool init_bq25895() {
    if (!i2c_probe(BQ25895_I2C_ADDRESS)) return false;
    bq25895_initialized = true;
    power_metrics.bq25895_present = true;
    power_metrics.charger_control_available = true;
    pinMode(POWER_CHARGER_ENABLE_PIN, OUTPUT);
    digitalWrite(POWER_CHARGER_ENABLE_PIN, HIGH);
    charger_enabled = true;
    return true;
}

static bool read_bq25895() {
    if (!bq25895_initialized) return false;
    return true;
}

bool power_manager_init() {
    Wire.begin(POWER_I2C_SDA_PIN, POWER_I2C_SCL_PIN, 100000);
    bool ok = false;
    if (init_ina219()) ok = true;
    if (init_bq25895()) ok = true;
    power_metrics.charge_limit = current_charge_limit;
    return ok;
}

void power_manager_tick(int adc_pct, bool adc_charging) {
    power_metrics.charging = adc_charging;
    power_metrics.battery_pct = adc_pct;

    if (power_metrics.ina219_present) {
        read_ina219();
        int pct = (int)(((power_metrics.battery_voltage - 3.0f) / 1.2f) * 100.0f);
        power_metrics.battery_pct = constrain(pct, 0, 100);
    }

    if (power_metrics.charger_control_available) {
        if (power_metrics.battery_pct >= power_metrics.charge_limit) {
            power_metrics.charge_limit_engaged = true;
            power_manager_enable_charging(false);
        } else if (adc_charging && !power_metrics.charger_control_available) {
            // no control path, just observe
        } else {
            power_metrics.charge_limit_engaged = false;
            if (!charger_enabled) power_manager_enable_charging(true);
        }
    }

    if (millis() - last_runtime_calc > 10000) {
        last_runtime_calc = millis();
        if (power_metrics.battery_current > 0.1f) {
            float amp = power_metrics.battery_current / 1000.0f;
            float hours = (DEFAULT_BATTERY_CAPACITY_MAH / 1000.0f) / amp;
            power_metrics.estimated_runtime_minutes = (int)(hours * 60.0f);
        } else {
            power_metrics.estimated_runtime_minutes = -1;
        }
    }
}

void power_manager_set_charge_limit(int pct) {
    current_charge_limit = constrain(pct, 50, 100);
    power_metrics.charge_limit = current_charge_limit;
}

int power_manager_get_charge_limit() {
    return current_charge_limit;
}

bool power_manager_enable_charging(bool enable) {
    if (!power_metrics.charger_control_available) return false;
    digitalWrite(POWER_CHARGER_ENABLE_PIN, enable ? HIGH : LOW);
    charger_enabled = enable;
    return true;
}
