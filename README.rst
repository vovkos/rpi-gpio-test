Raspberry Pi kernel GPIO benchmarking test
==========================================

Abstract
--------

``rpi-gpio-test`` is a test to benchmark and compare different approaches to performing GPIO on **Raspberry Pi** from within a *Linux kernel module*.

Motivation
----------

There are quite a few GPIO benchmarking tests for Raspberry Pi available on the web in general and GitHub in particular. The absolute most of those just do *write-only* GPIO and measure performance of that. In reality, however, it's often crucial to measure the speed of a *two-way communication* to estimate the expected latency of a response to some external event.

Another thing is, when we benchmark a two-way GPIO communication, it's important to test and compare both *interrupt-based* and *polling-based* approaches.

This repo provides a codebase for a more complete testing matrix.

Testing matrix
--------------

.. list-table::
	:header-rows: 1

	*	- Test
		- Description

	*	- write-only
		- a simple busy loop which constantly changes the state of a GPIO line from HIGH to LOW and back

	*	- read/write
		- there are two parties, A and B; each one is both a reader and a writer. Each reader waits for its GPIO line to drop, resets it to back to HIGH, then drops the GPIO line of its counterpart. One of the GPIO lines is dropped initially to start the loop.

When a reader waits for its GPIO to drop, it can do either of the following:

.. list-table::
	:header-rows: 1

	*	- Method
		- Description

	*	- IRQ-based
		- use an interrupt handler to be triggered on lowering of a GPIO line

	*	- polling-based
		- run a thread which would constantly poll the state of a GPIO line in a busy loop

For reading and writing the states of GPIO lines, two approaches may be used:

.. list-table::
	:header-rows: 1

	*	- Method
		- Description

	*	- GPIO registers
		- map BCM-2836-specific GPIO registers into kernel address space and use those to read and write GPIO lines

	*	- ``gpio_get_value`` / ``gpio_set_value``
		- use portable, but less performant GPIO API available in Linux kernel

Usage
-----

1. Install Linux kernel headers for Raspberry Pi

	.. code:: bash

		$ sudo apt install raspberrypi-kernel-headers

2. Select testing configuration using defines in ``rpi-gpio-test.c``

	Testing scenario can be changed using:

	.. list-table::
		:header-rows: 1

		*	- #define
			- Default
			- Description

		*	- ``USE_GPIO_REGS``
			- 1
			- use BCM-2836 GPIO registers (otherwise, use the ``gpio_get_value`` / ``gpio_set_value`` API)

		*	- ``USE_RW_IRQ``
			- 0
			- use interrupts for read-write benchmark

		*	- ``USE_RW_POLL``
			- 1
			- use polling for read-write benchmark

		*	- ``USE_RW_YIELD``
			- 0
			- yield CPU with ``schedule ()`` during polling

		*	- ``USE_WO_BLOCKING``
			- 1
			- perform blocking write-only benchmark in ``module_init ()``

		*	- ``USE_WO_THREADED``
			- 0
			- perform write-only benchmark in a dedicated thread

		*	- ``USE_AFFINITY``
			- 1
			- assign threads to different CPU cores

	GPIO pins can be changed using:

	.. list-table::
		:header-rows: 1

		*	- #define
			- Default
			- Description

		*	- ``GPIO_A_OUT``
			- 17
			- output pin used to trigger reader A

		*	- ``GPIO_A_IN``
			- 18
			- reader A of the read-write test

		*	- ``GPIO_B_OUT``
			- 23
			- output pin used to trigger reader B

		*	- ``GPIO_B_IN``
			- 24
			- reader B of the read-write test

		*	- ``GPIO_C_OUT``
			- 22
			- used for write-only test

	For read-write tests, connect pins ``GPIO_A_OUT`` |<->| ``GPIO_A_IN`` and ``GPIO_B_OUT`` |<->| ``GPIO_B_IN``.

	Iteration counts can be changed using:

	.. list-table::
		:header-rows: 1

		*	- #define
			- Default (API)
			- Default (regs)
			- Description

		*	- ``RW_IRQ_ITERATION_COUNT``
			- 50000
			- 500000
			- IRQ-based read-write test

		*	- ``RW_POLL_ITERATION_COUNT``
			- 500000
			- 5000000
			- polling-based read-write test

		*	- ``WO_ITERATION_COUNT``
			- 1000000
			- 10000000
			- write-only test

3. Build kernel module

	.. code:: bash

		$ cd src
		$ make
		make -C /lib/modules/4.9.80-v7+/build/ M=/home/pi/Projects/rpi-gpio-test/src modules
		make[1]: Entering directory '/usr/src/linux-headers-4.9.80-v7+'
		  CC [M]  /home/pi/Projects/rpi-gpio-test/src/rpi-gpio-test.o
		  Building modules, stage 2.
		  MODPOST 1 modules
		  CC      /home/pi/Projects/rpi-gpio-test/src/rpi-gpio-test.mod.o
		  LD [M]  /home/pi/Projects/rpi-gpio-test/src/rpi-gpio-test.ko
		make[1]: Leaving directory '/usr/src/linux-headers-4.9.80-v7+'

4. Load kernel module

	.. code:: bash

		$ sudo insmod rpi-gpio-test

	This will launch the testing scenario chosen with the pre-processor definitions during step 2.

5. Inspect the results

	.. code:: bash

		$ dmesg

6. Unload kernel module

	.. code:: bash

		$ sudo rmmod rpi-gpio-test

7. Repeat from step 2

Results
-------

The following results are obtained on Raspberry Pi 2 Model B V1.1

The Linux version is:

.. code:: bash

	$ uname -a
	Linux raspberrypi 4.9.80-v7+ #1098 SMP Fri Mar 9 19:11:42 GMT 2018 armv7l GNU/Linux

.. list-table::
	:header-rows: 1

	*	- Test
		- Frequency (API)
		- Frequency (regs)

	*	- Write-only test
		- 1.3 MHz
		- 41 MHz

	*	- Read-write test (IRQ)
		- 110 KHz
		- 140 KHz

	*	- Read-write test (poll)
		- 370 KHz
		- 2.7 MHz

.. ............................................................................
..
.. RST replacments
..
.. ............................................................................

.. |<->| unicode:: U+2194
