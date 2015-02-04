/*
 Wax9
 Class to obtain data from the Wax3 device via serial port.
 
 Code based on the waxrec application:
 https://code.google.com/p/openmovement/source/browse/trunk/Software/WAX3/waxrec
 
 For more information read developer guide:
 http://openmovement.googlecode.com/svn/trunk/Software/WAX3/WAX%20Developer%20Guide.pdf
 */

/*
 Created by Adrià Navarro at Red Paper Heart
 
 Copyright (c) 2015, Red Paper Heart
 All rights reserved.
 
 This code is designed for use with the Cinder C++ library, http://libcinder.org
 
 To contact Red Paper Heart, email hello@redpaperheart.com or tweet @redpaperhearts
 
 Redistribution and use in source and binary forms, with or without modification, are permitted provided that
 the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice, this list of conditions and
 the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
 the following disclaimer in the documentation and/or other materials provided with the distribution.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * TO DO
 * separate IMU interface from data acquisition.
 * join with WAX3 block
 *
 */

#pragma once

#include "cinder/app/App.h"
#include "cinder/Thread.h"
//#include "cinder/ConcurrentCircularBuffer.h"
#include "cinder/Serial.h"
#include "cinder/Utilities.h"

#include <boost/circular_buffer.hpp>
#include <sys/timeb.h>

// Wax Structures
#define BUFFER_SIZE 0xffff
#define MAX_SAMPLES 32

using namespace std;
using namespace ci;

// 9-axis packet type (always little-endian, transmitted SLIP-encoded)
typedef struct
{
    // Standard part (26-bytes)
    char packetType;                        // @ 0 ASCII '9' for 9-axis
    char packetVersion;                     // @ 1 Version (0x01 = standard, 0x02 = extended)
    unsigned short sampleNumber;            // @ 2 Sample number (reset on configuration change, inactivity, or wrap-around)
    uint32_t timestamp;                     // @ 4 Timestamp (16.16 fixed-point representation, seconds)
    struct { signed short x, y, z; } accel; // @ 8 Accelerometer
    struct { signed short x, y, z; } gyro;  // @14 Gyroscope
    struct { signed short x, y, z; } mag;   // @20 Magnetometer
    
    // Extended part
    unsigned short battery;                 // @26 Battery (mV)
    short temperature;                      // @28 Temperature (0.1 degrees C)
    uint32_t pressure;                      // @30 Pressure (Pascal)
    //unsigned short deviceId;              // @34 Device identifier
    //                                      // @36
} Wax9Packet;

// Processed Wax9 packet
typedef struct
{
    unsigned short sampleNumber;
    uint32_t timestamp;
    vec3 acc;
    vec3 gyr;
    vec3 mag;
} Wax9Sample;

typedef  boost::circular_buffer<Wax9Sample> SampleBuffer;

class Wax9 {
public:
    Wax9();
    ~Wax9();
    
    bool        setup(string portName, int historyLength = 120);
    bool        start();
    bool        stop();
    int         update();  // only call if we don't want it threaded
    
    void        setDebug(bool b)                    { bDebug = b; }
    void        setSmooth(bool s, float f = 0.5f)   { bSmooth = s; mSmoothFactor = f; }
    
    bool        isConnected()                       { return bConnected; }
    bool        isEnabled()                         { return bEnabled; }
    bool        hasNewReadings();                   

//    vec3        getNextReading();
    
    int         getNumNewReadings()                 { return mNewReadings; }
    ushort      getId()                             { return mId; }
    
//    vec3        getAccel()                          {return mAccels->front();}
//    vec3        getAccel(int i)                     {return mAccels->at(i);}
//    vec3*       getAccelHistory()                   {return mAccels->linearize();}
//    float       getAccelMagnitude()                 {return mAccelMags->front();}
//    float       getAccelMagnitude(int i)            {return mAccelMags->at(i);}
//    float*      getAccelMagHistory()                {return mAccelMags->linearize();}
//    CircBuffer  getAccelMagHistoryBuffer()          {return mAccelMags;}
//    
//    vec3        getMaxAccel()                       {return mMaxAccel;}
//    float       getMaxAccelMagnitude()              {return mMaxAccelMag;}
    int         getHistoryLength()                  {return mSamples->size();}
    
    Wax9Sample   getReading()                       { return mSamples->front(); }
    Wax9Sample   getReading(int i)                  { return mSamples->at(i); }
    SampleBuffer* getReadings()                     { return mSamples; }
    
    float       getPitch(); // rotation in x axis in degrees
    float       getRoll(); // rotation in z axis in degrees
    
    
private:
    
    // packet parsing
    int                 readPackets(char *inBuffer);
    size_t              slipread(void *inBuffer, size_t len);
    size_t              lineread(void *inBuffer, size_t len);
    Wax9Packet*         parseWax9Packet(const void *inputBuffer, size_t len, unsigned long long now);
    Wax9Sample          processPacket(Wax9Packet *packet);
    
    // utils
    void                printWax9(Wax9Packet *waxPacket);
    const char*         timestamp(unsigned long long ticks);
    unsigned long long  ticksNow();
    
    // state
    bool                bConnected;
    bool                bDebug;
    bool                bEnabled;
    bool                bSmooth;
    int                 mId;
    int                 mNewReadings;
    int                 mHistoryLength;
    float               mMaxAccelMag;
    float               mSmoothFactor;
    
    // device settings - to construct init string
    bool                bAccOn;
    bool                bGyrOn;
    bool                bMagOn;
    int                 mOutputRate;
    int                 mAccRate;
    int                 mGyrRate;
    int                 mMagRate;
    int                 mAccRange;
    int                 mGyrRange;
    int                 mDataMode;
    
    // data
    char                mBuffer[BUFFER_SIZE];   // send back to class
    vec3                mMaxAccel;
    Serial              mSerial;
    SampleBuffer*       mSamples;
    
//    boost::circular_buffer<float>* mAccelMags;
//    boost::circular_buffer<vec3>* mAccels;
    

    
//    AccelDataSource* mDataSource;
//    map<ushort, ConcurrentCircularBuffer<WaxSample>* > mBuffers;
};
