 //
//  main.c
//  FloppyCoffee
//
//  Created by PingPong on 24/05/2014.
//  Copyright (c) 2014 BitsOfBeards. All rights reserved.
//

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dbus/dbus.h>       // for dbus_*   
#include <dbus/dbus-glib.h>  // for dbus_g_*
#include <wiringPi.h>
#include <stdbool.h>
#include <stdlib.h>
#include <libconfig.h>
#include <softPwm.h>
#include <time.h>

// File system is:
// {DiskType - Time, Hour, Minute}
// or
// {DiskType - Variety, Quantity, Variety}

#define NUM_REDUNDANT_DATA 50
#define DATA_ENTRY_SIZE 3
#define DATA_OFFSET 20000

#define ON LOW
#define OFF HIGH

#define Log(s, args...) logPrnt(__func__);  printf(s, ##args); printf("\n");

config_t config;
const char* drive;
const char* arg1;

int warmupSecs;
int espressoSecs;
int americanoSecs;
int pinPower;
int pinWater;
int beepPin; 

GMainLoop* loop;
bool diskIn;

typedef enum {TIME = 0, VARIETY = 1} DiskType;
typedef enum {ESPRESSO = 2, AMERICANO = 3} Variety;
typedef signed char schar;

typedef struct {
    schar type;
    union {
        struct {
            schar hour;
            schar minute;
            };
        struct {
            schar quantity;
            schar variety;
        };
    } detail;
} DataEntry;

DataEntry savedDiskTime, savedDiskVariety;

//build gcc main.c -std=c99 -I/usr/include/dbus-1.0 -I/usr/lib/arm-linux-gnueabihf/dbus-1.0/include/ -I/usr/include/glib-2.0 -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include/ -ldbus-1 -ldbus-glib-1 -lwiringPi -lconfig -lpthread -Wall -o bin/main

void logPrnt(const char* func) 
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    printf("[a:%s d:%d/%d t:%d:%d:%d f:%s]   ", arg1, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, func);
}

//Writes 3 bytes of data over and over
void writeDisk(DataEntry data)
{
    int f = open(drive, O_WRONLY);
    if(f < 0) {

        Log("Couldn't open disk1 for write. Check write protect switch. Error: %s", strerror(errno));
        return;
    }
    
    if(lseek(f, DATA_OFFSET, SEEK_SET) < 0) {
        Log("Seek error");
        return;
    }
    
    //Convert struct to 3 byte array
    schar dataArray[DATA_ENTRY_SIZE];
    dataArray[0] = data.type;
    dataArray[1] = data.detail.hour;
    dataArray[2] = data.detail.minute;
    
    for(int i = 0; i < NUM_REDUNDANT_DATA; ++i) {
        if(write(f, dataArray, DATA_ENTRY_SIZE) < 0){
            Log("Write error: %s", strerror(errno));
            return;
        }
    }
    
    Log("Disk write successful!");
    close(f);
}

//Reads all data and takes average of each
void readDisk(DataEntry* result)
{
    int f = open(drive, O_RDONLY);
    if(f < 0) {
        Log("Couldn't open disk1 for read");
        return;
    }
    
    if(lseek(f, DATA_OFFSET, SEEK_SET) < 0) {
        Log("Seek error");
        return;
    }
    
    schar data[DATA_ENTRY_SIZE*NUM_REDUNDANT_DATA];
    if(read(f, data, DATA_ENTRY_SIZE*NUM_REDUNDANT_DATA) < 0){
        Log("Read error: %s", strerror(errno));
        return;
    }
    
    long averageResult[DATA_ENTRY_SIZE] = {0, 0, 0};
    for(int i = 0; i < sizeof(data); ++i) {
        averageResult[i % DATA_ENTRY_SIZE] += data[i];
    }
    
    result->type = averageResult[0] / NUM_REDUNDANT_DATA;
    result->detail.hour = averageResult[1] / NUM_REDUNDANT_DATA;
    result->detail.minute = averageResult[2] / NUM_REDUNDANT_DATA;
    
    close(f);
}

void printDiskInfo(DataEntry data) 
{
    Log("-----Disk----- Type: ");
    if(data.type == TIME) {
        Log("Time");
        Log("%dh %dm", data.detail.hour, data.detail.minute);
    }
    else {
        Log("Variety");
        Log("%dx %s", data.detail.quantity, data.detail.variety == ESPRESSO ? "Espresso" : "Americano");
    }
}

void saveDisk(DataEntry disk)
{
    //Last disk file contains {Hour, Minute, Quantity, Variety}
    FILE* f = fopen("lastDisk", "r+");
    
    if(disk.type == TIME) {
        fseek(f, 0, SEEK_SET);
    }
    else {
        fseek(f, 2, SEEK_SET);
    }
    
    schar data[] = {disk.detail.hour, disk.detail.minute};
    fwrite(data, sizeof(schar), sizeof(data), f);
    fclose(f);
    
    Log("Saved disk in file:");
    printDiskInfo(disk);
}

void loadSavedDisk()
{
    FILE* f = fopen("lastDisk", "r");
    
    schar data[4];
    fread(data, sizeof(schar), sizeof(data), f);
    fclose(f);
    
    savedDiskTime.type = TIME;
    savedDiskTime.detail.hour = data[0];
    savedDiskTime.detail.minute = data[1];
    
    savedDiskVariety.type = VARIETY;
    savedDiskVariety.detail.quantity = data[2];
    savedDiskVariety.detail.variety = data[3];
    
    Log("Loaded saved disk:");
    printDiskInfo(savedDiskTime);
    printDiskInfo(savedDiskVariety);
}

bool isDiskIn()
{
    Log("isDiskIn? ");
    int f = open(drive, O_RDONLY);
    if(f < 0) {
        Log("No. Error: %s", strerror(errno));
        return false;
    }
    else {
        Log("Yes.");
        close(f);
        return true;
    }
}

void beep()
{
    //softPwmWrite(beepPin, 5);
    //delay(200);
    //softPwmWrite(beepPin, 0);
    
    pinMode(beepPin, OUTPUT);
    for(int i = 0; i < 100; ++i) {
        digitalWrite(beepPin, HIGH);
        delayMicroseconds(1000);
        digitalWrite(beepPin, LOW);
        delayMicroseconds(1000);
    }
    
    for(int i = 0; i < 110; ++i) {
        digitalWrite(beepPin, HIGH);
        delayMicroseconds(500);
        digitalWrite(beepPin, LOW);
        delayMicroseconds(500);
    }
    
    for(int i = 0; i < 150; ++i) {
        digitalWrite(beepPin, HIGH);
        delayMicroseconds(400);
        digitalWrite(beepPin, LOW);
        delayMicroseconds(400);
    }
    //pinMode (pinWater, OUTPUT);
    //pinMode (pinPower, OUTPUT);
    //digitalWrite(pinWater, OFF);
}

void beepStartup()
{
    pinMode(beepPin, OUTPUT);
    for(int j = 0; j < 3; ++j) {
        for(int i = 0; i < 100; ++i) {
            digitalWrite(beepPin, HIGH);
            delayMicroseconds(500);
            digitalWrite(beepPin, LOW);
            delayMicroseconds(500);
        }
        delay(50);
    }
}

void setupCronJob() 
{
    Log(" ");

    // Use saved disk data to create cron job
    //# m h  dom mon dow command
    //18 17 * * * touch ~/blah
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));

    char data[2048];
    sprintf(data, "%d %d * * * cd %s && stdbuf -oL bin/main --make-coffee | tee -a cronLog\n", savedDiskTime.detail.minute, savedDiskTime.detail.hour, cwd);
    FILE* f = fopen("cronjob", "w");
    fputs(data, f);
    fclose(f);
    
    system("crontab cronjob");
}

void makeDrink(DataEntry drink)
{
    if(drink.type != VARIETY) {
        Log("Error, bad params in function call makeDrink");
        return; 
    }
    
    Log("Making:");
    printDiskInfo(drink);
    
    //Warmup
    pinMode (pinWater, OUTPUT);
    pinMode (pinPower, OUTPUT);
    digitalWrite(pinWater, OFF);
    digitalWrite(pinPower, ON);
    sleep(warmupSecs);
    
    //Make drink
    digitalWrite(pinWater, ON);
    int drinkTime = drink.detail.variety == ESPRESSO ? espressoSecs : americanoSecs;
    sleep(drinkTime * drink.detail.quantity);
    
    //All off
    digitalWrite(pinWater, OFF);
    digitalWrite(pinPower, OFF);
    pinMode(pinWater, INPUT);
    pinMode(pinPower, INPUT);
}

void device_changed (DBusGProxy *proxy,
         char *ObjectPath[],
         char *word_eol[],
         guint hook_id,
         guint context_id,
         gpointer user_data)
{
    bool diskInTest = isDiskIn();
    if(diskInTest && !diskIn) {
        diskIn = true;
        Log("Added");
        
        DataEntry result;
        readDisk(&result);
        
        //check for now disk
        if(result.type == TIME && (result.detail.hour < 0 || result.detail.minute < 0)) {
            loadSavedDisk();
            beep();
            makeDrink(savedDiskVariety);
        }
        else {
            saveDisk(result);
            loadSavedDisk();
            setupCronJob();
            beep();
        }
    }
    
    if(!diskInTest && diskIn){
        diskIn = false;
        Log("removed");
    }
}

void startUDisks()
{
DBusGConnection *connection;
  GError *error;
  DBusGProxy *proxy;
  g_type_init ();

  error = NULL;
  connection = dbus_g_bus_get(DBUS_BUS_SYSTEM,NULL);

  if (connection == NULL)
    {
      g_printerr ("Failed to open connection to bus: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }
    else
    {
        Log("Got a connection to DBUS_BUS_SYSTEM");
    }


  /* Create a proxy object for the "bus driver" (name "org.freedesktop.DBus") */

     proxy=dbus_g_proxy_new_for_name(connection,"org.freedesktop.UDisks","/org/freedesktop/UDisks","org.freedesktop.UDisks");

     if(proxy == NULL)
     {
        g_printerr ("Failed To Create A proxy...: %s\n", error->message);
        g_error_free (error);
        exit(1);
     }
     else {
        Log("Probably got a connection to the correct interface...");
     }
    
     dbus_g_proxy_add_signal(proxy,"DeviceChanged",DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
     dbus_g_proxy_connect_signal(proxy,"DeviceChanged",(GCallback)device_changed,NULL,NULL);

     loop=g_main_loop_new(NULL,FALSE);

     g_main_loop_run (loop);

     g_error_free (error);
}

void monitorDisks()
{
    beepStartup();
    //If there is a variety disk in when program starts, save the disk. //make the coffee.
    if(isDiskIn()) {
        DataEntry result;
        readDisk(&result);
        //Kind of confusing. wait for 'now' disk to do immediate stuff
        //if(result.type == VARIETY) {
        //    Log("Started with a variety disk in.\n");
        //    makeDrink(result);
        // }
        //else {
        //    Log("Started with a time disk, doing nothing\n");
        //}
        
        //dont save 'now' disks
        if(result.type != TIME || (result.detail.hour >= 0 && result.detail.minute >= 0)) {
            saveDisk(result);
        }
        diskIn = true;
        beep();
    }
    else {
        diskIn = false;
    }
    
    startUDisks();
}

void createDisk(int argc, const char * argv[])
{
    if(argc != 5) {
        Log("invalid number of arguments");
        goto arg_error;
    }
    
    DataEntry entry;
    if(!strcmp(argv[2], "time")) entry.type = TIME;
    else if(!strcmp(argv[2], "variety")) entry.type = VARIETY;
    else goto arg_error;
       
    if(entry.type == TIME) {
        entry.detail.hour = atoi(argv[3]);
        entry.detail.minute = atoi(argv[4]);
        
        if(entry.detail.hour < -1 || entry.detail.hour > 23) {
            Log("hours invalid range");
            goto arg_error;
        }
        if(entry.detail.minute < -1 || entry.detail.minute > 59) goto arg_error;
    }
    else {
        entry.detail.quantity = atoi(argv[3]);
        if(entry.detail.quantity < 1 || entry.detail.quantity > 2) {
            Log("Cant make less than 1 or more than 2 drinks..."); 
            goto arg_error;
        }
        
        if(!strcmp(argv[4], "espresso")) entry.detail.variety = ESPRESSO;
        else if(!strcmp(argv[4], "americano")) entry.detail.variety = AMERICANO;
        else goto arg_error;
    }
        
    Log("Creating:");
    printDiskInfo(entry);
    writeDisk(entry);
    return;
    
    arg_error:
    Log("Argument error. Use --create-disk time <24 hour (-1 for now)s> <minute>\n \
OR --create-disk variety <quantity> espresso or americano\n\n");  
}

int readConfigFile() 
{
    config_init(&config);
    /* Read the file. If there is an error, report it and exit. */
    if(! config_read_file(&config, "config"))
    {
        Log("%s:%d - %s", config_error_file(&config),
                config_error_line(&config), config_error_text(&config));
        goto fail;
    }
    
    if(config_lookup_string(&config, "drive", &drive) == CONFIG_FALSE) goto fail;
    Log("%s", drive);
    if(config_lookup_int(&config, "warmupSecs", &warmupSecs) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "espressoSecs", &espressoSecs) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "americanoSecs", &americanoSecs) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "waterPin", &pinWater) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "powerPin", &pinPower) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "beepPin", &beepPin) == CONFIG_FALSE) goto fail;
    
    Log("Config read successfully");
    return 0;
    
    fail:
    Log("Parse config file error");
    config_destroy(&config);
    return 1;
}

void stopHeater() 
{
    Log(" ");

    //All off
    digitalWrite(pinWater, OFF);
    digitalWrite(pinPower, OFF);
    pinMode(pinWater, INPUT);
    pinMode(pinPower, INPUT);
}

int main(int argc, const char * argv[])
{    
    if(argc <= 1) {
        printf("Not enough arguments. Use --monitor-disks or --make-coffee or --create-disk or --stop-heater\n");
        goto fail_main;
    }

    arg1 = argv[1];

    if(readConfigFile()) {
        goto fail_main;
    }
 
    loadSavedDisk();
    
    //setup beep pin
    wiringPiSetup();
    setPadDrive(0, 7); //max current mode
    //softPwmCreate(beepPin, 0, 10);
    //beep();
      
    if(!strcmp(argv[1], "--monitor-disks")) {
        Log("Starting disk monitor");
        monitorDisks();
    }
    else if(!strcmp(argv[1], "--make-coffee")) {
        makeDrink(savedDiskVariety);
    }
    else if(!strcmp(argv[1], "--create-disk")) {
        createDisk(argc, argv);
    }
    else if(!strcmp(argv[1], "--stop-heater")) {
        stopHeater();
    }
    else {
        Log("Command not recognised.");
        goto fail_main;
    }
    
    config_destroy(&config);
    return 0;
    
    fail_main:
    config_destroy(&config);
    return 1;
}


