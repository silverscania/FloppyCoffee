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

// File system is:
// {DiskType - Time, Hour, Minute}
// or
// {DiskType - Variety, Quantity, Variety}

#define NUM_REDUNDANT_DATA 100
#define DATA_ENTRY_SIZE 3
#define DATA_OFFSET 20000

#define ON LOW
#define OFF HIGH

config_t config;
const char* drive;
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

//Writes 3 bytes of data over and over
void writeDisk(DataEntry data)
{
    int f = open(drive, O_WRONLY);
    if(f < 0) {
        printf("Couldn't open disk1 for write. Check write protect switch. Error: %s\n", strerror(errno));
        return;
    }
    
    if(lseek(f, DATA_OFFSET, SEEK_SET) < 0) {
        printf("Seek error\n");
        return;
    }
    
    //Convert struct to 3 byte array
    schar dataArray[DATA_ENTRY_SIZE];
    dataArray[0] = data.type;
    dataArray[1] = data.detail.hour;
    dataArray[2] = data.detail.minute;
    
    for(int i = 0; i < NUM_REDUNDANT_DATA; ++i) {
        if(write(f, dataArray, DATA_ENTRY_SIZE) < 0){
            printf("Write error: %s\n", strerror(errno));
            return;
        }
    }
    
    printf("Disk write successful!\n");
    close(f);
}

//Reads all data and takes average of each
void readDisk(DataEntry* result)
{
    int f = open(drive, O_RDONLY);
    if(f < 0) {
        printf("Couldn't open disk1 for read\n");
        return;
    }
    
    if(lseek(f, DATA_OFFSET, SEEK_SET) < 0) {
        printf("Seek error\n");
        return;
    }
    
    schar data[DATA_ENTRY_SIZE*NUM_REDUNDANT_DATA];
    if(read(f, data, DATA_ENTRY_SIZE*NUM_REDUNDANT_DATA) < 0){
        printf("Read error: %s\n", strerror(errno));
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
    printf("-----Disk-----\nType: ");
    if(data.type == TIME) {
        printf("Time\n");
        printf("%dh %dm\n", data.detail.hour, data.detail.minute);
    }
    else {
        printf("Variety\n");
        printf("%dx %s\n", data.detail.quantity, data.detail.variety == ESPRESSO ? "Espresso" : "Americano");
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
    
    printf("\nSaved disk in file:\n");
    printDiskInfo(disk);
    printf("\n");
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
    
    printf("\nLoaded saved disk:\n");
    printDiskInfo(savedDiskTime);
    printDiskInfo(savedDiskVariety);
    printf("\n");
}

bool isDiskIn()
{
    printf("isDiskIn? ");
    int f = open(drive, O_RDONLY);
    if(f < 0) {
        printf("No. Error: %s\n", strerror(errno));
        return false;
    }
    else {
        printf("Yes.\n");
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
    // Use saved disk data to create cron job
    //# m h  dom mon dow command
    //18 17 * * * touch ~/blah
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));

    char data[2048];
    sprintf(data, "%d %d * * * cd %s && sudo bin/main --make-coffee >> logs\n", savedDiskTime.detail.hour, savedDiskTime.detail.minute, cwd);
    FILE* f = fopen("cronjob", "w");
    fputs(data, f);
    fclose(f);
    
    system("crontab cronjob");
}

void makeDrink(DataEntry drink)
{
    if(drink.type != VARIETY) {
        printf("Error, bad params in function call makeDrink\n");
        return; 
    }
    
    printf("Making:\n");
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
        printf("Added\n");
        
        DataEntry result;
        readDisk(&result);
        
        //check for now disk
        if(result.type == TIME && (result.detail.hour < 0 || result.detail.minute < 0)) {
            beep();
            makeDrink(savedDiskVariety);
        }
        else {
            saveDisk(result);
            setupCronJob();
            beep();
        }
    }
    
    if(!diskInTest && diskIn){
        diskIn = false;
        printf("removed\n");
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
        printf("Got a connection to DBUS_BUS_SYSTEM\n");
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
        printf("Probably got a connection to the correct interface...\n");
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
        //    printf("Started with a variety disk in.\n");
        //    makeDrink(result);
        // }
        //else {
        //    printf("Started with a time disk, doing nothing\n");
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
        printf("invalid number of arguments\n");
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
            printf("hours invalid range\n");
            goto arg_error;
        }
        if(entry.detail.minute < -1 || entry.detail.minute > 59) goto arg_error;
    }
    else {
        entry.detail.quantity = atoi(argv[3]);
        if(entry.detail.quantity < 1 || entry.detail.quantity > 2) {
            printf("Cant make less than 1 or more than 2 drinks...\n"); 
            goto arg_error;
        }
        
        if(!strcmp(argv[4], "espresso")) entry.detail.variety = ESPRESSO;
        else if(!strcmp(argv[4], "americano")) entry.detail.variety = AMERICANO;
        else goto arg_error;
    }
        
    printf("Creating:\n");
    printDiskInfo(entry);
    writeDisk(entry);
    return;
    
    arg_error:
    printf("Argument error. Use --create-disk time <24 hour (-1 for now)s> <minute>\n \
OR --create-disk variety <quantity> espresso or americano\n\n");  
}

int readConfigFile() 
{
    config_init(&config);
    /* Read the file. If there is an error, report it and exit. */
    if(! config_read_file(&config, "config"))
    {
        printf("%s:%d - %s\n", config_error_file(&config),
                config_error_line(&config), config_error_text(&config));
        goto fail;
    }
    
    if(config_lookup_string(&config, "drive", &drive) == CONFIG_FALSE) goto fail;
    printf("%s\n", drive);
    if(config_lookup_int(&config, "warmupSecs", &warmupSecs) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "espressoSecs", &espressoSecs) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "americanoSecs", &americanoSecs) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "waterPin", &pinWater) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "powerPin", &pinPower) == CONFIG_FALSE) goto fail;
    if(config_lookup_int(&config, "beepPin", &beepPin) == CONFIG_FALSE) goto fail;
    
    printf("Config read successfully\n");
    return 0;
    
    fail:
    printf("Parse config file error\n");
    config_destroy(&config);
    return 1;
}

int main(int argc, const char * argv[])
{    
    if(argc <= 1) {
        printf("Not enough arguments. Use --monitor-disks or --make-coffee or --create-disk\n");
        goto fail_main;
    }

    if(readConfigFile()) {
        goto fail_main;
    }
 
    loadSavedDisk();
    setupCronJob();
    
    //setup beep pin
    wiringPiSetup();
    setPadDrive(0, 7); //max current mode
    //softPwmCreate(beepPin, 0, 10);
    //beep();
      
    if(!strcmp(argv[1], "--monitor-disks")) {
        printf("Starting disk monitor\n");
        monitorDisks();
    }
    else if(!strcmp(argv[1], "--make-coffee")) {
        makeDrink(savedDiskVariety);
    }
    else if(!strcmp(argv[1], "--create-disk")) {
        createDisk(argc, argv);
    }
    else {
        printf("Command not recognised.\n");
        goto fail_main;
    }
    
    config_destroy(&config);
    return 0;
    
    fail_main:
    config_destroy(&config);
    return 1;
}


