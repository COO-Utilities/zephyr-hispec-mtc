# Thermal Controller Architecture

## System Overview

```mermaid
graph TB
    subgraph "Hardware Layer"
        AD7124[AD7124 ADC<br/>SPI Interface]
        TPS[TPS55287Q1<br/>I2C Interface<br/>Heater Controller]
        RTD[Penguin RTD Sensors]
    end

    subgraph "Application Layer - Main"
        MAIN[main.c<br/>System Initialization<br/>Supervisor Loop]
        SENSOR_THREAD[Sensor Thread<br/>500ms period<br/>Priority 5]
        CONTROL_THREAD[Control Thread<br/>500ms period<br/>Priority 7]
    end

    subgraph "Application Layer - Configuration"
        CONFIG[config.c/h<br/>Configuration Management]
        CONFIG_DATA[Configuration Data<br/>- Sensors<br/>- Heaters<br/>- Control Loops]
    end

    subgraph "Application Layer - Sensors"
        SENSOR_MGR[sensor_manager.c<br/>Multi-Sensor Management]
        ADC_DRIVER[adc_temp_sensor.c<br/>AD7124 Driver<br/>Internal Temp + RTDs]
        SENSOR_CACHE[Sensor Reading Cache<br/>Thread-Safe Mutex]
    end

    subgraph "Application Layer - Heaters"
        HEATER_MGR[heater_manager.c<br/>Multi-Heater Control]
        HEATER_STATE[Heater State Cache<br/>Thread-Safe Mutex]
        TPS_DRV[TPS55287Q1 Driver<br/>TODO: Future]
    end

    subgraph "Application Layer - Control"
        CONTROL_LOOP[control_loop.c<br/>PID Control]
        PID[pid.c<br/>PID Algorithm]
    end

    subgraph "Zephyr RTOS"
        ZEPHYR_SPI[SPI Driver]
        ZEPHYR_I2C[I2C Driver]
        KERNEL[Kernel<br/>Threading, Mutex, Logging]
    end

    %% Main connections
    MAIN -->|initialize| CONFIG
    MAIN -->|initialize| SENSOR_MGR
    MAIN -->|initialize| HEATER_MGR
    MAIN -->|initialize| CONTROL_LOOP
    MAIN -->|creates| SENSOR_THREAD
    MAIN -->|creates| CONTROL_THREAD

    %% Configuration connections
    CONFIG -->|provides| CONFIG_DATA
    CONFIG_DATA -->|used by| SENSOR_MGR
    CONFIG_DATA -->|used by| HEATER_MGR
    CONFIG_DATA -->|used by| CONTROL_LOOP

    %% Sensor thread flow
    SENSOR_THREAD -->|calls every 500ms| SENSOR_MGR
    SENSOR_MGR -->|reads| ADC_DRIVER
    ADC_DRIVER -->|SPI commands| ZEPHYR_SPI
    ZEPHYR_SPI -->|hardware| AD7124
    AD7124 -->|analog input| RTD
    SENSOR_MGR -->|stores| SENSOR_CACHE

    %% Control thread flow
    CONTROL_THREAD -->|calls every 500ms| CONTROL_LOOP
    CONTROL_LOOP -->|reads temp| SENSOR_MGR
    SENSOR_MGR -->|retrieves from| SENSOR_CACHE
    CONTROL_LOOP -->|runs| PID
    PID -->|calculates power| CONTROL_LOOP
    CONTROL_LOOP -->|sets power| HEATER_MGR

    %% Heater control flow
    HEATER_MGR -->|updates| HEATER_STATE
    HEATER_MGR -.->|future| TPS_DRV
    TPS_DRV -.->|I2C commands| ZEPHYR_I2C
    ZEPHYR_I2C -.->|hardware| TPS

    %% Kernel connections
    KERNEL -->|provides| SENSOR_THREAD
    KERNEL -->|provides| CONTROL_THREAD
    KERNEL -->|mutex protection| SENSOR_CACHE
    KERNEL -->|mutex protection| HEATER_STATE

    %% Styling
    classDef hardware fill:#ff9999
    classDef mainApp fill:#99ccff
    classDef config fill:#ffcc99
    classDef sensor fill:#99ff99
    classDef heater fill:#ff99ff
    classDef control fill:#ffff99
    classDef zephyr fill:#cccccc
    classDef future fill:#ffffff,stroke-dasharray: 5 5

    class AD7124,TPS,RTD hardware
    class MAIN,SENSOR_THREAD,CONTROL_THREAD mainApp
    class CONFIG,CONFIG_DATA config
    class SENSOR_MGR,ADC_DRIVER,SENSOR_CACHE sensor
    class HEATER_MGR,HEATER_STATE heater
    class CONTROL_LOOP,PID control
    class ZEPHYR_SPI,ZEPHYR_I2C,KERNEL zephyr
    class TPS_DRV future
```

## Thread Architecture

```mermaid
sequenceDiagram
    participant Main as Main Thread<br/>(Supervisor)
    participant Sensor as Sensor Thread<br/>(500ms, Priority 5)
    participant Control as Control Thread<br/>(500ms, Priority 7)
    participant SensorMgr as Sensor Manager
    participant HeaterMgr as Heater Manager
    participant ControlLoop as Control Loop

    Main->>Main: Initialize configuration
    Main->>SensorMgr: sensor_manager_init()
    Main->>HeaterMgr: heater_manager_init()
    Main->>ControlLoop: control_loop_init()
    Main->>Sensor: Create thread
    Main->>Control: Create thread

    loop Every 500ms
        Sensor->>SensorMgr: sensor_manager_read_all()
        SensorMgr->>SensorMgr: Read AD7124 via SPI
        SensorMgr->>SensorMgr: Update sensor cache (mutex)
    end

    loop Every 500ms
        Control->>ControlLoop: control_loop_update_all()
        ControlLoop->>SensorMgr: get sensor readings
        SensorMgr->>SensorMgr: Read from cache (mutex)
        SensorMgr-->>ControlLoop: temperature value
        ControlLoop->>ControlLoop: Run PID algorithm
        ControlLoop->>HeaterMgr: heater_manager_set_power()
        HeaterMgr->>HeaterMgr: Update heater state (mutex)
        HeaterMgr->>HeaterMgr: Set hardware output
    end

    loop Every 100ms
        Main->>Main: monitor_system_health()
        Main->>Main: Check for alarms
        Main->>Main: Handle mode changes
    end
```

## File Structure

```mermaid
graph LR
    subgraph "app/"
        APP_MAIN[src/main.c]
        APP_OVERLAY[boards/nucleo_h563zi.overlay]
        APP_CONF[prj.conf]
        APP_KCONFIG[Kconfig]
    end

    subgraph "lib/"
        subgraph "config/"
            CONFIG_H[config.h]
            CONFIG_C[config.c]
        end

        subgraph "sensors/"
            SENSOR_MGR_H[sensor_manager.h]
            SENSOR_MGR_C[sensor_manager.c]
            ADC_H[adc_temp_sensor.h]
            ADC_C[adc_temp_sensor.c]
        end

        subgraph "heaters/"
            HEATER_MGR_H[heater_manager.h]
            HEATER_MGR_C[heater_manager.c]
        end

        subgraph "control/"
            LOOP_H[control_loop.h]
            LOOP_C[control_loop.c]
        end

        subgraph "coo_commons/"
            PID_H[pid.h]
            PID_C[pid.c]
        end
    end

    subgraph "include/"
        COMMON_H[coo_commons/*.h]
    end

    APP_MAIN -->|uses| CONFIG_H
    APP_MAIN -->|uses| SENSOR_MGR_H
    APP_MAIN -->|uses| HEATER_MGR_H
    APP_MAIN -->|uses| LOOP_H

    SENSOR_MGR_C -->|uses| CONFIG_H
    SENSOR_MGR_C -->|uses| ADC_C

    HEATER_MGR_C -->|uses| CONFIG_H

    LOOP_C -->|uses| CONFIG_H
    LOOP_C -->|uses| SENSOR_MGR_H
    LOOP_C -->|uses| HEATER_MGR_H
    LOOP_C -->|uses| PID_C
```

## Data Flow - Temperature Control

```mermaid
flowchart TD
    START([System Start]) --> INIT[Load Configuration]
    INIT --> INIT_HW[Initialize Hardware<br/>AD7124, I2C]
    INIT_HW --> CREATE_THREADS[Create Threads]
    CREATE_THREADS --> RUNNING{System Running?}

    RUNNING -->|Yes| SENSOR_READ[Sensor Thread:<br/>Read AD7124]
    SENSOR_READ --> STORE_TEMP[Store Temperature<br/>in Cache]
    STORE_TEMP --> WAIT_SENSOR[Wait 500ms]
    WAIT_SENSOR --> RUNNING

    RUNNING -->|Yes| GET_TEMP[Control Thread:<br/>Get Temperature<br/>from Cache]
    GET_TEMP --> CALC_ERROR[Calculate Error<br/>Setpoint - PV]
    CALC_ERROR --> RUN_PID[Run PID Algorithm<br/>P + I + D terms]
    RUN_PID --> CALC_POWER[Calculate Power Output<br/>0-100%]
    CALC_POWER --> CLAMP[Clamp to Limits<br/>min/max power]
    CLAMP --> SET_HEATER[Set Heater Power]
    SET_HEATER --> LOG_STATUS[Log Loop Status]
    LOG_STATUS --> WAIT_CONTROL[Wait 500ms]
    WAIT_CONTROL --> RUNNING

    RUNNING -->|Yes| HEALTH[Supervisor:<br/>Monitor Health]
    HEALTH --> CHECK_ALARM{Alarm Condition?}
    CHECK_ALARM -->|Yes| ESTOP[Emergency Stop<br/>All Heaters Off]
    CHECK_ALARM -->|No| WAIT_SUPER[Wait 100ms]
    ESTOP --> WAIT_SUPER
    WAIT_SUPER --> RUNNING

    RUNNING -->|No| SHUTDOWN[Shutdown:<br/>Stop All Heaters]
    SHUTDOWN --> END([Exit])
```

## Configuration Structure

```mermaid
classDiagram
    class thermal_config_t {
        +char id
        +controller_mode_t mode
        +temp_unit_t units
        +int number_of_sensors
        +int number_of_heaters
        +int number_of_control_loops
        +uint32_t timeout_seconds
        +sensor_config_t sensors[]
        +heater_config_t heaters[]
        +control_loop_config_t control_loops[]
    }

    class sensor_config_t {
        +char id
        +sensor_type_t type
        +char location
        +float default_value
        +float temperature_at_default
        +float temperature_coefficient
        +bool enabled
    }

    class heater_config_t {
        +char id
        +heater_type_t type
        +char location
        +float max_power_w
        +bool enabled
    }

    class control_loop_config_t {
        +char id
        +char sensor_ids[]
        +char heater_ids[]
        +float default_target_temperature
        +control_algo_t control_algorithm
        +float p_gain, i_gain, d_gain
        +float alarm_min_temp, alarm_max_temp
        +float heater_power_limit_min/max
        +bool enabled
    }

    thermal_config_t "1" *-- "1..16" sensor_config_t
    thermal_config_t "1" *-- "1..16" heater_config_t
    thermal_config_t "1" *-- "1..8" control_loop_config_t
```

## Hardware Connections

```mermaid
graph LR
    subgraph "NUCLEO-H563ZI Board"
        MCU[STM32H563ZI MCU]
    end

    subgraph "SPI1 Bus"
        SPI_SCK[PA5 - SPI1_SCK]
        SPI_MISO[PG9 - SPI1_MISO]
        SPI_MOSI[PB5 - SPI1_MOSI]
        SPI_CS[PD14 - SPI1_CS]
    end

    subgraph "I2C1 Bus (Future)"
        I2C_SCL[PB8 - I2C1_SCL]
        I2C_SDA[PB9 - I2C1_SDA]
    end

    MCU --> SPI_SCK
    MCU --> SPI_MISO
    MCU --> SPI_MOSI
    MCU --> SPI_CS
    MCU -.-> I2C_SCL
    MCU -.-> I2C_SDA

    SPI_SCK --> AD7124_CHIP[AD7124 ADC]
    SPI_MISO --> AD7124_CHIP
    SPI_MOSI --> AD7124_CHIP
    SPI_CS --> AD7124_CHIP

    AD7124_CHIP -->|AIN0-AIN15| RTD_SENSORS[Penguin RTD Sensors<br/>Future]
    AD7124_CHIP -->|Internal| CHIP_TEMP[Chip Temp Sensor<br/>Testing Only]

    I2C_SCL -.-> TPS_CHIP[TPS55287Q1<br/>Buck-Boost Converter<br/>Future]
    I2C_SDA -.-> TPS_CHIP
    TPS_CHIP -.-> HEATER[High Power Heater Element]
```

## Key Design Decisions

**Thread Separation**: Sensor reading and control logic in separate threads for better real-time performance
**Thread-Safe Caching**: Mutex-protected sensor and heater state caches for safe cross-thread access
**Modular Design**: Clear separation between config, sensors, heaters, and control logic
**500ms Update Rate**: Both sensor reading and control updates synchronized at 2 Hz
