#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <nvml.h>

#define MAX_POINTS 20
#define DEFAULT_POLL_RATE_MS 1000

typedef struct {
    unsigned int temp;
    unsigned int fan_speed;
} TempPoint;

volatile sig_atomic_t running = 1;

void signal_handler(int signum) {
    running = 0;
}

// Linear interpolation between two points
unsigned int interpolate(unsigned int temp, TempPoint p1, TempPoint p2) {
    if (temp <= p1.temp) return p1.fan_speed;
    if (temp >= p2.temp) return p2.fan_speed;
    
    float ratio = (float)(temp - p1.temp) / (float)(p2.temp - p1.temp);
    return p1.fan_speed + (unsigned int)(ratio * (p2.fan_speed - p1.fan_speed));
}

// Get fan speed for current temperature using the curve
unsigned int get_fan_speed_for_temp(unsigned int temp, TempPoint* points, int num_points) {
    if (num_points == 0) return 0;
    if (num_points == 1) return points[0].fan_speed;
    
    // Find the appropriate segment
    for (int i = 0; i < num_points - 1; i++) {
        if (temp >= points[i].temp && temp <= points[i + 1].temp) {
            return interpolate(temp, points[i], points[i + 1]);
        }
    }
    
    // temperature is outside the defined range
    if (temp < points[0].temp) return points[0].fan_speed;
    return points[num_points - 1].fan_speed;
}

void print_usage(const char* program_name) {
    printf("Usage: %s [-p poll_rate_ms] temp1 fan1 temp2 fan2 [temp3 fan3 ...]\n", program_name);
    printf("  -p poll_rate_ms: Polling rate in milliseconds (default: %d)\n", DEFAULT_POLL_RATE_MS);
    printf("  tempX: Temperature point in Celsius\n");
    printf("  fanX: Fan speed percentage (0-100) for corresponding temperature\n");
    printf("Example: %s -p 500 30 30 50 60 70 100\n", program_name);
}

void check_nvml_error(nvmlReturn_t result, const char* message) {
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Error: %s - %s\n", message, nvmlErrorString(result));
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    nvmlDevice_t device;
    unsigned int temp, current_fan_speed;
    unsigned int num_fans = 0;
    TempPoint points[MAX_POINTS];
    int num_points = 0;
    int poll_rate_ms = DEFAULT_POLL_RATE_MS;
    int opt;
    nvmlReturn_t result;

    while ((opt = getopt(argc, argv, "p:h")) != -1) {
        switch (opt) {
            case 'p':
                poll_rate_ms = atoi(optarg);
                if (poll_rate_ms < 100) {
                    fprintf(stderr, "Warning: Poll rate too low, setting to 100ms\n");
                    poll_rate_ms = 100;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    for (int i = optind; i < argc; i += 2) {
        if (i + 1 >= argc || num_points >= MAX_POINTS) {
            fprintf(stderr, "Error: Invalid number of arguments or too many points\n");
            print_usage(argv[0]);
            return 1;
        }
        
        points[num_points].temp = atoi(argv[i]);
        points[num_points].fan_speed = atoi(argv[i + 1]);
        
        if (points[num_points].fan_speed > 100) {
            fprintf(stderr, "Error: Fan speed must be between 0 and 100\n");
            return 1;
        }
        num_points++;
    }

    if (num_points < 2) {
        fprintf(stderr, "Error: At least two temperature-fan points required\n");
        print_usage(argv[0]);
        return 1;
    }

    // Initialize NVML
    result = nvmlInit();
    check_nvml_error(result, "Failed to initialize NVML");

    result = nvmlDeviceGetHandleByIndex(0, &device);
    check_nvml_error(result, "Failed to get device handle");

    result = nvmlDeviceGetNumFans(device, &num_fans);
    check_nvml_error(result, "Failed to get number of fans");

    if (num_fans == 0) {
        fprintf(stderr, "Error: No fans detected on the GPU\n");
        nvmlShutdown();
        return 1;
    }

    printf("Detected %u fans on the GPU\n", num_fans);

    // Set up signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("GPU fan control started. Press Ctrl+C to exit.\n");
    printf("Temperature points: ");
    for (int i = 0; i < num_points; i++) {
        printf("(%d°C: %d%%) ", points[i].temp, points[i].fan_speed);
    }
    printf("\nPolling rate: %d ms\n", poll_rate_ms);

    for (unsigned int fan = 0; fan < num_fans; fan++) {
        check_nvml_error(nvmlDeviceSetFanControlPolicy(device, fan, NVML_FAN_POLICY_MANUAL), "Failed to set fan policy");
    }

    // Main control loop
    while (running) {
        // Get current GPU temperature
        result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
        check_nvml_error(result, "Failed to get GPU temperature");

        // Calculate desired fan speed
        current_fan_speed = get_fan_speed_for_temp(temp, points, num_points);

        // Set fan speed for all fans
        for (unsigned int fan = 0; fan < num_fans; fan++) {
            result = nvmlDeviceSetFanSpeed_v2(device, fan, current_fan_speed);
            if (result != NVML_SUCCESS) {
                fprintf(stderr, "\nWarning: Failed to set speed for fan %u - %s\n", 
                        fan, nvmlErrorString(result));
            }
        }

        printf("\rGPU Temperature: %d°C, Fan Speed: %d%% (All Fans)", temp, current_fan_speed);
        fflush(stdout);

        usleep(poll_rate_ms * 1000);
    }

    for (unsigned int fan = 0; fan < num_fans; fan++) {
        check_nvml_error(nvmlDeviceSetFanControlPolicy(device, fan, NVML_FAN_POLICY_TEMPERATURE_CONTINOUS_SW), "Failed to set fan policy");
        check_nvml_error(nvmlDeviceSetDefaultFanSpeed_v2(device, fan), "Failed to set default fan speed");
    }
    printf("\nShutting down...\n");
    nvmlShutdown();
    return 0;
}
