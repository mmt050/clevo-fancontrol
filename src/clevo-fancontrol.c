/*
 ============================================================================
 Name        : clevo-fancontrol.c
 Author      : (original author) AqD <iiiaqd@gmail.com>, modified by Myles <m.m.todorov@gmail.com>
 Version     :
 Description : Clevo & S76 models fan-control CLI & service 

 Based on https://github.com/SkyLandTW/clevo-indicator by AqD <iiiaqd@gmail.com>

 Based on http://www.association-apml.fr/upload/fanctrl.c by Jonas Diemer
 (diemer@gmx.de)

 ============================================================================

 sudo chown root clevo-indicator
 sudo chmod u+s clevo-indicator

 Run as effective uid = root, but uid = desktop user (in order to use indicator).

 ============================================================================ 

 Install the system76 service, or write your own PID controller based on the 
 CLI which speaks ec_sys module's for Clevo. 
 Run without args to JSON output of current values, Run with an INT to set 
 fan duty cycle in %. 

 ============================================================================
 Auto fan control algorithm:

 S76 ships Pangolin (and other AMD laptops) without COREBOOT, and thus with 
 shitty (and permanent) fan-curves and TDP configs. For ex fan runs constantly 
 on Pang11 with 5700U. 

 This version of the tool is CLI-only, independent of desktop environment.
 It includes a rudimentary high-, low- water (temperature) style fan duty
 triggers, or if you want more sophisticated PID-controller style control, 
 run in read/write mode only, it outputs JSON by default.

 Original motivation of AqD <iiiaqd@gmail.com>:

 The algorithm is to replace the builtin auto fan-control algorithm in Clevo
 laptops which is apparently broken in recent models such as W350SSQ, where the
 fan doesn't get kicked until both of GPU and CPU are really hot (and GPU
 cannot be hot anymore thanks to nVIDIA's Maxwell chips). It's far more
 aggressive than the builtin algorithm in order to keep the temperatures below
 60°C all the time, for maximized performance with Intel turbo boost enabled.

 ============================================================================
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#define NAME "clevo-fancontrol"

#define EC_SC 0x66
#define EC_DATA 0x62

#define IBF 1
#define OBF 0
#define EC_SC_READ_CMD 0x80

/* EC registers can be read by EC_SC_READ_CMD or /sys/kernel/debug/ec/ec0/io:
 *
 * 1. modprobe ec_sys
 * 2. od -Ax -t x1 /sys/kernel/debug/ec/ec0/io
 */

#define EC_REG_SIZE 0x100
#define EC_REG_CPU_TEMP 0x07
#define EC_REG_GPU_TEMP 0xCD
#define EC_REG_FAN_DUTY 0xCE
#define EC_REG_FAN_RPMS_HI 0xD0
#define EC_REG_FAN_RPMS_LO 0xD1

#define MAX_FAN_RPM 4400.0

typedef enum {
    NA = 0, AUTO = 1, MANUAL = 2
} MenuItemType;

static void main_init_share(void);
static int main_ec_worker(void);

static void main_on_sigchld(int signum);
static void main_on_sigterm(int signum);
static int main_dump_fan(void);
static int main_test_fan(int duty_percentage);
static void ec_on_sigterm(int signum);
static int ec_init(void);
static int ec_auto_duty_adjust(void);
static int ec_query_cpu_temp(void);
static int ec_query_gpu_temp(void);
static int ec_query_fan_duty(void);
static int ec_query_fan_rpms(void);
static int ec_write_fan_duty(int duty_percentage);
static int ec_io_wait(const uint32_t port, const uint32_t flag,
        const char value);
static uint8_t ec_io_read(const uint32_t port);
static int ec_io_do(const uint32_t cmd, const uint32_t port,
        const uint8_t value);
static int calculate_fan_duty(int raw_duty);
static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low);
static void get_time_string(char* buffer, size_t max, const char* format);
static void signal_term(__sighandler_t handler);



#define MAX(a,b) (((a)>(b))?(a):(b))

struct {
    volatile int exit;
    volatile int cpu_temp;
    volatile int gpu_temp;
    volatile int fan_duty;
    volatile int fan_rpms;
    volatile int auto_duty;
    volatile int auto_duty_val;
}static *share_info = NULL;

static pid_t parent_pid = 0;

int main(int argc, char* argv[]) {
    if (ec_init() != EXIT_SUCCESS) {
        printf("unable to control EC: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (argc <= 1) {
        return main_dump_fan();
    } else {
        if (argv[1][0] == '--') {
            printf(
                    "\n\
Usage: clevo-fancontrol [fan-duty-percentage|-1]\n\
\n\
Dump/Control fan duty on Clevo laptops. Display indicator by default.\n\
\n\
Arguments:\n\
  [fan-duty-percentage]\t\tTarget fan duty in percentage, from 40 to 100\n\
  -?\t\t\t\tDisplay this help and exit\n\
\n\
Without arguments this program will dump current fan duty and temperature in JSON \n\
format. The binary requires running as root - either directly or with \n\
setuid=root flag.  \n\
This program would always attempt to load kernel\n\
module 'ec_sys', in order to query EC information from\n\
'/sys/kernel/debug/ec/ec0/io' instead of polling EC ports for readings,\n\
which may be more risky if interrupted or concurrently operated during the\n\
process.\n\
\n\
DO NOT MANIPULATE OR QUERY EC I/O PORTS WHILE THIS PROGRAM IS RUNNING.\n\
\n");
            return main_dump_fan();
        } else {
            int val = atoi(argv[1]);
            if (val == -1) {
                signal_term(&ec_on_sigterm);
                main_init_share();
                return main_ec_worker();
            } else if (val < 0 || val > 100)
            {
                printf("invalid fan duty %d!\n", val);
                return EXIT_FAILURE;
            }
            return main_test_fan(val);
        }
    }
    return EXIT_SUCCESS;
}

static void main_init_share(void) {
    void* shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED,
            -1, 0);
    share_info = shm;
    share_info->exit = 0;
    share_info->cpu_temp = 0;
    share_info->gpu_temp = 0;
    share_info->fan_duty = 0;
    share_info->fan_rpms = 0;
    share_info->auto_duty = 1;
    share_info->auto_duty_val = -1;
}

static int main_ec_worker(void) {
    setuid(0);
    system("modprobe ec_sys");
    while (share_info->exit == 0) {
        // read EC
        int io_fd = open("/sys/kernel/debug/ec/ec0/io", O_RDONLY, 0);
        if (io_fd < 0) {
            printf("unable to read EC from sysfs: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        unsigned char buf[EC_REG_SIZE];
        ssize_t len = read(io_fd, buf, EC_REG_SIZE);
        switch (len) {
        case -1:
            printf("unable to read EC from sysfs: %s\n", strerror(errno));
            break;
        case 0x100:
            share_info->cpu_temp = buf[EC_REG_CPU_TEMP];
            share_info->gpu_temp = buf[EC_REG_GPU_TEMP];
            share_info->fan_duty = calculate_fan_duty(buf[EC_REG_FAN_DUTY]);
            share_info->fan_rpms = calculate_fan_rpms(buf[EC_REG_FAN_RPMS_HI],
                    buf[EC_REG_FAN_RPMS_LO]);
            /*
             printf("temp=%d, duty=%d, rpms=%d\n", share_info->cpu_temp,
             share_info->fan_duty, share_info->fan_rpms);
             */
            break;
        default:
            printf("wrong EC size from sysfs: %ld\n", len);
        }
        close(io_fd);
        // auto EC
        if (share_info->auto_duty == 1) {
            int next_duty = ec_auto_duty_adjust();     
            // printf("auto-eval=%d%\n", next_duty);

            if ((next_duty != -1 && next_duty != share_info->auto_duty_val) || (next_duty == 0 && share_info->fan_duty !=0))   {
                char s_time[256];
                get_time_string(s_time, 256, "%m/%d %H:%M:%S");
                printf("%s CPU=%d°C, GPU=%d°C, auto fan duty to %d%%\n", s_time,
                        share_info->cpu_temp, share_info->gpu_temp, next_duty);
                ec_write_fan_duty(next_duty);
                share_info->auto_duty_val = next_duty;
            }
        }
        //
        usleep(3000 * 1000);
    }
    printf("worker quit\n");
    return EXIT_SUCCESS;
}

static void main_on_sigchld(int signum) {
    printf("main on worker quit signal\n");
    exit(EXIT_SUCCESS);
}

static void main_on_sigterm(int signum) {
    printf("main on signal: %s\n", strsignal(signum));
    if (share_info != NULL)
        share_info->exit = 1;
    exit(EXIT_SUCCESS);
}

static int main_dump_fan(void) {
    printf("{\n");
    printf("  \"duty\": %d,\n", ec_query_fan_duty());
    printf("  \"rpms\": %d,\n", ec_query_fan_rpms());
    printf("  \"cpu_temp_cels\": %d,\n", ec_query_cpu_temp());
    printf("  \"gpu_temp_cels\": %d\n", ec_query_gpu_temp());
    printf("}\n");
    return EXIT_SUCCESS;
}

static int main_test_fan(int duty_percentage) {
    printf("Change fan duty to %d%%\n", duty_percentage);
    ec_write_fan_duty(duty_percentage);
    printf("\n");
    main_dump_fan();
    return EXIT_SUCCESS;
}

static int ec_init(void) {
    if (ioperm(EC_DATA, 1, 1) != 0)
        return EXIT_FAILURE;
    if (ioperm(EC_SC, 1, 1) != 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static void ec_on_sigterm(int signum) {

    printf("ec on signal: %s\n, resetting to 20%", strsignal(signum));
    if (share_info != NULL)
        share_info->exit = 1;
    main_test_fan(16);   
}

static int eq_fuzzy_range(int left, int right, int range) {
    float min_right = right;
    float max_right = right;
    if (right != 0) {
        max_right = right + range;
        min_right = right - range;
    }

    if (left >= min_right && left <= max_right) 
        return 1;
    else
        return 0;
}

static int eq_fuzzy_perc(int left, int right, float accuracy) {
    float min_right = right;
    float max_right = right;
    if (right != 0) {
        max_right = (float)right * (1+accuracy);
        min_right = right * (1-accuracy);
    }

    // printf("eq_fuzzy=%d,%d; min(%f), max(%f)\n", left, right, min_right, max_right);
    if (left >= min_right && left <= max_right) 
        return 1;
    else
        return 0;
}

static int identify_duty(int duty) {
        const int SIZE = 7;
        float ACCURACY = 0.1; // 10% 
        int default_duty = 30;
        int allowed_duties[7] = {0, 16, 30, 40, 65, 90, 100};
        for (int i = 0; i < SIZE; i++) {
            if (eq_fuzzy_range(duty, allowed_duties[i], 1) == 1)
                return allowed_duties[i];

        }
        // printf("could not idenfity duty input(%d), returning default=%d%\n", duty, default_duty);
        // printf("could not idenfity duty returning raw input(%d)\n", duty);
        // return default_duty;
        return duty;
}

static int get_fan_speed(int temp) {
    /* Fitted qubic poly for curve :
       50c -> 0%
       60c -> 16%
       70c -> 30%
       80c -> 60%
       90c -> 80%
       95c -> 100%
    */
    int coef =10000;
    double t = temp;
    double a = (2.3268); // pre-coef-ed
    double b = (-296.758); // pre-coef-ed
    double c = 2.70371;
    double d = -89.6658;


    double result = (a*pow(t, 3)/coef) + (b*pow(t, 2))/coef + c*t+d;
    if (result > 100) {
        return 100;
    }
    return round(result);
}

static int ec_auto_duty_adjust(void) {
    int temp = MAX(share_info->cpu_temp, share_info->gpu_temp);
    // int duty = share_info->fan_duty;
    int duty = identify_duty(share_info->fan_duty);
    // printf("ec_auto_duty_adjust: temp=%dc, real_dury=%d, identifie_duty=%d%\n", temp, share_info->fan_duty, duty);


    // Poly:
    /*int new_duty = get_fan_speed(temp);

    if (duty > 0 && temp <= 60) {
        new_duty = 16;
    }

    if (duty > 0 && temp <= 50) {
        new_duty = 0;
    }*/
   
    // Hysterisis:

    int new_duty = -1;
    /*if (temp >= 90) 
        new_duty = 100; 
    else if (temp >= 87.5 && duty < 90)
        new_duty = 90;
    else*/ 
        if (temp >= 85 && duty < 65)
        new_duty = 65; 
    else if (temp >= 75 && duty < 40)
        new_duty = 40; 
    else if (temp >= 65 && duty < 30)
        new_duty = 30;
    else if (temp >= 55 && duty < 17)
        new_duty = 17;
    
    // else if (temp <= 50 && duty > 0)
    else if (temp <= 50)
        new_duty = 0;
    else if (temp <= 60 && duty >= 17)
        new_duty = 17;
    else if (temp <= 70 && duty >= 30)
        new_duty = 30;
    else if (temp <= 80 && duty >= 40)
        new_duty = 40;
    else if (temp <= 85 && duty >=65)
        new_duty = 65;
    

    if (new_duty > share_info->fan_duty) {
        int new_duty_adj = duty + (int)((new_duty - share_info->fan_duty)/2);
        if ((new_duty - new_duty_adj) > 2) {
            printf("using adjusted new duty=%d%\n", new_duty_adj);
            new_duty = new_duty_adj;
        }
    }

    return new_duty;
}

static int ec_query_cpu_temp(void) {
    return ec_io_read(EC_REG_CPU_TEMP);
}

static int ec_query_gpu_temp(void) {
    return ec_io_read(EC_REG_GPU_TEMP);
}

static int ec_query_fan_duty(void) {
    int raw_duty = ec_io_read(EC_REG_FAN_DUTY);
    return calculate_fan_duty(raw_duty);
}

static int ec_query_fan_rpms(void) {
    int raw_rpm_hi = ec_io_read(EC_REG_FAN_RPMS_HI);
    int raw_rpm_lo = ec_io_read(EC_REG_FAN_RPMS_LO);
    return calculate_fan_rpms(raw_rpm_hi, raw_rpm_lo);
}

static int ec_write_fan_duty(int duty_percentage) {
    if (duty_percentage < 0 || duty_percentage > 100) {
        printf("Wrong fan duty to write: %d\n", duty_percentage);
        return EXIT_FAILURE;
    }
    double v_d = ((double) duty_percentage) / 100.0 * 255.0;
    int v_i = (int) v_d;
    return ec_io_do(0x99, 0x01, v_i);
}

static int ec_io_wait(const uint32_t port, const uint32_t flag,
        const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    if (i >= 100) {
        printf("wait_ec error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x\n",
                port, data, flag, value);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static uint8_t ec_io_read(const uint32_t port) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(EC_SC_READ_CMD, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    //wait_ec(EC_SC, EC_SC_IBF_FREE);
    ec_io_wait(EC_SC, OBF, 1);
    uint8_t value = inb(EC_DATA);

    return value;
}

static int ec_io_do(const uint32_t cmd, const uint32_t port,
        const uint8_t value) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(cmd, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, IBF, 0);
    outb(value, EC_DATA);

    return ec_io_wait(EC_SC, IBF, 0);
}

static int calculate_fan_duty(int raw_duty) {
    return (int) ((double) raw_duty / 255.0 * 100.0);
}

static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    return raw_rpm > 0 ? (2156220 / raw_rpm) : 0;
}



static void get_time_string(char* buffer, size_t max, const char* format) {
    time_t timer;
    struct tm tm_info;
    time(&timer);
    localtime_r(&timer, &tm_info);
    strftime(buffer, max, format, &tm_info);
}

static void signal_term(__sighandler_t handler) {
    signal(SIGHUP, handler);
    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
    signal(SIGPIPE, handler);
    signal(SIGALRM, handler);
    signal(SIGTERM, handler);
    signal(SIGUSR1, handler);
    signal(SIGUSR2, handler);
}
