//  How to access GPIO registers from C-code on the Raspberry-Pi
//  Example program
//  15-January-2012
//  Dom and Gert
//
// INSTALL
// wget http://www.open.com.au/mikem/bcm2835/bcm2835-1.15.tar.gz
// tar xzf bcm2835-1.15.tar.gz
// cd bcm2835-1.15/
// ./configure
// make
// make install
// COMPILE
// gcc readDHT.c -lbcm2835 -lrt -o readDHT

// 24-October-2015
// some filtering experiments by alpet.

// Access from ARM Running Linux

#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <bcm2835.h>
#include <unistd.h>
#include <time.h>

#define MAXTIMINGS 100

#define DHT11 11
#define DHT22 22
#define AM2302 22
#define MAXSAMPLES 64

float temperature, humidity;
int max_timings =  MAXTIMINGS; 

int max_wait      = 100;
int dbg  	  = 0;
int dhtpin   	  = 7;
FILE *logfd     = NULL;

int readDHT(int type, int pin);

struct tm * systime() {
  time_t rawtime;        
  time (&rawtime);
  return localtime (&rawtime);
}


void log_msg(char *fmt, ...) {
  if (!logfd) return;
 
  char buff[256];
  va_list ap;  
  va_start(ap, fmt);
  vsnprintf(buff, 256, fmt, ap);  
  va_end(ap); 
  // fputs(logfd, buff);
  
  char s[32];  
  strftime(s, 32, "[%H:%M:%S]. ", systime());
  fprintf(logfd, "%s %s\n", s, buff); 
}

float filter_errors(float *values, int count, float median) {
  int i, good = 0;
  float sum = 0.f;
  float sq_sum = 0.f;
  
  if (!count) return median;
  
  float max_dev = 1.f + 25.f / (float)count;
  
  
  
  
  while (1) {
    int min_i = -1, max_i = -1;
    float min_v = 1e100, max_v = -1e100;
    
    for (i = 0; i < count; i ++)
    if (values[i] != 0) {
      float v = values[i];
      if (v < min_v) {
        min_i = i;
        min_v = v;
      }
      if (v > max_v) {
        max_i = i;
        max_v = v;
      }               
      
    }
    
    log_msg("min_v = %.1f, max_v = %.1f", min_v, max_v);
    
    
    
    // extremums probable have errors
    if ( max_v - min_v > 1.f && count > 5 ) {
         float rc = 0.f;
         if ( median - min_v > max_v - median) {
             if ( max_v - 1.f < min_v * 2 && max_v + 1.f > min_v * 2 )
                  rc = 2.0f;
             
             values[min_i] *= rc;            
             log_msg(" broken value %d = %.1f, restore coef = %.1f ", min_i, min_v, rc); 
         }
         else {    
             // trying restore
             if ( max_v - 1.f < min_v * 2 && max_v + 1.f > min_v * 2 ) 
                  rc = 0.5f; // bit shift recovery              
             
         
             values[max_i] *= rc;
             log_msg(" broken value %d = %.1f, restore coef = %.1f ", max_i, max_v, rc);
         }               
    }  
    else break;   
  
  }
  
  for (i = 0; i < count; i ++)
  if (values[i] != 0) {
     float diff = values[i] - median;
     diff = diff * diff; // square     
     sq_sum += diff;
     if (diff < max_dev) {
       sum += values[i]; // most not deviated value
       good ++;
     } // if
  } 
  
  
  
  log_msg("std_dev = %.2f, max_dev = %.2f, median = %.2f, good = %d/%d ", sq_sum / (float)count, max_dev, median, good, count);  
  
    if (good > 0)     
      return sum / (float)good;      
  
  return 0.f;
}




int main(int argc, char **argv) {
 //       return 1;
 char   args[1024];
 args[0] = 0;
 int i = 0;
 for (i = 1; i < argc; i++) { 
   strcat(args, " "); strcat(args, argv[i]);
 }
 dbg = (int) strstr(args, "debug");
 if (dbg)
     printf("argc: %d, args: %s \n", argc, args);
 // else  printf("argc: %d, args: %s, dbg: %d\n", argc, args, dbg);


 if (argc < 3) {
       printf("usage: %s [11|22|2302] GPIOpin#\n", argv[0]);
       printf("example: %s 2302 4 - Read from an AM2302 connected to GPIO #4\n", argv[0]);
       return 2;
 }
 int type = DHT11;
 if (strcmp(argv[1], "11") == 0) type = DHT11;
 if (strcmp(argv[1], "22") == 0) type = DHT22;
 if (strcmp(argv[1], "2302") == 0) type = AM2302;
 if (type == 0) {
       printf("Select 11, 22, 2303 as type!\n");
       return 3;
 }

 dhtpin = atoi(argv[2]);

 if (dhtpin <= 0) {
       printf("Please select a valid GPIO pin #\n");
       return 3;
 }

 char s[256], fn[260];
 
 strftime(s, 32,   "%y%m%d", systime());
 snprintf(fn, 260, "/var/log/data/dht_p%d_%s.log", dhtpin, s);
 logfd = fopen(fn, "a+");
  
 
 log_msg("-- readDHT %s ", args);


 printf("%d;", dhtpin);

 int delay = 300;

 if (argc >= 4) 
     delay = atoi (argv[3]);
 if (argc >= 5)
     max_wait = atoi(argv[4]);

 float temp = 0.f;
 float hum  = 0.f; 
 int   samples = 0;


 time_t start, curr;
 time(&start);
 
 float h_values[MAXSAMPLES];
 float t_values[MAXSAMPLES]; 
 

 while (samples < MAXSAMPLES)
 {
    time(&curr);
    if (curr - start > 12) break; // timeout reached

    if (bcm2835_init()) {
      temperature = 0.f;
      humidity = 0.f;
      if ( readDHT(type, dhtpin) )  {
        // printf(" read complete, i = %d, t = %.1f, h = %.1f \n", i, temperature, humidity);
        h_values[samples] = humidity;
        t_values[samples] = temperature;        
        
        temp += temperature;
        hum  += humidity;
        samples ++;
        log_msg("(%02d) T =  %.1f *C, RH = %.1f%%", samples, temperature, humidity);
        usleep(delay * 1000);
      } /// if
      bcm2835_close();
      
    } // if   
    usleep(1000);
    // else printf(" init failed, i = %d \n", i);	
    i ++;
 } // while


 


 if (samples > 1) // only effective checks
 {
   temp /= (float)samples;
   hum  /= (float)samples;
   
   // h_values[0] = 5.f;
   
   temp = filter_errors(t_values, samples, temp);      
   hum  = filter_errors(h_values, samples, hum);
   
   
   log_msg("t=%.1f, h=%.1f", temp, hum);
   printf("%.1f;%.1f;%d\n", temp, hum, samples);   
   if (logfd) fclose(logfd);
   return 0;

 }
 else
 {
  printf("0;0;0\n"); // read failed 
  if (logfd) fclose(logfd);
  return 5;
 }
} // main

long int time_ns() {
 struct timespec gettime_now;
 clock_gettime(CLOCK_REALTIME, &gettime_now);
 return gettime_now.tv_nsec;		//Get nS value
}

long int time_diff_ns(long int start_time) {
  long int time_difference;
  time_difference = time_ns() - start_time;
  if (time_difference < 0)
      time_difference += 1000000000;				//(Rolls over every 1 second)
  return time_difference;
}

void micro_sleep (int delay_us){
  long int start_time = time_ns();
  while (1)
  {
      if (time_diff_ns(start_time) > (delay_us * 1000))		//Delay for # nS
	  break;
  }
}

int reads, bits[250], data[100];

int read_bit() { 
    reads ++;
    return bcm2835_gpio_lev(dhtpin);
}

int readDHT(int type, int pin) {
 int bitidx = 0;
 int laststate = HIGH;
 int j=0;
 int i=0;
 int h_times[64];
 int l_times[64];
 
 long int bit_start = time_ns();

 // Set GPIO pin to output
 // bcm2835_gpio_write(pin, HIGH);
 // request tryings
 for (i = 0; i < 1; i ++) {
   bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_OUTP);
   bcm2835_gpio_write(pin, HIGH);
   micro_sleep(10);
   bcm2835_gpio_write(pin, LOW);
   
   if (type == DHT11)
       usleep(18500);     // ms wait by datasheet.
       
   if (type == DHT22)   
       micro_sleep(520);  // 500us wait with zero-lvl
   bcm2835_gpio_write(pin, HIGH);
   micro_sleep(5);    
   bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_INPT);
   // 20-40qs wait with high-lvl   
   break; // wait for zero
 } 

 data[0] = data[1] = data[2] = data[3] = data[4] = 0;
 // read data!
 // while (bcm2835_gpio_lev(pin) > 0) nanosleep(1); // wait for reset
 int curstate = read_bit();

 // if (curstate > 0) return 0; // no response
 // if (dbg) printf(" after init: %d, init time = %d \n", curstate, time_diff_ns(bit_start)/1000);
 
 laststate = curstate;
 //micro_sleep(max_wait);
 micro_sleep(max_wait);
 
 char s[32];
 s[8] = 0;
 
 reads = 0;
 int started = 0;
 int switches = 0; 

 for (i=0; i < MAXTIMINGS; i++) {
    long int low_t  = 0;
    long int high_t = 0;
    bit_start = time_ns(); 
    // measuring time for low level
    while ( curstate == 0 ) {
       curstate = read_bit();
       low_t = time_diff_ns(bit_start)/1000; // ns to us
       if (low_t >= 100) break;
       if (low_t < 40) micro_sleep(1);
    }
    // edge down
    if (laststate != curstate) switches++;
    
    bit_start = time_ns();
    laststate = curstate;    
    
    // measuring time for high level 
    while ( curstate == 1 ) {
       curstate = read_bit();
       high_t = time_diff_ns(bit_start)/1000; // ns to us
       if (high_t >= 100) break;
       if (high_t < 15) micro_sleep(1);
    }
    
    if (laststate != curstate) switches++;
    laststate = curstate;
    // if (started && j <= 40 && dbg) printf("%02d:%02d ", low_t, high_t);


    if ( low_t >= 100 || high_t >= 100 )
    {
       if(dbg && i > 16 && i < 40 ) printf("\n waiting for bit %d/%d breaked due timout, curstate = %d, switches = %d, reads = %d \n", i, j, curstate, switches, reads);
       break;
    } 

    // first bit for DHT11 ~= 20us???

    if (started && low_t >= 15 && high_t > 15 && low_t < 70 && high_t < 87) {
      // shove each bit into the storage bytes
      if (dbg) { 
	      s[j & 7] = high_t > 50 ? '1' : '0';
        //if (j == 7) puts("\n");     
	      //if ((j & 7) == 7) printf("\t %d:[%s]", j/8, s);             
      }
      data[j/8] <<= 1;

      if (high_t > 58)
          data[j/8] |= 1;

      j++;
      
      if (j/8 > 4) break;
    }
    
    if (low_t > 70 && high_t > 70) {
      // reset due prepare sequence
      started = 1;
      j = 0;
      bitidx = 0; //
    }
    
    l_times[bitidx  ] = low_t;
    h_times[bitidx++] = high_t;        
 }
 // if(dbg) puts("\n");

#ifdef DEBUG
 for (int i=3; i<bitidx; i+=2) {
    printf("bit %d: %d\n", i-3, bits[i]);
    printf("bit %d: %d (%d)\n", i-2, bits[i+1], bits[i+1] > 15);
 }
#endif

 int crc = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
 
 if (dbg && j >= 16)  {
    printf("\n Data (%d): %02x %02x %02x %02x %02x, CRC = %02x\n\t", j, data[0], data[1], data[2], data[3], data[4], crc);
    for (i = 0; i < bitidx; i++)
      printf("%02d:%02d %s", l_times[i], h_times[i], (i & 7) ? "" : "\n\t");
    puts("\n");   
    
}

 if ((j >= 39) && (crc == data[4])) {
    // yay!
    // if (type == DHT11) printf("%d;%d", data[2], data[0]);
    if (type == DHT11)
    {
       temperature = (float)data[2] + (float)data[3] * 0.1f; 
       humidity    = (float)data[0] + (float)data[1] * 0.1f;
       return 1;
    }


    if (type == DHT22) {
       float f, h;
       h = data[0] * 256 + data[1]; // easy 16 bit?
       h /= 10;

       f = (data[2] & 0x7F)* 256 + data[3];
       f /= 10.0;
       if (data[2] & 0x80)  f *= -1;

       if ( h > 2.f && h < 100.f && f > -20.f && f < 150.f )
       {
          temperature = f;
          humidity = h;
 	        return 1;
       }
    }
 }

 return 0;
}
