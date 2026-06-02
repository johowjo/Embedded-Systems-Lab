PROJECT := WiFi_Client_Server
BUILD_DIR := build

STM32_TOOLCHAIN := /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.0.202411102158/tools/bin/arm-none-eabi-
ifneq ($(wildcard $(STM32_TOOLCHAIN)gcc),)
PREFIX ?= $(STM32_TOOLCHAIN)
else
PREFIX ?= arm-none-eabi-
endif
CC := $(PREFIX)gcc
AS := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
OBJDUMP := $(PREFIX)objdump
SIZE := $(PREFIX)size

MCU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS := -DUSE_HAL_DRIVER -DUSE_STM32L475E_IOT01 -DSTM32L475xx

INCLUDES := \
  -IInc \
  -ICommon/Inc \
  -IDrivers/CMSIS/Include \
  -IDrivers/CMSIS/Device/ST/STM32L4xx/Include \
  -IDrivers/STM32L4xx_HAL_Driver/Inc \
  -IDrivers/BSP/B-L475E-IOT01 \
  -IDrivers/BSP/Components/Common \
  -IDrivers/BSP/Components/lsm6dsl \
  -ISrc/classifier

CFLAGS := $(MCU_FLAGS) $(DEFS) $(INCLUDES) -Og -g3 -Wall -ffunction-sections -fdata-sections
ASFLAGS := $(MCU_FLAGS) $(DEFS) $(INCLUDES) -x assembler-with-cpp -g3
LDSCRIPT := STM32L475VGTX_FLASH.ld
LDFLAGS := $(MCU_FLAGS) -T$(LDSCRIPT) --specs=nosys.specs --specs=nano.specs \
  -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map -Wl,--gc-sections -static \
  -u _printf_float -u _scanf_float -Wl,--start-group -lc -lm -Wl,--end-group

C_SOURCES := \
  Src/main.c \
  Src/classifier/classifier.c \
  Src/stm32l4xx_it.c \
  Src/system_stm32l4xx.c \
  Application/User/syscalls.c \
  Application/User/sysmem.c \
  Common/Src/es_wifi.c \
  Common/Src/es_wifi_io.c \
  Common/Src/wifi.c \
  Drivers/BSP/B-L475E-IOT01/stm32l475e_iot01.c \
  Drivers/BSP/B-L475E-IOT01/stm32l475e_iot01_accelero.c \
  Drivers/BSP/Components/lsm6dsl/lsm6dsl.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_cortex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dma.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_gpio.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c_ex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr_ex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc_ex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_spi.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_spi_ex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart_ex.c

ASM_SOURCES := Application/Startup/startup_stm32l475vgtx.s

OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o)) \
  $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean size

all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).hex $(BUILD_DIR)/$(PROJECT).bin size

$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS) $(LDSCRIPT)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(PROJECT).list: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJDUMP) -h -S $< > $@

size: $(BUILD_DIR)/$(PROJECT).elf
	$(SIZE) $<

clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)
