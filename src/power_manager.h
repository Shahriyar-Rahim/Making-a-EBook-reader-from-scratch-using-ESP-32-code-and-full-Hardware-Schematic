#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>

#define POWER_I2C_SDA_PIN         32
#define POWER_I2C_SCL_PIN         33
#define POWER_CHARGER_ENABLE_PIN  27
#define INA219_I2C_ADDRESS        0x40
#define BQ25895_I2C_ADDRESS       0x6B
#define DEFAULT_BATTERY_CAPACITY_MAH 1500

struct PowerMetrics {
    bool  ina219_present;
    bool  bq25895_present;
    bool  charger_control_available;
    float battery_voltage;
    float battery_current;
    float battery_power;
    int   battery_pct;
    bool  charging;
    int   charge_limit;
    int   estimated_runtime_minutes;
    bool  charge_limit_engaged;
};

extern PowerMetrics power_metrics;

bool power_manager_init();
void power_manager_tick(int adc_pct, bool adc_charging);
void power_manager_set_charge_limit(int pct);
int  power_manager_get_charge_limit();
bool power_manager_enable_charging(bool enable);

#endif // POWER_MANAGER_H
