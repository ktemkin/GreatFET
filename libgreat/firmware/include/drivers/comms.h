/*
 * This file is part of libgreat
 *
 * High-level communications API -- used by all devices that wish to speak
 * the standard communications protocol.
 */

/**
 * Structure describing the various operations that can be performed by a 
 * (conceptual) pipe.
 */ 
struct comms_pipe_ops {

    /**
     * Handle data being recieved from the host.
     *
     * @param verb -- The verb, if this is a shared pipe. For a dedicate pipe,
     *      this value is always zero.
     * @param data_in -- Pointer to the block of data recieved.
     * @param length -- The length of the data recieved.
     *
     * @return 0 on success, or an error code on failure
     *      Not all tranports will respect error codes, for now.
     */
    int (*handle_data_in)(uint32_t verb, void *data_in, uint32_t length);


    /**
     * Handles an indication that the host is ready to recieve data.
     *
     * @param verb -- The verb, if this is a shared pipe. For a dedicate pipe,
     *      this value is always zero.
     */
    void (*handle_host_ready_for_data)(uint32_t verb);


    /**
     * Handles completion of a transmission on a pipe. This callback gives
     * us the ability to free data after use, if desired.
     */
    int (*handle_data_out_complete)(void *data, uint32_t length);
};


/**
 * Data structure that describes a libgreat class.
 */
struct comms_class_descriptor {
    
    /**
     * The number for the provided class. These should be reserved
     * on the relevant project's wiki.
     */
    uint32_t class_number;

};


/**
 * Object describing a communications class.
 */
struct comms_class {

};

/**
 * Object describing a communications pipe.
 */
struct comms_pipe {
    

};


/**
 * Callback types for commands registered by communications backends.
 *
 * @param verb -- The verb number for the given class. Allows each class
 *      to provide more than one function, but with logical grouping.
 * @param data_in -- Pointer to a byte buffer that provides the command's
 *      input, or NULL if no input is being provided.
 * @param data_in_length -- The length of the data provided in the data_in
 *      buffer. Must be 0 if data_in is null.
 * @param data_out -- Pointer to a byte buffer that will recieve the command's
 *      output, or NULL if no output is accepted.
 * @param data_out_max_length -- The maximum response length that the device is
 *      willing to accept. This must be 0 if data_out is null.
 * @param data_out_length -- Out argument that accepts the amount of data
 *      to be returned by the given command. Must be <= data_out_max_length.
 *
 * @return 0 if the operation went successfully, or an error code on failure.
 *      This will be converted to a protocol-specific response, and the error
 *      code may not be conveyed.
 */
typedef int (*command_handler)(uint32_t verb, void *data_in, uint32_t data_in_length,
        void *data_out, uint32_t data_out_max_length, uint32_t *data_out_length);


/**
 * Registers a given class for use with libgreat; which implicitly provides it
 * with an ability to handle commands.
 *
 * @param class_descriptor A descriptor describing the class to be created.
 * @param command_callback A callback function used to execute commands for
 *      the class; or NULL if the class proides pipes only.
 *
 * @return a comms_class object on success; or NULL on failure
 */
struct comms_class *comms_register_class(struct comms_class_descriptor class_descriptor, 
        command_handler command_callback);


/**
 * Registers a pipe to be provided for a given class, which allows 
 * bulk bidirectional communications.
 *
 * @param class_number -- The number for the class for which the pipe is
 *      to be associated. This must have already been registered with
 *      register_class.
 * @param flags -- Flags describing how this pipe is to operate. TBD.
 * @param ops -- A structure defining the operations this pipe supports.
 *
 * @returns a comms_pipe object on success; or NULL on failure
 */
struct comms_pipe *comms_register_pipe(struct comms_class *owning_class, 
        uint32_t flags, struct comms_pipe_ops ops);


/**
 * Transmits data on a given communications pipe.
 *
 * @param pipe The pipe on which to transmit.
 * @param 
 */ 
int comms_send_on_pipe(struct comms_pipe *pipe, void *data, uint32_t length);


/**
 * @return True iff the given comms pipe is ready for data transmission.
 */ 
bool comms_pipe_ready(struct comms_pipe *pipe);
