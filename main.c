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

// File system is:
// {DiskType - Time, Hour, Minute}
// or
// {DiskType - Variety, Quantity, Variety}

#define NUM_REDUNDANT_DATA 100
#define DATA_ENTRY_SIZE 3
#define DATA_OFFSET 1000

#define DISK "/dev/sda"

typedef enum {TIME = 0, VARIETY = 1} DiskType;
typedef enum {ESPRESSO = 2, AMERICANO = 3} Variety;

typedef struct {
    char type;
    union {
        struct {
            char hour;
            char minute;
            };
        struct {
            char quantity;
            char variety;
        };
    } detail;
} DataEntry;

//build gcc main.c -std=c99 -I/usr/include/dbus-1.0 -I/usr/lib/arm-linux-gnueabihf/dbus-1.0/include/ -I/usr/include/glib-2.0 -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include/ -ldbus-1 -ldbus-glib-1 -lwiringPi -Wall -Wextra -o bin/main

//Writes 3 bytes of data over and over
void writeDisk(char data[])
{
    int f = open(DISK, O_WRONLY);
    if(f < 0) {
        printf("Couldn't open disk1 for write. Check write protect switch. Error: %s\n", strerror(errno));
        return;
    }
    
    if(lseek(f, DATA_OFFSET, SEEK_SET) < 0) {
        printf("Seek error\n");
        return;
    }
    
    for(int i = 0; i < NUM_REDUNDANT_DATA; ++i) {
        if(write(f, data, DATA_ENTRY_SIZE) < 0){
            printf("Write error: %s\n", strerror(errno));
            return;
        }
    }
    
    close(f);
}

//Reads all data and takes average of each
void readDisk(DataEntry* result)
{
    int f = open(DISK, O_RDONLY);
    if(f < 0) {
        printf("Couldn't open disk1 for read\n");
        return;
    }
    
    if(lseek(f, DATA_OFFSET, SEEK_SET) < 0) {
        printf("Seek error\n");
        return;
    }
    
    char data[DATA_ENTRY_SIZE*NUM_REDUNDANT_DATA];
    if(read(f, data, DATA_ENTRY_SIZE*NUM_REDUNDANT_DATA) < 0){
        printf("Read error: %s\n", strerror(errno));
        return;
    }
    
    long averageResult[DATA_ENTRY_SIZE] = {0, 0, 0};
    for(int i = 0; i < sizeof(data); ++i) {
        averageResult[i % DATA_ENTRY_SIZE] += data[i];
    }
    
    result->type = averageResult[0] / NUM_REDUNDANT_DATA;
    
    
    close(f);
}

void createTimeDisk(char hour, char minute)
{
    char data[DATA_ENTRY_SIZE];
    data[0] = TIME;
    data[1] = hour;
    data[2] = minute;
    
    writeDisk(data);
}

void createVarietyDisk(char quantity, Variety variety)
{
    char data[DATA_ENTRY_SIZE];
    data[0] = VARIETY;
    data[1] = quantity;
    data[2] = variety;
    
    writeDisk(data);
}

void decodeDiskInfo()
{
/*
    char result[DATA_ENTRY_SIZE];
    readDisk(result);
    
    printf("-----Disk-----\nType: ");
    if(result[0] == TIME) {
        printf("Time\n");
        printf("%dh %dm\n", result[1], result[2]);
    }
    else {
        printf("Variety\n");
        printf("%dx %s\n", result[1], result[2] == ESPRESSO ? "Espresso" : "Americano");
    }*/
}

static GMainLoop *loop;

static void
device_removed (DBusGProxy *proxy,
         char *ObjectPath[],
         char *word_eol[],
         guint hook_id,
         guint context_id,
         gpointer user_data)
{
    printf("device changed: %s\n", (char*)word_eol);
    printf("Removed Device\nObjectPath: %s\nhook_id: %d\ncontext_id: %d\nuser_data: %d\n\n", (char*)ObjectPath, hook_id, context_id, (int)user_data); 
}

void testUDisks()
{
DBusGConnection *connection;
  DBusMessage* message;
  GError *error;
  DBusGProxy *proxy;
  gchar *m1;
  gchar *m2;
  char *object_path;
  GPtrArray* ret;

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
     else
       printf("Probably got a connection to the correct interface...\n");
//It works for me without marshaler register, add and connect to the signals directly
     m1=g_cclosure_marshal_VOID__STRING;
     m2=g_cclosure_marshal_VOID__STRING;

     dbus_g_object_register_marshaller(m1,G_TYPE_NONE,G_TYPE_STRING,G_TYPE_INVALID);

     dbus_g_object_register_marshaller(m2,G_TYPE_NONE,G_TYPE_STRING,G_TYPE_INVALID);

    

     dbus_g_proxy_add_signal(proxy,"DeviceChanged",DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
     dbus_g_proxy_connect_signal(proxy,"DeviceChanged",(GCallback)device_removed,NULL,NULL);

     loop=g_main_loop_new(NULL,FALSE);

     g_main_loop_run (loop);

     g_error_free (error);
}

bool isDiskIn()
{
    printf("isDiskIn? ");
    int f = open(DISK, O_RDONLY);
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

void monitorDisks()
{
    //If there is a variety disk in when program starts, make the coffee.
    if(isDiskIn()) {
        DataEntry result;
        readDisk(&result);
        if(result.type == VARIETY) {
            printf("Started with a variety disk in\n");
        }
        else {
            printf("Started with a time disk, doing nothing\n");
        }
    }
}

void createDisk(int argc, const char * argv[])
{
    if(argc != 5) goto arg_error;
   
    DataEntry entry;
    if(!strcmp(argv[2], "time")) entry.type = TIME;
    else if(!strcmp(argv[2], "variety")) entry.type = VARIETY;
    else goto arg_error;
       
    if(entry.type == TIME) {
        entry.detail.hour = atoi(argv[3]);
        entry.detail.minute = atoi(argv[4]);
        
        if(entry.detail.hour < 0 || entry.detail.hour > 23) goto arg_error;
        if(entry.detail.minute < 0 || entry.detail.minute > 59) goto arg_error;
    }
    else {
        entry.detail.quantity = atoi(argv[3]);
        
        if(!strcmp(argv[4], "espresso")) entry.detail.variety = ESPRESSO;
        else if(!strcmp(argv[4], "americano")) entry.detail.variety = AMERICANO;
        else goto arg_error;
    }
        
    return;
    arg_error:
    printf("Argument error. Use --create-disk time <hour> <minute>\n \
    OR --create-disk variety <quantity> espresso or americano\n\n");  
}

int main(int argc, const char * argv[])
{
    if(argc <= 1) {
        printf("Not enough arguments. Use --monitor-disks or --make-coffee\n");
        return;
    }

    if(!strcmp(argv[1], "--monitor-disks")) {
        printf("Starting disk monitor");
        monitorDisks();
        //testUDisks();
    }
    else if(!strcmp(argv[1], "--make-coffee")) {
        wiringPiSetup () ;
        const int pin1 = 15;
        const int pin2 = 16;
        pinMode (pin1, OUTPUT);
        pinMode (pin2, OUTPUT);
        while(1) {
            digitalWrite(pin1, HIGH);
            delay(500);
            digitalWrite(pin2, HIGH);
            delay(200);
            digitalWrite(pin1, LOW);
            delay(150);
            digitalWrite(pin2, LOW);
            delay(200);
        }
    }
    else if(!strcmp(argv[1], "--create-disk")) {
        createDisk(argc, argv);
    }
    
    createVarietyDisk(3, ESPRESSO);
    createTimeDisk(12, 34);

    return 0;
}


