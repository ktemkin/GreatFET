/**
 * This file is part of libgreat
 *
 * LPC43xx reset generation/control driver
 */

#include <drivers/platform_reset.h>

/**
 * Return a reference to the LPC43xx's RGU block.
 */
platform_reset_register_block_t *get_platform_reset_registers(void)
{
	return (platform_reset_register_block_t *)0x40053000;
}


/**
 * Returns a reference to the LPC43xx's watchdog timer block.
 */
platform_watchdog_register_block_t *get_platform_watchdog_registers()
{
	return (platform_watchdog_register_block_t *)0x40080000;
}


/**
 * Reset everything except for the always-on / RTC power domain.
 */
static void platform_core_reset(void)
{
	platform_reset_register_block_t *rgu = get_platform_reset_registers();
	rgu->core_reset = 1;
}


/**
 * Feed the platform's watchdog timer, noting that the system is still alive.
 */
void platform_watchdog_feed(void)
{
	platform_watchdog_register_block_t *wwdt = get_platform_watchdog_registers();

	// Issue the write sequence that should feed the watchdog.
	wwdt->feed = 0xAA;
	wwdt->feed = 0x55;
}


/**
 * Reset everything including the always-on / RTC power domain.
 */
static void platform_watchdog_reset(void)
{
	const uint32_t default_watchdog_timeout = 100000;

	platform_watchdog_register_block_t *wwdt = get_platform_watchdog_registers();

	wwdt->enable = 1;
	wwdt->reset_enable = 1;
	wwdt->timeout = default_watchdog_timeout;

	platform_watchdog_feed();
}


/**
 * Software reset the entire system.
 *
 * @param true iff the always-on power domain should be included
 */
void platform_software_reset(bool include_always_on_domain)
{
	if (include_always_on_domain) {
		platform_watchdog_reset();
	} else {
		platform_core_reset();
	}
}
