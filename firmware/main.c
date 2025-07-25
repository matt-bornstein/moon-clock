#include "EPD_Test.h"   // Examples
#include "run_File.h"

#include "led.h"
#include "waveshare_PCF85063.h" // RTC
#include "DEV_Config.h"
#include "MoonPhase.h"
#include <time.h>
#include <math.h>
#include <string.h>

extern const char *fileList;
extern char pathName[];

#define enChargingRtc 0
#define CMD_MAX_LENGTH 32
#define CMD_BUFFER_SIZE 64

#define PI 3.14159265358979323846
#define SYNODIC_MONTH 29.530588861

/*
Mode 0: Automatically get pic folder names and sort them
Mode 1: Automatically get pic folder names but not sorted
Mode 2: pic folder name is not automatically obtained, users need to create fileList.txt file and write the picture name in TF card by themselves
Mode 3: Display moon phase based on current date
*/
#define Mode 3
#define acceleratedSimulation 0

static const char* month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};


float measureVBAT(void)
{
    float Voltage=0.0;
    const float conversion_factor = 3.3f / (1 << 12);
    uint16_t result = adc_read();
    Voltage = result * conversion_factor * 3;
    // printf("Raw value: 0x%03x, voltage: %f V\n", result, Voltage);
    return Voltage;
}

void chargeState_callback() 
{
    if(DEV_Digital_Read(VBUS)) {
        if(!DEV_Digital_Read(CHARGE_STATE)) {  // is charging
            ledCharging();
        }
        else {  // charge complete
            ledCharged();
        }
    }
}

double calculate_moon_phase(int year, int month, int day, int hour, int minute, int second) {
    MoonPhase mp;
    MoonPhase_init(&mp);
    MoonPhase_calculate(&mp, year, month, day, hour, minute, second);
    return mp.phase;
}

typedef struct {
    int year;
    int month;
    int day;
} MoonDate;

MoonDate calculate_next_moon_event(int year, int month, int day, double target_phase) {
    MoonDate result;
    
    // Convert date to Julian Date (reusing code from calculate_moon_phase)
    double jd;
    int b;
    
    if (month <= 2) {
        year--;
        month += 12;
    }
    
    b = 2 - floor(year/100) + floor(year/400);
    jd = floor(365.25*(year + 4716)) + floor(30.6001*(month + 1)) + day + b - 1524.5;
    
    // Calculate current phase
    double current_age = (jd - 2451550.1) / SYNODIC_MONTH;
    current_age = current_age - floor(current_age);  // Fractional part
    
    // Calculate how many days until the next target phase
    double days_until_target = ((target_phase - current_age) * SYNODIC_MONTH);
    if (days_until_target <= 0) {
        days_until_target += SYNODIC_MONTH;
    }
    
    // Add days to Julian Date
    jd += days_until_target;
    
    // Convert Julian Date back to calendar date
    // Adapted from Jean Meeus' Astronomical Algorithms
    jd += 0.5;
    int Z = (int)jd;
    double F = jd - Z;
    
    int A;
    if (Z < 2299161) {
        A = Z;
    } else {
        int alpha = (int)((Z - 1867216.25) / 36524.25);
        A = Z + 1 + alpha - (int)(alpha / 4);
    }
    
    int B = A + 1524;
    int C = (int)((B - 122.1) / 365.25);
    int D = (int)(365.25 * C);
    int E = (int)((B - D) / 30.6001);
    
    result.day = B - D - (int)(30.6001 * E) + F;
    result.month = (E < 14) ? E - 1 : E - 13;
    result.year = (result.month > 2) ? C - 4716 : C - 4715;
    
    return result;
}

// Get moon phase image path based on phase
void get_moon_phase_path(char* path_buffer, double phase) {
    // Convert phase (0-1) to phase number (0-32), where 0<->0 and 1<->32 are the same phase (new moon)
    // 32 moon phase images: 01.jpg (new) through 32.jpg (waning crescent)
    int phase_index = (int)round(phase * 32) % 32;
    sprintf(path_buffer, "pic/%02d.bmp", phase_index+1);
}

// displays the proper image based on current time
// must set time correctly **before** calling this function
void run_display(char hasCard)
{
    printf("Running display in mode %d\n", Mode);
    char display_text[128] = "";
    if(hasCard) {
        if(Mode == 3) {
            // Calculate moon phase using RTC time
            Time_data currentTime = PCF85063_GetTime();
            printf("Calculating moon info for date %02d-%02d-%02d %02d:%02d:%02d\n",
                currentTime.years, currentTime.months, currentTime.days,
                currentTime.hours, currentTime.minutes, currentTime.seconds);
            
            #if acceleratedSimulation
            int day = currentTime.minutes % 30;
            #else
            int day = currentTime.days;
            #endif

            double phase = calculate_moon_phase(currentTime.years + 2000, currentTime.months, day, currentTime.hours, currentTime.minutes, currentTime.seconds);
            MoonDate nextFullMoon = calculate_next_moon_event(currentTime.years + 2000, currentTime.months, day, 0.5);
            MoonDate nextNewMoon = calculate_next_moon_event(currentTime.years + 2000, currentTime.months, day, 0.0);

            // display current date, next full moon, next new moon
            sprintf(display_text, "%s %d, %d | full: %s %d | new: %s %d",
                month_names[currentTime.months-1], day, currentTime.years+2000,
                month_names[nextFullMoon.month-1], nextFullMoon.day,
                month_names[nextNewMoon.month-1], nextNewMoon.day);
            printf("Moon phase: %f\n", phase);
            printf("Moon display text: %s\n", display_text);
            get_moon_phase_path(pathName, phase);
            printf("Moon phase path: %s\n", pathName);
        } else {
            setFilePath();
        }
        EPD_7in3f_display_BMP(pathName, display_text, measureVBAT());   // display bmp
    }
    else {
        EPD_7in3f_display(measureVBAT());
    }
}

// sets the next alarm time
// must set time correctly **before** calling this function
void set_next_alarm() {
    Time_data currentTime = PCF85063_GetTime();
    Time_data alarmTime = currentTime;
    #if acceleratedSimulation
    alarmTime.minutes += 2;
    #else
    alarmTime.days += 1;
    alarmTime.hours = 0;
    alarmTime.minutes = 0;
    alarmTime.seconds = 0;
    #endif

    PCF85063_clear_alarm_flag();    // clear RTC alarm flag
    PCF85063_alarm_Time_Enabled(alarmTime); // set new alarm time without resetting the RTC time
}

void process_command(char* cmd) {
    printf("Processing command: %s\n", cmd);
    if (strncmp(cmd, "setdate", 7) == 0) {
        // Format: setdate YYYY-MM-DD HH:MM:SS
        int year, month, day, hours, minutes, seconds;
        if (sscanf(cmd + 8, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hours, &minutes, &seconds) == 6) {
            if (year >= 2000 && year <= 2099 && 
                    month >= 1 && month <= 12 && 
                    day >= 1 && day <= 31 &&
                    hours >= 0 && hours <= 23 &&
                    minutes >= 0 && minutes <= 59 &&
                    seconds >= 0 && seconds <= 59) {
                Time_data newDate;
                newDate.years = year - 2000;
                newDate.months = month;
                newDate.days = day;
                newDate.hours = hours;
                newDate.minutes = minutes;
                newDate.seconds = seconds;
                printf("Running display, setting new date/time, and setting next alarm for: %02d-%02d-%02d %02d:%02d:%02d\n",
                    newDate.years, newDate.months, newDate.days,
                    newDate.hours, newDate.minutes, newDate.seconds);
                PCF85063_SetTime(newDate);
                set_next_alarm();
                run_display(1);
            } else {
                printf("Invalid date/time format. Use: setdate YYYY-MM-DD HH:MM:SS\n");
            }
        }
    }
    else if (strncmp(cmd, "getphase", 8) == 0) {
        // Format: getphase YYYY-MM-DD HH:MM:SS
        int year, month, day, hours, minutes, seconds;
        if (sscanf(cmd + 8, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hours, &minutes, &seconds) == 6) {
            printf("Getting moon phase for date %02d-%02d-%02d %02d:%02d:%02d\n", 
                year, month, day, hours, minutes, seconds);
            double phase = calculate_moon_phase(year, month, day, hours, minutes, seconds);
            printf("Moon phase: %f\n", phase);
        }
    }
    else {
        printf("Unknown command. Type 'help' for available commands\n");
    }
}

void check_serial_commands(void) {
    static char cmd_buffer[CMD_BUFFER_SIZE];
    static int buf_pos = 0;
    
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        // printf("Received char: %c (0x%02x)\n", c, c);  // Debug received characters
        if (c == '\n' || c == '\r') {
            if (buf_pos > 0) {
                cmd_buffer[buf_pos] = '\0';
                process_command(cmd_buffer);
                buf_pos = 0;
            }
        }
        else if (buf_pos < CMD_BUFFER_SIZE - 1) {
            cmd_buffer[buf_pos++] = (char)c;
        }
    }
}

int main(void)
{
    // default boot time (date is overwritten by SD card in mode 3 if present)
    Time_data bootTime = {2025-2000, 1, 1, 0, 0, 0};
    char isCard = 0;
  
    if(DEV_Module_Init() != 0) {  // DEV init
        return -1;
    }

    gpio_set_function(0, GPIO_FUNC_UART);  // TX on GPIO0
    gpio_set_function(1, GPIO_FUNC_UART);  // RX on GPIO1
    
    watchdog_enable(8*1000, 1);    // 8s
    DEV_Delay_ms(5000);
    printf("Init...\r\n");
    gpio_set_irq_enabled_with_callback(CHARGE_STATE, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, chargeState_callback);

    if(measureVBAT() < 3.1) {   // battery power is low
        printf("low power ...\r\n");
        PCF85063_alarm_Time_Disable();
        ledLowPower();  // LED flash for Low power
        powerOff(); // BAT off
        return 0;
    }
    else {
        printf("power sufficient ...\r\n");
        ledPowerOn();
    }

    int rtcIsStable = PCF85063_is_stable();
    int rtcHasValidTime = PCF85063_has_valid_time();
    Time_data rtcTime = PCF85063_GetTime();
    printf("RTC state (0=not stable, 1=stable): %d\n", rtcIsStable);
    printf("RTC has valid time: %d\n", rtcHasValidTime);
    printf("RTC time: %02d-%02d-%02d %02d:%02d:%02d\n", 
           rtcTime.years, rtcTime.months, rtcTime.days,
           rtcTime.hours, rtcTime.minutes, rtcTime.seconds);
    
    if (rtcIsStable == 1 && rtcHasValidTime == 1) {
        printf("RTC is stable and has valid time, using RTC time\n");
        bootTime = rtcTime;
    }

    if(!sdTest()) 
    {
        isCard = 1;
        if(Mode == 0)
        {
            sdScanDir();
            file_sort();
        }
        if(Mode == 1)
        {
            sdScanDir();
        }
        if(Mode == 2)
        {
            file_cat();
        }
        if(Mode == 3)
        {
            // read date from SD card
            printf("Executing mode 3\n");
            if (rtcIsStable == 0 || rtcHasValidTime == 0) {
                printf("RTC is not stable or has invalid time, getting date from SD card\n");
                char buf[32];
                if (read_file_contents("date.txt", buf, sizeof(buf)) == 0) {
                    printf("Contents of date.txt: %s\n", buf);
                    int year, month, day;
                    if (sscanf(buf, "%d-%d-%d", &year, &month, &day) == 3) {
                        // Validate ranges
                        if (year >= 2000 && year <= 2099 && 
                            month >= 1 && month <= 12 && 
                            day >= 1 && day <= 31) {
                            bootTime.years = year - 2000;  // Convert to 2-digit year format
                            bootTime.months = month;
                            bootTime.days = day;
                            printf("Date parsed successfully: %02d-%02d-%02d\n", 
                                bootTime.years, bootTime.months, bootTime.days);
                        } else {
                            printf("Invalid date ranges in date.txt\n");
                        }
                    } else {
                        printf("Failed to parse date from date.txt\n");
                    }
                }
                else {
                    printf("Failed to read date.txt, or date.txt not present\n");
                }
            }
        }
    }
    else 
    {
        isCard = 0;
    }

    // initialize RTC
    // this might not be necessary if already stable
    printf("Initializing RTC\n");
    PCF85063_init();
    printf("Setting RTC time: %02d-%02d-%02d %02d:%02d:%02d\n", 
        bootTime.years, bootTime.months, bootTime.days,
        bootTime.hours, bootTime.minutes, bootTime.seconds);
    PCF85063_SetTime(bootTime);

    // set first alarm
    set_next_alarm();

    // display first image
    run_display(isCard);

    // if plugged in, poll for key (and RTC) interrupts
    // TODO switch to using real interrupts so we can run off battery?
    if(!DEV_Digital_Read(VBUS)) {    // no charge state
        printf("No charge state\n");
    }
    else {  // charge state
        printf("Charge state\n");
        chargeState_callback();
        while(DEV_Digital_Read(VBUS)) {
            measureVBAT();
            check_serial_commands();
            
            #if enChargingRtc
            // RTC interrupt trigger
            // this never seems to fire
            if(!DEV_Digital_Read(RTC_INT)) {
                printf("rtc interrupt\r\n");
                set_next_alarm();
                run_display(isCard);
            }
            #endif

            // KEY pressed
            // this seems to fire for RTC interrupt too (at least when HW switch is on)
            if(!DEV_Digital_Read(BAT_STATE)) {
                printf("key interrupt\r\n");
                set_next_alarm();
                run_display(isCard);
            }
            DEV_Delay_ms(200);
        }
    }
    
    printf("power off ...\r\n");
    powerOff(); // BAT off

    return 0;
}
