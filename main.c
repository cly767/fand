#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>

#include <wiringPi.h>
#if !HARD_PWM
#include <softPwm.h>
#endif

enum states { running, idle, stopped };

/* global variables */
static const size_t buffer_size = 32;

// threading
static pthread_t listen_fifo_thread = { };
static pthread_mutex_t operation_mutex = { };
pid_t main_pid = 0;
static int main_scheduler = -1;


static bool manual = 0;
static int dc = 0;
static char buffer[32] = { };
static int temp_fd = -1, fifo_fd = -1;
static int return_value = 0;
static double temp = 0.0;
static enum states last_state = running; // state of last cycle
static unsigned idle_cycle_count = 0;

/* functions */
void init(void);

enum states determine_state(void);
void get_temp(void);
static inline int fan_curve(void);
void update_fan_dc(void);
static inline bool is_idle(void);
void *listen_fifo(void *arg);
int open_or_cre_fifo(const char *path, const int flags);
void main_loop(void);

void sighandler(int signo);
void cleanup(void);

int main(void) {
	init();

	main_loop();

	cleanup();

	return return_value;
}

void init(void) {
	struct sigaction act = { };
	struct sched_param param = { .sched_priority = 1 };

	main_pid = getpid();

	main_scheduler = sched_getscheduler(0);
	if(main_scheduler == -1)
#ifdef DEBUG
		fprintf(stderr, "unable to get scheduler for current process\n");
#endif
	else if(main_scheduler != SCHED_FIFO && main_scheduler != SCHED_RR) {
#ifdef DEBUG
			fprintf(stderr, "trying to set current sched policy to SCHED_RR...\n");
#endif
			if(sched_setscheduler(0, SCHED_RR, &param) == -1) {
#ifdef DEBUG
					fprintf(stderr, "unable to set scheduler for current process\n");
					fprintf(stderr, "SCHED_RR or SCHED_FIFO is recommended\n");
#endif
			}
	}

	if((temp_fd = open(temp_path, O_RDONLY)) < 0) {
		fprintf(stderr, "failed to open %s\n", temp_path);
		exit(1);
	}

	if(wiringPiSetupGpio()) {
		fprintf(stderr, "failed to setup WiringPi\n");
		close(temp_fd);
		exit(2);
	}
#if HARD_PWM
	switch(fan_control) {
		case 12:
		case 13:
		case 18:
		case 19:
			pwmSetMode(PWM_MODE_MS);
			pinMode(fan_control, PWM_MS_OUTPUT);

			// 19200000 / divisor / range
			pwmSetRange(100);
			pwmSetClock(8); // that's 25kHz
			break;
		default:
			fprintf(stderr, "bad GPIO number for hardware PWM\n");
			close(temp_fd);
			exit(0);
	}
#else
	softPwmCreate(fan_control, 0, 100);
#endif

	pthread_mutex_init(&operation_mutex, NULL);
	if(pthread_create(&listen_fifo_thread, NULL, listen_fifo, NULL)) {
		fprintf(stderr, "failed to start new thread\n");

		pthread_mutex_destroy(&operation_mutex);

#if !HARD_PWM
		softPwmStop(fan_control);
#endif
		pinMode(fan_control, PM_OFF);

		close(temp_fd);
		exit(5);
	}

	act.sa_handler = sighandler;
	act.sa_flags |= SA_RESTART;
	sigaction(SIGINT, &act, NULL);
	// in case of a systemd service
	sigaction(SIGTERM, &act, NULL);

	return;
}

void main_loop(void) {
	do {
		if(!manual) {
			pthread_mutex_lock(&operation_mutex);
			get_temp();
			update_fan_dc();
			pthread_mutex_unlock(&operation_mutex);
		}
		// we're on real-time scheduling, so make sure to sleep for a while
		usleep(interval * 1000);
		// and also yield, in case of tiny intervals
		if(main_scheduler == SCHED_FIFO || main_scheduler == SCHED_RR)
			sched_yield();
	} while(1);

	return;
}

// determine in which state we should run
enum states determine_state(void) {
	if(temp >= threshold) {
		// be running now! temperature is going high
		idle_cycle_count = 0;
		return running;
	}
	else {
		if(threshold - temp < hysteris) {
			// we're at idle temperature this cycle

			switch (last_state) {
				case idle:
					if(interval * idle_cycle_count >= idle_timeout)
						// we've been idle for too long; let the fan rest for a while
						return stopped;
				case running:
					// entered idle state from running & not long enough to stop the fan
					idle_cycle_count++;
					return idle;
				case stopped:
					// we were previously stopped and temperature rose to
					// the idle section; should sill be stopped
					return stopped;
			}
		}
		else {
			// temperature is safely low enough to stop the fan
			idle_cycle_count = 0;
			return stopped;
		}
	}
}

void get_temp(void) {
	if(read(temp_fd, buffer, buffer_size) < 0) {
		fprintf(stderr, "failed to read from %s\n", temp_path);
		return_value = 3;
		cleanup();
	}
	temp = atoi(buffer) / 1000.0;
#ifdef DEBUG
	fprintf(stdout, "temp = %.3lf\n", temp);
#endif
	lseek(temp_fd, 0, SEEK_SET);
	return;
}

void *listen_fifo(void *arg) {
	int n = 0;
	char a[32] = { }, *end = a;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	while(1) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		// blocks here
		if((fifo_fd = open_or_cre_fifo(fifo_path, O_RDONLY)) < 0) {
			fprintf(stderr, "failed to open or create %s\n", fifo_path);
			return_value = 4;
			kill(main_pid, SIGTERM);
			pthread_exit(NULL);
		}

		if(read(fifo_fd, a, buffer_size) == -1) {
			fprintf(stderr, "failed to read() from %s\n", fifo_path);
			kill(main_pid, SIGTERM);
			pthread_exit(NULL);
		}

		n = strtol(a, &end, 0);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		if(end != a) {
#ifdef DEBUG
			fprintf(stdout, "got manual dc value %d\n", n);
#endif
			// you may want to set dc_low to 0
			if(n != 0 && n >= dc_low && n <= 100) {
				pthread_mutex_lock(&operation_mutex);
				manual = 1;
				last_state = running;
				dc = n;
				update_fan_dc();
				pthread_mutex_unlock(&operation_mutex);

			} else {
#ifdef DEBUG
				fprintf(stdout, "out-of-range dc value, switching back to auto mode\n");
#endif
				pthread_mutex_lock(&operation_mutex);
				manual = 0;
				get_temp();
				update_fan_dc();
				pthread_mutex_unlock(&operation_mutex);
			}
		}
#ifdef DEBUG
		else {
			fprintf(stdout, "invalid input, fan state will not change\n");
		}
#endif
		// re-open for blocking input
		close(fifo_fd);
		memset(a, 0, 32);
		end = a;
	}
	return NULL;
}

int open_or_cre_fifo(const char *path, const int flags) {
	int fd = -1;

	if((fd = open(path, O_RDONLY)) < 0) {
		if(mkfifo(path, 0755)) {
			fprintf(stderr, "failed to mkfifo %s\n", fifo_path);
			return -1;
		}
		fd = open(path, O_RDONLY);
	}

	return fd;
}

void update_fan_dc(void) {
	if(!manual) {
		switch(last_state = determine_state()) {
			case running:
				dc = fan_curve();
				break;
			case idle:
				dc = dc_low;
				break;
			case stopped:
				dc = 0;
		}
	}

#ifdef DEBUG
	if(!manual)
		fprintf(stdout, "calculated fan dc %d%%\n", dc);
#endif

#if HARD_PWM
		pwmWrite(fan_control, dc>100?100:(dc>=dc_low?dc:0));
#else
		softPwmWrite(fan_control, dc>100?100:(dc>=dc_low?dc:0));
#endif
	return;
}

void cleanup(void) {
	// basically this is called when the system is shutting down
#ifdef DEBUG
	fprintf(stderr, "cleaning up...\n");
#endif

	// block signals for atomic operation, as this function should never fail
	// and restarting it from the middle is a bad idea
	//sigset_t signals, old_signals;
	//sigfillset(&signals);
	//sigemptyset(&old_signals);
	//sigprocmask(SIG_BLOCK, &signals, &old_signals);

#ifdef DEBUG
	fprintf(stderr, "cancelling thread\n");
#endif
	pthread_cancel(listen_fifo_thread);
	pthread_join(listen_fifo_thread, NULL);
	pthread_mutex_destroy(&operation_mutex);

#if !HARD_PWM
	softPwmStop(fan_control);
#endif
	pinMode(fan_control, PM_OFF);

	close(temp_fd);
	close(fifo_fd);
	unlink(fifo_path);

	//fprintf(stderr, "\n");
	exit(return_value);
	return;
}

void sighandler(int signo) {
	cleanup();
	return;
}
