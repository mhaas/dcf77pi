/*
Copyright (c) 2013-2014 René Ladan. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

#ifndef DCF77PI_INPUT_H
#define DCF77PI_INPUT_H

/** this bit has value 1 */
#define GETBIT_ONE	(1 << 0)
/** end-of-minute bit */
#define GETBIT_EOM	(1 << 1)
/** end of data, either end-of-file or user quit */
#define GETBIT_EOD	(1 << 2)
/** bit value could not be determined */
#define GETBIT_READ	(1 << 3)
/** bit buffer would overflow */
#define GETBIT_TOOLONG	(1 << 4)
/** I/O error while reading bit from hardware */
#define GETBIT_IO	(1 << 5)
/** transmitter error, all positive pulses */
#define GETBIT_XMIT	(1 << 6)
/** receiver error, all negative pulses */
#define GETBIT_RECV	(1 << 7)
/** random radio error, both positive and negative pulses but no proper
  * signal */
#define GETBIT_RND	(1 << 8)
/** this bit should be skipped (i.e. not displayed) */
#define GETBIT_SKIP	(1 << 9)
/** next bit should be skipped (i.e. not added to bitpos) */
#define GETBIT_SKIPNEXT	(1 << 10)

#include <stdint.h> /* uintX_t */

/**
 * Hardware parameters:
 */
struct hardware {
	/** sample frequency in Hz */
	uint32_t freq;
	/* pin number to read from */
	uint8_t pin;
	/** pin value is high (1) or low (0) for active signal */
	uint8_t active_high;
};

/**
 * (Internal) information about the currently received bit:
 */
struct bitinfo {
	/** time in samples when the signal went low again */
	int64_t tlow;
	/** time in samples when the signal was last measured as 0 */
	int64_t tlast0;
	/** length of this bit in samples */
	int64_t t;
	/** the average length of a bit in samples */
	int64_t realfreq;
	/** the average length of the high part of bit 0 (a 0 bit) in
	  * samples */
	int64_t bit0;
	/** the average length of the high part of bit 20 (a 1 bit) in
	  * samples */
	int64_t bit20;
	/** bit0 and bit20 were reset to their initial values (normally because
	  * of reception errors or fluctuations in CPU usage) */
	int bitlen_reset;
	/** realfreq was reset to {@link hardware.freq} (normally because of
	  * reception errors or fluctuations in CPU usage) */
	int freq_reset;
	/** the raw received radio signal, {@link hardware.freq} / 2 items, with
	  * each item holding 8 bits */
	uint8_t *signal;
};

/**
 * Prepare for input from a log file.
 *
 * @param infilename The name of the log file to use.
 * @return Preparation was succesful (0), errno otherwise.
 */
int set_mode_file(char *infilename);

/**
 * Prepare for live input.
 *
 * The sample rate is set to {@link hardware.freq} Hz, reading from pin
 * {@link hardware.pin} using {@link hardware.active_high} logic.
 *
 * @return Preparation was succesful (0), -1 or -errno otherwise.
 */
int set_mode_live(void);

/**
 * Return the hardware paramters.
 *
 * @return The hardware parameters.
 */
struct hardware *get_hardware_parameters(void);

/**
 * Clean up when: close the device or input logfile, and output log file if
 * applicable.
 */
void cleanup(void);

/**
 * Retrieve one pulse from the hardware.
 *
 * @return: 0 or 1 depending on the pin value and {@link hardware.active_high},
 *   or GETBIT_IO if obtaining the pulse failed.
 */
uint8_t get_pulse(void);

/**
 * Retrieve one bit from the log file.
 *
 * @param acc_minlen Pointer to the accumulated minute length, if given by the
 *   log file.
 * @return The current bit from the log file, a mask of GETBIT_* values.
 */
uint16_t get_bit_file(int *acc_minlen);

/**
 * Retrieve one live bit from the hardware. This function determines several
 * values which can be retrieved using get_bitinfo().
 *
 * @param acc_minlen Pointer to the accumulated minute length to store in the
 *   log file (NULL means no storage, e.g. for readpin).
 * @return The currently received bit, a mask of GETBIT_* values.
 */
uint16_t get_bit_live(int *acc_minlen);

/**
 * Prepare for the next bit: update the bit position or wrap it around.
 *
 * @return The current bit state mask, ORed with GETBIT_TOOLONG if the bit
 *   buffer just became full.
 */
uint16_t next_bit(void);

/**
 * Retrieve the current bit position.
 *
 * @return The current bit position (0..60).
 */
uint8_t get_bitpos(void);

/**
 * Retrieve the current bit buffer.
 *
 * @return The bit buffer, a packed array of 0 and 1 values (8 bits per item).
 */
uint8_t *get_buffer(void);

/**
 * Determine if there should be a space between the last bit and the current bit
 * when displaying the bit buffer.
 *
 * @param bitpos The current bit position.
 */
int is_space_bit(int bitpos);

/**
 * Open the log file and append a "new log" marker to it.
 *
 * @param logfile The name of the log file to use.
 * @return The log file was opened succesfully (0), or errno on error.
 */
int write_new_logfile(char *logfile);

/**
 * Close the currently opened log file.
 *
 * @return The log file was closed successfully (0), or errno otherwise.
 */
int close_logfile(void);

/**
 * Retrieve "internal" information about the currently received bit.
 *
 * @return The bit information as described for {@link bitinfo}.
 */
struct bitinfo *get_bitinfo(void);

#endif
