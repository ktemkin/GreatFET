/*
 * This file is part of libgreat
 *
 * High-level communications API -- used by all devices that wish to speak
 * the standard communications protocol.
 */

/**
 * Core communications driver for libgreat.
 */
struct comm_driver {

    /** The name of the driver, for e.g. logging. */
    char *name;

    /**
     * Comm drivers are structured in a linked list.
     *
     * This structure always points to the next communciations driver,
     * or NULL if this is the last driver.
     */
    struct comm_driver *next;

};
