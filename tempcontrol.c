//------------------------------------------------------------------------------
//  File: 	tempcontrol.c
//  Date: 	Mar 03, 2021
//  Author:	Wil van Meurs
//------------------------------------------------------------------------------
//
//  Measured temperature is divided into a number of temperature ranges and for
//  each range, specific settings for fan speed and led colors on the Smart 
//  Cooling Hat (DF-DFR0672) are defined.
//
//  The program can run in test mode by supplying a startup argument, either of:
//	tempcontrol -t sweepTemperatures
//	tempcontrol -t sweepTempRanges
//
//  In the first test, the program does not read the actual temperatures, but
//  steps through the values 30 through 65 degrees celsius and controls fan
//  and led setttings accordingly.
//  In the second test, the program steps through the defined temperature ranges
//
//  The OLED display on the Smart Cooling Hat displays the following properties:
//	- CPU utilization
//	- Total RAM and free RAM
//	- Total and free Disk space
//	- IP address
//	- CPU temperature
//
//------------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <fcntl.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/vfs.h>

// Wiring library specifications
#include <wiringPi.h>
#include <wiringPiI2C.h>

#include "ssd1306_i2c.h"

//  This file contains current temperature:
#define TEMP_PATH "/sys/class/thermal/thermal_zone0/temp"

#define MAX_SIZE 	32
#define MAX_LED  	3
#define MAX_RANGE  	7

enum TempRange
{
    TempBelow40 = 0,
    Temp40To45 =  1,
    Temp45To47 =  2,
    Temp47To49 =  3,
    Temp49To51 =  4,
    Temp51To53 =  5,
    TempAbove53 = 6
};

// Gobal values
int 	gFileI2C= 0;

double 	gTemperature = 0.0; // CPU temperature
FILE*	pFileTemperature = NULL;

// Forward declarations
int    	read( int fd, char* buf, int count );
int    	close( int fd );

void   	setRGB( int fd, int num, int R, int G, int B );
void   	closeRGB( int fd );

enum TempRange 	temperatureRange( const double temperature );
int	init();
int 	setTempControls( enum TempRange tempRange, bool verbose );
int	runControlLoop();	// Normal temperature control
int 	updateTemperature(); 	// Refresh CPU temperature in gTemperature
int	sweepTemperatures();    // Step through temperatures from 30 to 60 C
int	sweepTempRanges();      // Step through the defined temperature ranges
int	showProperties();	// Display properties on oled display


//------------------------------------------------------------------------------
//
//  main program
//
//------------------------------------------------------------------------------
int main( int argc, char* argv[] )
{
    if ( init() != 0 )
    {
    	fprintf( stderr, "Init failed\n" );
    } else if ( argc == 1 )
    {
	// No arguments supplied: run normal control loop
	return runControlLoop();
    } 

    // Illegal arguments; should be either
    //     	-t sweepTemperatures, or
    //		-t sweepTempRanges    

    if ( (argc != 3) || strcmp( argv[1], "-t" )) 
    {
        fprintf( stderr, "Usage:\n" );
        fprintf( stderr, "\t tempcontrol, or\n" );
        fprintf( stderr, "\t tempcontrol -t sweepTempRanges, or\n" );
        fprintf( stderr, "\t tempcontrol -t sweepTemperatures\n" );
	return -1;
    }

    if ( !strcmp( argv[2], "sweepTemperatures" ))
    {
	fprintf( stderr, "sweepTemperatures\n" );
	return sweepTemperatures();
    }
    else if ( !strcmp( argv[2], "sweepTempRanges" ) )
    {
	fprintf( stderr, "sweepTempRanges\n" );
	return sweepTempRanges();
    }
    else 
    {
	fprintf( stderr, "unknown option %s\n", argv[2] );
	return -1;
    }
}


//------------------------------------------------------------------------------
//  int init();
//
//    Open files for I2C controls and cpu temperature reading.
//    Return 0 if both files could be opened succesfully, -1 otherwise
//------------------------------------------------------------------------------
int  init()
{
    int returnValue = 0;

    // Initialize I2C fan control
    wiringPiSetup();
    gFileI2C = wiringPiI2CSetup( 0x0d );
    if ( gFileI2C < 0 )
    {
        fprintf( stderr, "Could not init I2C\n" );
        returnValue = -1;
    }

    // Open CPU temperature file
    pFileTemperature = fopen( TEMP_PATH, "r");
    if ( pFileTemperature == NULL )
    {
        fprintf( stderr, "Could not open temperature file\n" );
        returnValue = -1;	
    }

    return returnValue;
}


//------------------------------------------------------------------------------
//  enum TempRange temperatureRange( double temperature );
//
//    This function returns the TempRange, the supplied temperature (degrees 
//    Celsius) lies in.
//------------------------------------------------------------------------------
enum TempRange temperatureRange( const double temperature )
{
    enum TempRange tempRange = TempAbove53;

    if ( temperature < 40 )
    {
	tempRange = TempBelow40;
    }            
    else if ( temperature < 45 )
    {
	tempRange = Temp40To45;
      }
    else if ( temperature < 47 )
    {
	tempRange = Temp45To47;
    }
    else if ( temperature < 49 )
    {
	tempRange = Temp47To49;
    }
    else if ( temperature < 51 )
    {
	tempRange = Temp49To51;
    }
    else if ( temperature < 53 )
    {
	tempRange = Temp51To53;
    }
    else
    {
	tempRange = TempAbove53;
    } 

    return tempRange;
}


//------------------------------------------------------------------------------
//  int setTempControls( TempRange  tempRange, bool verbose );
//
//    This function sets fan speed and led colors for the supplied temp-range.
//    TempBelow40: off
//    Temp40To45:  20%
//    Temp45To47:  40%
//    Temp47To49:  60%
//    Temp49To51:  80%
//    Temp51To53:  90%
//    TempAbove53: 100%
//
//  The LED color on the Smart Cooling Hat is changed accordingly.
//
//  Selected system properties are displayed on the OLED display on the Smart 
//  Cooling Hat.
//
//  Only if verbose is true, will the value of temp-range be printed on stdout.
//  The return value is 0, unless the I2C interface could not be initialized.
//------------------------------------------------------------------------------
int setTempControls( enum TempRange tempRange, bool verbose )
{
    int	 returnValue = 0;
    if ( gFileI2C == 0 )
    {
	gFileI2C = wiringPiI2CSetup( 0x0d );
    }

    switch ( tempRange )
    {
	case TempBelow40: 
	    // Switch off fan below 40 C
	    wiringPiI2CWriteReg8( gFileI2C, 0x08, 0x00 );
	    setRGB( gFileI2C, MAX_LED, 0x00, 0x88, 0x00 );    
	    break;

	case Temp40To45: 
	    // Below 45 C run at 20% fan speed
	    wiringPiI2CWriteReg8( gFileI2C, 0x08, 0x02 );
	    setRGB( gFileI2C, MAX_LED, 0x00, 0x44, 0x44 );    
	    break;
    
	case Temp45To47: 
	    // Below 47 C run at 40% fan speed
	    wiringPiI2CWriteReg8( gFileI2C, 0x08, 0x04 );
	    setRGB( gFileI2C, MAX_LED, 0x00, 0x00, 0x88 );    
	    break;
    
	case Temp47To49: 
	    // // Below 49 C run at 60% fan speed
	    wiringPiI2CWriteReg8( gFileI2C, 0x08, 0x06 );
	    setRGB( gFileI2C, MAX_LED, 0x44, 0x00, 0x44 );    
	    break;
    
	case Temp49To51: 
	    // Below 51 C run at 80% fan speed
	    wiringPiI2CWriteReg8( gFileI2C, 0x08, 0x08 );        
	    setRGB( gFileI2C, MAX_LED, 0x88, 0x00, 0x00 );        
	    break;
    
	case Temp51To53: 
	    // Below 53 C run at 90% fan speed
	    wiringPiI2CWriteReg8( gFileI2C, 0x08, 0x09 );
	    setRGB( gFileI2C, MAX_LED, 0xff, 0x00, 0x00 );    
	    break;
    
	case TempAbove53: 
	    // Above 53 degrees run fan at full speed
	    wiringPiI2CWriteReg8( gFileI2C, 0x08, 0x01 );
	    setRGB( gFileI2C, MAX_LED, 0xff, 0xff, 0xff );    
	    break;
    
	default:
	    returnValue = -1;
        }
    
    if (verbose) printf("Settings applied for TempRange: %i\n", tempRange);
    
    close( gFileI2C );
    gFileI2C = 0;
    return returnValue;
}


//------------------------------------------------------------------------------
//  int updateTemperature()
//	Read the temperature of the Raspberry Pi device in degrees Celsius.
//	If succesful, place the temperature in global variable gTemperature and
//	return 0. Otherwise, return -1.
//------------------------------------------------------------------------------
int updateTemperature()
{
    char 	buf[MAX_SIZE];	
    int		returnValue = 0;

    rewind ( pFileTemperature );
    fread ( &buf[ 0 ], 1, MAX_SIZE, pFileTemperature );

    // Convert to degrees celsius
    gTemperature = atoi(buf) / 1000.0;
    return returnValue;
}


//------------------------------------------------------------------------------
//  int sweepTemperatures()
//	Steps through temperatures from 30 to 65 C
//------------------------------------------------------------------------------
int sweepTemperatures()
{
    enum TempRange oldRange = TempAbove53;
    int i;

    for ( i =30 ; i < 65; i++ )
    {
	gTemperature = i;
	showProperties();

	enum TempRange tempRange =  temperatureRange( gTemperature );
 	fprintf( stderr, "Simulated temperature now: %.1f -- ", gTemperature );
 	fprintf( stderr, "in range: %i\n", tempRange );

	if ( tempRange != oldRange )
	{
	    // Set controls for new temperature range
	    setTempControls( tempRange, true );
	    oldRange = tempRange;
	}

	// Check again in one second
        delay( 1000 );
    }
}


//------------------------------------------------------------------------------
//  int sweepTempRanges()
//	Steps through the defined temperature ranges in descending order and then
//	in ascending order, one step per second.
//------------------------------------------------------------------------------
int sweepTempRanges()
{
    int   i;

    // Descending order
    for ( i = MAX_RANGE-1; i >= 0; i-- )
    {
	setTempControls( (enum TempRange)i, true );
        delay( 1000 );
    }

    // Ascending order
    for ( i = 0; i < MAX_RANGE; i++ )
    {
	setTempControls( (enum TempRange)i, true );
        delay( 1000 );
    }
    
    return 0;
}


//------------------------------------------------------------------------------
//  runControlLoop()
//	Run the loop where the temperature is read periodically and new cooling 
//	settings are applied when the temperature enters a new range.
//------------------------------------------------------------------------------
int runControlLoop()
{
    int returnValue = 0;
    enum TempRange oldRange = 	TempAbove53; // Max cooling by default

    while ( true )
    {
	returnValue = updateTemperature();
        showProperties();

	if ( !returnValue )
	{
	    enum TempRange tempRange = temperatureRange( gTemperature );

	    if ( tempRange != oldRange )
	    {
	        // Set controls for new temperature range
	        returnValue = setTempControls( tempRange, false );
	        oldRange = tempRange;
	    }
        }
 
	// check again in five seconds
	delay( 5000 );
    }
    
    return returnValue;
}


//------------------------------------------------------------------------------
//  void SetRGB( int num, int R, int G, int B)
//    Sets the R, G and B values of the addressed LEDs
//    If the maximum amount of leds is addressed, all leds are controlled,
//    otherwise a specific led van be set.
//------------------------------------------------------------------------------
void setRGB( int fd, int num, int R, int G, int B )
{
    if (num >= MAX_LED)
    {
        wiringPiI2CWriteReg8( fd, 0x00, 0xff );
        wiringPiI2CWriteReg8( fd, 0x01, R );
        wiringPiI2CWriteReg8( fd, 0x02, G );
        wiringPiI2CWriteReg8( fd, 0x03, B );
    }
    else if (num >= 0)
    {
        wiringPiI2CWriteReg8( fd, 0x00, num );
        wiringPiI2CWriteReg8( fd, 0x01, R );
        wiringPiI2CWriteReg8( fd, 0x02, G );
        wiringPiI2CWriteReg8( fd, 0x03, B );
    }
}


//------------------------------------------------------------------------------
//  void closeRGB( int fd )
//    close the I2C file used for LED control
//------------------------------------------------------------------------------
void closeRGB( int fd_i2c )
{
//    wiringPiI2CWriteReg8( fd_i2c, 0x07, 0x00 );
    close( fd_i2c );
    delay(100);
}


//------------------------------------------------------------------------------
//  int showProperties()
//	Retrieves system properties and displays them on the OLED display that
//	is mounted on the Smart Cooling Hat.
//	Returns 0 if all properties were displayed successfully; -1 otherwise
//------------------------------------------------------------------------------
int showProperties()
{
    struct sysinfo 	sysInfo;
    struct statfs 	diskInfo;
    struct ifaddrs*	pIfAddrStruct = NULL;
    void*               pTmpAddr = NULL;
    char 		cpuInfoTxt	[MAX_SIZE];
    char		cpuTempTxt	[MAX_SIZE];
    char 		ramInfoTxt	[MAX_SIZE];
    char		ipInfoTxt 	[MAX_SIZE];
    char		diskInfoTxt	[MAX_SIZE];
    char 		addressBuffer	[INET_ADDRSTRLEN];
    bool		bResult = true;
 
    // Initialize oled display:
    ssd1306_begin( SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS );
    ssd1306_clearDisplay();

    // Retrieve system info
    if( sysinfo( &sysInfo ) != 0 )
    {
	char *text = "sysinfo-Error";
	ssd1306_drawString(text);
	ssd1306_display();
        bResult = false;
    }

    // Fill cpuInfoTxt and cpuTempTxt buffers:
    unsigned long avgCpuLoad = sysInfo.loads[0] / 1000;
    sprintf( cpuInfoTxt, "CPU:%ld%%", avgCpuLoad);
    sprintf(cpuTempTxt, "Temp:%.1fC", gTemperature);	

    // Fill ramInfoTxt buffer:
    unsigned long totalRam = sysInfo.totalram >> 20;
    unsigned long freeRam =  sysInfo.freeram >> 20;
    sprintf(ramInfoTxt, "RAM:%ld/%ld MB", freeRam, totalRam);

    // Fill ipInfo buffer:
    // Look for interface with name "eth0" or "wlan0":
    getifaddrs( &pIfAddrStruct );

    while ( pIfAddrStruct != NULL )
    {
	if ( pIfAddrStruct->ifa_addr->sa_family == AF_INET ) 
	{ 
           // check it is IP4 is a valid IP4 Address
	    struct sockaddr_in* pSockAddress = 
		((struct sockaddr_in *)pIfAddrStruct->ifa_addr );            
	    pTmpAddr = &pSockAddress->sin_addr;

	    inet_ntop( AF_INET, pTmpAddr, addressBuffer, INET_ADDRSTRLEN );

	    if ( strcmp( pIfAddrStruct->ifa_name, "eth0" ) == 0 )
	    {
		sprintf( ipInfoTxt, "eth0:IP:%s", addressBuffer );
		break;
	    }
	    else if ( strcmp( pIfAddrStruct->ifa_name, "wlan0" ) == 0 )
	    {
		sprintf( ipInfoTxt, "wlan0:%s", addressBuffer );
		break;
	    }
	}
	pIfAddrStruct = pIfAddrStruct->ifa_next;
    }

    // Fill diskInfoTxt buffer:
    statfs("/", &diskInfo);
    unsigned long long totalBlocks = 	diskInfo.f_bsize;
    unsigned long long totalSize = 	totalBlocks * diskInfo.f_blocks;
    size_t 				mbTotalsize = totalSize >> 20;
    unsigned long long freeDisk = 	diskInfo.f_bfree * totalBlocks;
    size_t 				mbFreedisk = freeDisk >> 20;
    sprintf( diskInfoTxt, "Disk:%ld/%ldMB", mbFreedisk, mbTotalsize);

    // Write the buffers to the oled display:
    ssd1306_drawText(0,  0, cpuInfoTxt );
    ssd1306_drawText(56, 0, cpuTempTxt );
    ssd1306_drawText(0,  8, ramInfoTxt );
    ssd1306_drawText(0, 16, diskInfoTxt );
    ssd1306_drawText(0, 24, ipInfoTxt );
    ssd1306_display();
  
    return 0;    
}

