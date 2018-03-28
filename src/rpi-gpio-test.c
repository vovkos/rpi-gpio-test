#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/kthread.h>

//..............................................................................

#define LKM_NAME "rpi-gpio-test"

// modify and recompile to test different scenarios

#define USE_GPIO_REGS    1  // use BCM-2836 GPIO registers (vs gpio_get_value/gpio_set_value)
#define USE_RW_IRQ       0  // use interrupts for read-write benchmark
#define USE_RW_POLL      1  // use polling for read-write benchmark
#define USE_RW_YIELD     0  // yield CPU with schedule () during polling
#define USE_WO_BLOCKING  1  // perform blocking write-only benchmark in module_init
#define USE_WO_THREADED  0  // perform write-only benchmark in a dedicated thread
#define USE_AFFINITY     1  // assign threads to different CPU cores

// sanity check

#if (USE_RW_IRQ && USE_RW_POLL || USE_WO_BLOCKING && USE_WO_THREADED)
#	error invalid configuration (mutual exlusive settings)
#endif

// connect pins A_IN <-> A_OUT and B_IN <-> B_OUT for read-write benchmark

#define GPIO_A_OUT 17
#define GPIO_A_IN  18
#define GPIO_B_OUT 23
#define GPIO_B_IN  24
#define GPIO_C_OUT 22 // used for write-only test

// iteration counts for read-write and write-only tests

#if (USE_GPIO_REGS)
#	define RW_IRQ_ITERATION_COUNT  500000   // 500,000 iterations
#	define RW_POLL_ITERATION_COUNT 5000000  // 5 mil iterations
#	define WO_ITERATION_COUNT      10000000 // 10 mil iterations
#else
#	define RW_IRQ_ITERATION_COUNT  50000    // 50,000 iterations
#	define RW_POLL_ITERATION_COUNT 500000   // 500,000 iterations
#	define WO_ITERATION_COUNT      1000000  // 1 mil iterations
#endif

//..............................................................................

#if (USE_GPIO_REGS)

static volatile unsigned int* g_gpio_regs;

#	define GPIO_BASE_ADDR 0x3f200000

#	define GPIO_SET_FUNC_IN(g)  *(g_gpio_regs+((g)/10)) &= ~(7<<(((g)%10)*3))
#	define GPIO_SET_FUNC_OUT(g) *(g_gpio_regs+((g)/10)) |= (1<<(((g)%10)*3))

#	define GPIO_GET(g) ((*(g_gpio_regs+13) & (1<<(g))) != 0)
#	define GPIO_SET(g) (*(g_gpio_regs+7) = 1<<(g))
#	define GPIO_CLR(g) (*(g_gpio_regs+10) = 1<<(g))

#else

#	define GPIO_GET(g) (gpio_get_value (g))
#	define GPIO_SET(g) (gpio_set_value (g, 1))
#	define GPIO_CLR(g) (gpio_set_value (g, 0))

#endif

//..............................................................................

// timestamps in 100-nsec intervals (aka windows file time)

static inline unsigned long long get_timestamp (void)
{
	struct timespec tspec;
	ktime_get_real_ts (&tspec);
	return tspec.tv_sec * 10000000 + tspec.tv_nsec / 100;
}

static unsigned long long g_rw_base_timestamp = 0;

//..............................................................................

// read-write benchmark test

#if (USE_RW_IRQ)

static unsigned long long g_count = 0;

static int g_gpio_a_irq = -1;
static int g_gpio_b_irq = -1;

static irqreturn_t gpio_a_irq_handler (int irq, void* context)
{
	GPIO_SET (GPIO_A_OUT); // shut down interrupt on line A
	GPIO_CLR (GPIO_B_OUT); // trigger interrupt on line B
	return IRQ_HANDLED;
}

static irqreturn_t gpio_b_irq_handler (int irq, void* context)
{
	unsigned long long time;
	unsigned long long hz;

	GPIO_SET (GPIO_B_OUT); // shut down interrupt on line B

	if (g_count >= RW_IRQ_ITERATION_COUNT)
	{
		time = get_timestamp () - g_rw_base_timestamp;
		hz = g_count * 10000000;
		do_div (hz, time);
		printk (KERN_INFO LKM_NAME ": IRQ-based read-write test finished: %llu iterations, %llu Hz\n", g_count, hz);
		return IRQ_HANDLED;
	}

	__sync_add_and_fetch (&g_count, 1);
	GPIO_CLR (GPIO_A_OUT); // trigger interrupt on line A
	return IRQ_HANDLED;
}

#elif (USE_RW_POLL)

static struct task_struct* g_rw_thread_a;
static struct task_struct* g_rw_thread_b;

static int rw_thread_a (void* context)
{
	bool b;

	printk (KERN_INFO LKM_NAME ": entering read-write thread A\n");

	while (!kthread_should_stop ())
	{
		while (!kthread_should_stop ())
		{
			b = GPIO_GET (GPIO_A_IN);
			if (!b)
			{
				GPIO_SET (GPIO_A_OUT);
				break;
			}

#if (USE_RW_YIELD)
			schedule ();
#endif
		}

		GPIO_CLR (GPIO_B_OUT); // trigger thread B
	}

	printk (KERN_INFO LKM_NAME ": exiting read-write thread A\n");
	return 0;
}

static int rw_thread_b (void* context)
{
	unsigned long long time;
	unsigned long long hz;
	unsigned long long i;
	bool b;

	printk (KERN_INFO LKM_NAME ": entering read-write thread B\n");

	for (i = 0; i < RW_POLL_ITERATION_COUNT; i++)
	{
		for (;;)
		{
			b = GPIO_GET (GPIO_B_IN);
			if (!b)
			{
				GPIO_SET (GPIO_B_OUT);
				break;
			}

#if (USE_RW_YIELD)
			schedule ();
#endif
		}

		GPIO_CLR (GPIO_A_OUT); // trigger thread A
	}

	time = get_timestamp () - g_rw_base_timestamp;
	hz = i * 10000000;
	do_div (hz, time);
	printk (KERN_INFO LKM_NAME ": polling-based read-write test finished: %llu iterations, %llu Hz\n", i, hz);

	kthread_stop (g_rw_thread_a);

	printk (KERN_INFO LKM_NAME ": exiting read-write thread B\n");
	return 0;
}

#endif

//..............................................................................

// write-only benchmark test

#if (USE_WO_BLOCKING || USE_WO_THREADED)

static void wo_benchmark (void)
{
	unsigned long long base_timestamp;
	unsigned long long time;
	unsigned long long hz;
	unsigned long long i;

	printk (KERN_INFO LKM_NAME ": benchmarking write-only GPIO...\n");

	base_timestamp = get_timestamp ();

	for (i = 0; i < WO_ITERATION_COUNT; i++)
	{
		GPIO_SET (GPIO_C_OUT);
		GPIO_CLR (GPIO_C_OUT);
	}

	time = get_timestamp () - base_timestamp;
	hz = i * 10000000;
	do_div (hz, time);
	printk (KERN_INFO LKM_NAME ": write-only GPIO finished: %llu iterations, %llu Hz\n", i, hz);
}

#endif

#if (USE_WO_THREADED)

static struct task_struct* g_wo_thread;

static int wo_thread (void* context)
{
	printk (KERN_INFO LKM_NAME ": entering write-only thread\n");
	wo_benchmark ();
	printk (KERN_INFO LKM_NAME ": exiting write-only thread\n");
	return 0;
}

#endif

//..............................................................................

#if (!USE_GPIO_REGS)

static int setup_gpio (unsigned int g, bool is_out)
{
	int result;

	result = gpio_request (g, NULL);
	if (result != 0)
	{
		printk (KERN_ERR LKM_NAME ": cannot request GPIO %d: error: %d\n", g, result);
		return result;
	}

	result = is_out ?
		gpio_direction_output (g, 1) :
		gpio_direction_input (g);

	if (result != 0)
	{
		printk (KERN_ERR LKM_NAME ": cannot configure GPIO %d: error: %d\n", g, result);
		return result;
	}

	return 0;
}

#endif

//..............................................................................

int __init rpi_gpio_test_init (void)
{
#if (USE_RW_IRQ || !USE_GPIO_REGS)
	int result;
#endif

	printk (KERN_INFO LKM_NAME ": --- loading GPIO benchmark test ---\n");

#if (USE_GPIO_REGS)
	printk (KERN_INFO LKM_NAME ": preparing GPIOs for register access...\n");

	g_gpio_regs = (volatile unsigned int*) ioremap (GPIO_BASE_ADDR, 16 * 1024);
	if (!g_gpio_regs)
	{
		printk (KERN_INFO LKM_NAME ": error mapping GPIO registers\n");
		return -EBUSY;
	}

	GPIO_SET_FUNC_IN (GPIO_A_IN);
	GPIO_SET_FUNC_IN (GPIO_A_OUT);
	GPIO_SET_FUNC_OUT (GPIO_A_OUT);
	GPIO_SET (GPIO_A_OUT);

	GPIO_SET_FUNC_IN (GPIO_B_IN);
	GPIO_SET_FUNC_IN (GPIO_B_OUT);
	GPIO_SET_FUNC_OUT (GPIO_B_OUT);
	GPIO_SET (GPIO_B_OUT);

	GPIO_SET_FUNC_IN (GPIO_C_OUT);
	GPIO_SET_FUNC_OUT (GPIO_C_OUT);
	GPIO_SET (GPIO_C_OUT);
#else
	printk (KERN_INFO LKM_NAME ": preparing GPIOs for API access...\n");

	result = setup_gpio (GPIO_A_OUT, true);
	if (result != 0)
		return result;

	result = setup_gpio (GPIO_A_IN, false);
	if (result != 0)
		return result;

	result = setup_gpio (GPIO_B_OUT, true);
	if (result != 0)
		return result;

	result = setup_gpio (GPIO_B_IN, false);
	if (result != 0)
		return result;

	result = setup_gpio (GPIO_C_OUT, true);
	if (result != 0)
		return result;
#endif

#if (USE_WO_BLOCKING)
	wo_benchmark ();
#endif

#if (USE_RW_IRQ)
	g_gpio_a_irq = gpio_to_irq (GPIO_A_IN);

	printk (KERN_INFO LKM_NAME ": setting interrupt handler for GPIO %d IRQ %d...\n", GPIO_A_IN, g_gpio_a_irq);

	result = request_irq (g_gpio_a_irq, gpio_a_irq_handler, IRQF_TRIGGER_LOW, LKM_NAME, NULL);
	if (result != 0)
	{
		printk (KERN_ERR LKM_NAME ": error: %d\n", result);
		return result;
	}

	g_gpio_b_irq = gpio_to_irq (GPIO_B_IN);

	printk (KERN_INFO LKM_NAME ": setting interrupt handler for GPIO %d IRQ %d...\n", GPIO_B_IN, g_gpio_b_irq);

	result = request_irq (g_gpio_b_irq, gpio_b_irq_handler, IRQF_TRIGGER_LOW, LKM_NAME, NULL);
	if (result != 0)
	{
		printk (KERN_ERR LKM_NAME ": error: %d\n", result);
		return result;
	}
#elif (USE_RW_POLL)
	printk (KERN_INFO LKM_NAME ": starting read-write threads...\n");

	g_rw_thread_a = kthread_create (rw_thread_a, NULL, "rw_thread_a");
	g_rw_thread_b = kthread_create (rw_thread_b, NULL, "rw_thread_b");

	if (!g_rw_thread_a || !g_rw_thread_b)
	{
		printk (KERN_ERR LKM_NAME ": unable to create read-write threads.\n");
		return -EFAULT;
	}

#if (USE_AFFINITY)
	kthread_bind (g_rw_thread_a, 1);
	kthread_bind (g_rw_thread_b, 2);
#endif

	wake_up_process (g_rw_thread_a);
	wake_up_process (g_rw_thread_b);
#endif

#if (USE_WO_THREADED)
	printk (KERN_INFO LKM_NAME ": starting write-only thread...\n");

	g_wo_thread = kthread_create (wo_thread, NULL, "thread_bitbang");
	if (!g_wo_thread)
	{
		printk (KERN_ERR LKM_NAME ": unable to create write-only thread.\n");
		return -EFAULT;
	}

#	if (USE_AFFINITY)
	kthread_bind (g_wo_thread, 3);
#	endif

	wake_up_process (g_wo_thread);
#endif

#if (USE_RW_IRQ || USE_RW_POLL)
	printk (KERN_INFO LKM_NAME ": lowering GPIO %d to initiate a loop...\n", GPIO_A_OUT);

	g_rw_base_timestamp = get_timestamp ();
	GPIO_CLR (GPIO_A_OUT);
#endif

	return 0;
}

void __exit rpi_gpio_test_exit (void)
{
	printk (KERN_INFO LKM_NAME ": --- unloading GPIO benchmark test ---\n");

#if (USE_RW_IRQ)
	free_irq (g_gpio_a_irq, NULL);
	free_irq (g_gpio_b_irq, NULL);
#endif

#if (USE_GPIO_REGS)
	iounmap (g_gpio_regs);
#else
	gpio_free (GPIO_A_IN);
	gpio_free (GPIO_A_OUT);
	gpio_free (GPIO_B_IN);
	gpio_free (GPIO_B_OUT);
	gpio_free (GPIO_C_OUT);
#endif
}

//..............................................................................

module_init (rpi_gpio_test_init);
module_exit (rpi_gpio_test_exit);

MODULE_LICENSE ("GPL v2");
MODULE_DESCRIPTION (LKM_NAME " - kernel module to benchmark GPIO performance on Raspberry Pi");
MODULE_VERSION ("1.0");

//..............................................................................
