#ifndef AL_CORE_H
#define AL_CORE_H

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * The USB VBUS monitoring pin.
 */
#define AL_USB_MON 18
#define AL_GPIO_A 3
#define AL_GPIO_B 10

/**
 * The available triggers that can wake the device.
 */
typedef enum {
  AL_RESET,     /**< The device was reset. */
  AL_TIMEOUT,   /**< The device woke up after a timeout. */
  AL_BUTTON,    /**< The device woke up due to a button press. */
  AL_INTERRUPT, /**< The device woke up due to a motion or power interrupt. */
} al_trigger_t;

/**
 * Initializes the Air Lab.
 *
 * @return The trigger that woke the device.
 */
al_trigger_t al_init();

/**
 * Puts the device into light/deep sleep for the specified timeout or indefinitely.
 *
 * @note: Even if deep sleep is requested the device might choose to enter another
 * mode of sleep and just return from this function.
 *
 * @param deep Whether to enter deep sleep.
 * @param timeout The timeout in milliseconds or 0 for indefinite sleep.
 * @return The trigger that woke the device.
 */
al_trigger_t al_sleep(bool deep, uint64_t timeout);

/**
 * Allocates memory from external RAM with the specified size. The function will
 * abort if the allocation fails.
 *
 * @param size The number of bytes.
 * @return Pointer to the allocated memory.
 */
void* al_alloc(size_t size);

/**
 * Allocates memory from external RAM for an array of elements of a specified
 * size and initializes each byte to zero. The function will abort if the
 * allocation fails.
 *
 * @param count Number of elements to allocate.
 * @param size Size of each element in bytes.
 * @return Pointer to the allocated memory.
 */
void* al_calloc(size_t count, size_t size);

/**
 * Performs an I2C transfer to the specified address with the provided data.
 *
 * @param addr The I2C address.
 * @param tx Pointer to the data to transmit or NULL.
 * @param tx_len Number of bytes to transmit.
 * @param rx Pointer to the buffer to receive data or NULL.
 * @param rx_len Number of bytes to receive.
 * @param timeout Timeout in milliseconds.
 * @return ESP_OK on success or an error code on failure.
 */
esp_err_t al_i2c_transfer(uint8_t addr, uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, int timeout);

#endif  // AL_CORE_H
