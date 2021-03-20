/******************************************************************************
 * BLE data advertiser
 *****************************************************************************/

/******************************************************************************
 * INCLUDES
 *****************************************************************************/

#include <pthread.h>
#include <stdarg.h> 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/msg.h>

/******************************************************************************
 * USER DEFINED TYPES
 *****************************************************************************/

typedef struct {
    long nMessage;
} MESSAGE;

typedef struct {
    char * ble_cmd;
    int (*action)(void);
}CMD;
 
/******************************************************************************
 * CONSTANTS
 *****************************************************************************/
 
#define MESSAGE_POLL  1
#define MESSAGE_STOP  2

#define MSG_KEY_IP_BROADCAST (0x6109434c)
#define MESSAGE_SIZE (sizeof(MESSAGE) - sizeof(long))

static const char * BLUETOOTH_DEVICE = "hci0";
static const char * HCI_CONFIG = "hciconfig";
static const char * HCI_TOOL = "hcitool";
static const char * UP = "up";
static const char * DOWN = "down";
static const char * BLE_STOP_ADV = "noleadv";
static const char * BLE_ADV = "leadv";
static char DEV_NAME[10] = "KDalsania";

/******************************************************************************
 * FUNCTION DECLARATION
 *****************************************************************************/
 
static int start(void);
static int stop(void);
static int help(void);

/******************************************************************************
 * LOCALS
 *****************************************************************************/
 
static int g_quit = 0;
static int g_hEngine = -1;

CMD cmd_list[] = {
    {"start", start},
    {"stop", stop},
    {"help", help},
    {NULL}
};

/******************************************************************************
 * FUNCTION DEFINATIONs
 *****************************************************************************/

static int getIPAddress(char * interface, char * ipAddress, char * mask)
{
    struct ifaddrs *interfaces = NULL;
    
    int success = -1;

    // retrieve the current interfaces - returns 0 on success
    if (getifaddrs(&interfaces) == 0)
    {
        // Loop through linked list of interfaces
        
        for(struct ifaddrs *temp_addr = interfaces; temp_addr != NULL; temp_addr = temp_addr->ifa_next)
        {
            if(temp_addr->ifa_addr==NULL)
                continue;
            
            if(temp_addr->ifa_addr->sa_family == AF_INET)
            {
                // Check if interface is en0 which is the wifi connection on the iPhone
                if(strcmp(temp_addr->ifa_name, interface)==0)
                {
                    // Load IP
                    char addr[16];
                    strcpy (addr, inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr));

                    int ip = inet_addr(addr);

                    for (int i=0; i<4; i++)
                    {
                        unsigned int digit = (ip>>(i*8)) & 0xFF;
                        char * tempAddr = &ipAddress[i*3];
                        if(digit < 16)
                        {
                            sprintf(tempAddr, "0%x", digit);
                        }
                        else
                        {
                            sprintf(tempAddr, "%x", digit);
                        }

                        if(i!=3)
                        {
                            sprintf(tempAddr+2, " ", digit);
                        }
                    }

                    //Load Mask

                    char subnet[16];
                    strcpy (subnet, inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_netmask)->sin_addr));

                    int netmask = inet_addr(subnet);

                    for (int i=0; i<4; i++)
                    {
                        unsigned int digit = (netmask>>(i*8)) & 0xFF;
                        char * tempAddr = &mask[i*3];
                        if(digit < 16)
                        {
                            sprintf(tempAddr, "0%x", digit);
                        }
                        else
                        {
                            sprintf(tempAddr, "%x", digit);
                        }

                        if(i!=3)
                        {
                            sprintf(tempAddr+2, " ", digit);
                        }
                    }

                    success = 0;
                    break;
                }
            }
        }
    }
    
    // Free memory
    freeifaddrs(interfaces);
    return success;
}

static int message(int nMessage) {

// queue found?
  int hQueue = msgget(MSG_KEY_IP_BROADCAST, 0666);

  if (hQueue < 0) {
    printf("Advertiser state machine not running\n");
    return -1;
  }

  // prep message
  MESSAGE msg;
  memset(&msg, 0, sizeof(MESSAGE));

  msg.nMessage = nMessage;

  // send message?
  if (msgsnd(hQueue, &msg, MESSAGE_SIZE, 0) < 0) {
    printf("failed to queue message\n");
    return -1;
  }
  // printf("MSG:%d send",nMessage);
  return 0;
}

static int ble_adv_stop(void)
{
    char command [100];

    // Stop advertisement
    memset(command, 0, sizeof(command));
    sprintf(command, "%s %s %s", HCI_CONFIG, BLUETOOTH_DEVICE, BLE_STOP_ADV);
    printf("%s\n",command);
    system(command);

    return 0;
}

static int ble_adv_start(void)
{
    // Advertising Data Flags (not part of AltBeacon standard)
    char * AD_Length_Flags = "02";    // Length of Flags AD structure in bytes
    char * AD_Type_Flags   = "01";    // Type of AD structure as Flags type
    char * AD_Data_Flags   = "1a";    // Flags data LE General Discoverable
    
    // AltBeacon Advertisement
    char * AD_Length_Data = "1b";    // Length of Data AD structure in bytes
    char * AD_Type_Manufacturer_Specific_Data = "ff";    // Type of AD structure as Manufacturer Specific Data
    char * AD_Data_Company_Identifier = "18 01";   //Company identifier (little endian). Radius Networks ID used for example (0x0118). Substitute your assigned manufacturer code.
    char * AD_Data_Proximity_Type = "be ac";    // AltBeacon advertisement code.  Big endian representation of 0xBEAC
    
    //#ethIp:"10.10.20.37/24",wifiIp:"10.10.20.37/24",ethConType:"Static",wifiConType:"DHCP", ethMac:"XYZ", wifiMac:"ABC"}
    //# Note: the 20-byte beacon identifier has been subdivided into three parts in this example for interoperability
    
    //                    ---ethIP--- --ethMask-- --wlanIP--- --wlanMsk--
    char AD_Data_ID1[] = "00 00 00 00 FF FF FF FF 00 00 00 00 FF FF FF FF 00 00 00 00";    // Organizational identifier as 16-byte UUID value
    char ethAddress[] = "00 00 00 00";
    char ethMask[] = "ff ff ff ff";
    getIPAddress("eth0", ethAddress, ethMask);
    printf("ethAddress = %s ethMask = %s\n", ethAddress, ethMask);
    strncpy(&AD_Data_ID1[0], ethAddress, 11);
    strncpy(&AD_Data_ID1[12], ethMask, 11);
    
    char wlanAddress[] = "00 00 00 00";
    char wlanMask[] = "ff ff ff ff";
    getIPAddress("wlan0", wlanAddress, wlanMask);
    printf("wlanAddress = %s wlanMask = %s\n", wlanAddress, wlanMask);
    strncpy(&AD_Data_ID1[24], wlanAddress, 11);
    strncpy(&AD_Data_ID1[36], wlanMask, 11);
    
    char * AD_Data_Reference_RSSI = "c5";    // Signed 1-byte value representing the average received signal strength at 1m from the advertiser
    char * AD_Data_Manufacturer_Reserved = "11";    // Reserved for use by manufacturer to implement special features
    
    char command [500];
    char AD_Flags[9]; 
    char AltBeacon_Advertisement[84];
    
    // Set up advertising data flags
    sprintf (AD_Flags, "%s %s %s", AD_Length_Flags, AD_Type_Flags, AD_Data_Flags);
    printf("AD_Flags: %s\n", AD_Flags);
    
    // Set up AltBeacon advertisement
    sprintf (AltBeacon_Advertisement, "%s %s %s %s %s %s %s", AD_Length_Data, AD_Type_Manufacturer_Specific_Data, AD_Data_Company_Identifier, AD_Data_Proximity_Type, AD_Data_ID1, AD_Data_Reference_RSSI, AD_Data_Manufacturer_Reserved);
    printf("AltBeacon_Advertisement: %s\n", AltBeacon_Advertisement);
    
    // Command Bluetooth device up
    //sudo hciconfig $BLUETOOTH_DEVICE up
    memset(command, 0, sizeof(command));
    sprintf(command, "%s %s %s", HCI_CONFIG, BLUETOOTH_DEVICE, UP);
    printf("%s\n",command);
    system(command);
    
    //# set ble device name
    //sudo hciconfig $BLUETOOTH_DEVICE name EAC19May19
    memset(command, 0, sizeof(command));
    char hostname[17];
    if(gethostname(hostname, 17)==0)
    {   
        printf("HOSTNAME = %s\n",hostname);
        strncpy(DEV_NAME, hostname, 4);
        strcpy(&DEV_NAME[4], &hostname[10]);
        printf("DEV_NAME = %s\n",DEV_NAME);
    }
    sprintf(command, "%s %s name %s", HCI_CONFIG, BLUETOOTH_DEVICE, DEV_NAME);
    printf("%s\n",command);
    system(command);
    
    //sudo hciconfig $BLUETOOTH_DEVICE down
    memset(command, 0, sizeof(command));
    sprintf(command, "%s %s %s", HCI_CONFIG, BLUETOOTH_DEVICE, DOWN);
    printf("%s\n",command);
    system(command);
    
    //sudo hciconfig $BLUETOOTH_DEVICE up
    memset(command, 0, sizeof(command));
    sprintf(command, "%s %s %s", HCI_CONFIG, BLUETOOTH_DEVICE, UP);
    printf("%s\n",command);
    system(command);
    
    // Stop LE advertising
    //sudo hciconfig $BLUETOOTH_DEVICE noleadv
    memset(command, 0, sizeof(command));
    sprintf(command, "%s %s %s", HCI_CONFIG, BLUETOOTH_DEVICE, BLE_STOP_ADV);
    printf("%s\n",command);
    system(command);
    
    // HCI_LE_Set_Advertising_Data command
    // Command parameters: Advertising_Data_Length (1f), Advertising_Data
    //sudo hcitool -i $BLUETOOTH_DEVICE cmd 0x08 0x0008 1f $AD_Flags $AltBeacon_Advertisement
    memset(command, 0, sizeof(command));
    sprintf(command, "%s -i %s cmd 0x08 0x0008 1f %s %s", HCI_TOOL, BLUETOOTH_DEVICE, AD_Flags, AltBeacon_Advertisement);
    printf("%s\n",command);
    system(command);
    
    // Start LE advertising (non-connectable)
    //sudo hciconfig $BLUETOOTH_DEVICE leadv 3
    //sudo hciconfig $BLUETOOTH_DEVICE leadv
    memset(command, 0, sizeof(command));
    sprintf(command, "%s %s %s", HCI_CONFIG, BLUETOOTH_DEVICE, BLE_ADV);
    printf("%s\n",command);
    system(command);
    
    return 0;
}

static int help(void)
{
    printf("advertiser\n");
    printf("start    --> start advertisement\n");
    printf("stop     --> stop advertisement\n");
    
    return 0;
}

static void * poll(void *arg)
{
    int nPeriod = 30000; // msec
    
    // micro seconds sleep
    useconds_t usecs = nPeriod * 1000;

    // enter main loop
    while (g_quit == 0) 
    {    
        // signal process to poll
        message(MESSAGE_POLL);

        // sleep for a bit
        usleep(usecs);
        // fflush(stdout);
        // fflush(stderr);
    }
    
    return 0;
}

static int loop(pthread_t thread)
{
    MESSAGE msg;
    
    while(g_quit == 0)
    {
        int err = msgrcv(g_hEngine, &msg, MESSAGE_SIZE, 0, 0);
        switch (msg.nMessage)
        {
            case MESSAGE_POLL:
                ble_adv_start();
                break;
                
            case MESSAGE_STOP:
                ble_adv_stop();
                // Stop the thread
                g_quit = 1;
                // destroy thread
                pthread_cancel(thread);
                break;
                
            default:
                printf("ble info command not supported\n");
                break;
        }
    }
    
    return 0;
}

static int start() {
  
    printf("*** START ***\n");
    // queue found?

	// create queue?
	g_hEngine = msgget(MSG_KEY_IP_BROADCAST, IPC_CREAT | 0666);

	if (g_hEngine < 0) {
	  printf("failed to create message queue\n");
	  return -1;
	}

    // fork child process
    printf("Creating daemon\n");
    pid_t pid = fork();

    if (pid < 0) {
        printf("failed to create process fork\n");
        return -1;
    }

    // stop parent process
    if (pid > 0)
        return 0;

    // int fd;
    printf("Log file set\n");

    // start polling thread?
    pthread_t pollthread;

    printf("Creating thread\n");

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    
    if (pthread_create(&pollthread, NULL, poll, NULL) < 0) {
        printf("failed to create poll thread\n");
        return -1;
    }

    printf("running loop\n");

    // run engine loop
    int err = loop(pollthread);

    // wait for poll thread
    if (pthread_join(pollthread, NULL) < 0) {
        printf("failed to join poll thread\n");
        return -1;
    }
    
    // kill queue
    if (msgctl(g_hEngine, IPC_RMID, 0) < 0) {
        printf("failed to destroy message queue\n");
        return -1;
    }

    // fclose(fd);
    return err;
}

static int stop()
{
    printf("engine_stop 1\n");
    int ret = message(MESSAGE_STOP);
 
    if (ret == 0) {
        printf("stop send success\n");
    } else {
        printf("stop send failed\n");
    }

    return ret;
}

int main(int argc, char *argv[])
{
    int found = 0;

    if(argc<2)
    {
        help();
        return 0;
    }
    
    for (int cmd_idx = 0; cmd_list[cmd_idx].ble_cmd!=NULL; cmd_idx++)
    {
        if(strcmp(argv[1], cmd_list[cmd_idx].ble_cmd) == 0)
        {
            found = 1;
            (cmd_list[cmd_idx].action)();
            break;
        }
    }

    if(!found) {
        printf("Failed: %d not found\n", argv[1]);
        help();
    }

    return 0;
}
