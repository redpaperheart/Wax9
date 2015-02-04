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

#include "Wax9.h"
#include "cinder/Serial.h"

/* -------------------------------------------------------------------------------------------------- */
#pragma mark constructors and setup
/* -------------------------------------------------------------------------------------------------- */

Wax9::Wax9()
{
    // state
    bEnabled = true;
    bConnected = false;
    bDebug = false;
    bSmooth  = false;
    mSmoothFactor = 0.8;
    mNewReadings = 0;
    
    // data
    mMaxAccel = vec3(0.0f);
    mMaxAccelMag = 0;
    mHistoryLength = 120;
    
    // device settings
    bAccOn = true;
    bGyrOn = true;
    bMagOn = true;
    mOutputRate = 10;
    mAccRate = 200;
    mGyrRate = 200;
    mMagRate = 80;
    mAccRange = 8;
    mGyrRange = 2000;
    mDataMode = 1;
}

Wax9::~Wax9()
{
    stop();
}

bool Wax9::setup(string portName, int historyLength)
{
    bConnected = false;
    mHistoryLength = historyLength;
    mSamples = new SampleBuffer(mHistoryLength);
//    mAccelMags  = new boost::circular_buffer<float>(mHistoryLength);
    
    app::console() << "Available serial ports: " << std::endl;
    for( auto device : Serial::getDevices()) app::console() << device.getName() << ", " << device.getPath() << std::endl;
    
    try {
        Serial::Device device = Serial::findDeviceByNameContains(portName);
        mSerial = Serial(device, 115200);
        app::console() << "Receiver sucessfully connected to " << portName << std::endl;
    }
    catch(SerialExc e) {
        app::console() << "Receiver unable to connect to " << portName << ": " << e.what() << std::endl;
        bConnected = false;
        return false;
    }
    
    bConnected = true;
    return true;
}

bool Wax9::start()
{
    if (bConnected) {

        // construct settings string - we're not using range, just leaving defaults
        std::string settings = "\r\n";
        settings += "RATE X 1 " + toString(mOutputRate) + "\r\n";                       // output rate in Hz (table 7 in dev guide)
        settings += "RATE A " + toString(bAccOn) + " " + toString(mAccRate) + "\r\n";   // accel rate in Hz (table 7)
        settings += "RATE G " + toString(bGyrOn) + " " + toString(mGyrRate) + "\r\n";   // gyro rate in Hz (table 7)
        settings += "RATE M " + toString(bMagOn) + " " + toString(mMagRate) + "\r\n";   // magnetometer rate Hz (table 7)
        settings += "DATAMODE " + toString(mDataMode) + "\r\n";                         // binary data mode (table 10)
        
        app::console() << settings;
        
        // send wait for reply from device
        mSerial.writeString(settings);
        app::console() << mSerial.readStringUntil('\n') << std::endl;
        
        // start streaming
        std::string init = "\r\nSTREAM\r\n";       // start streaming
        mSerial.writeString(init);
        
        return true;
    }
    return false;
}

/* Close input and thread */
bool Wax9::stop()
{
    // send termination string (this disconnects the device)
    if (bConnected) {
        mSerial.writeString("\\r\nRESET\r\n");
        app::console() << "Resetting and disconnecting WAX9" << std::endl;
    }
    bConnected = false;
    bEnabled = false;

    return true;
}

/* -------------------------------------------------------------------------------------------------- */
#pragma mark public interface
/* -------------------------------------------------------------------------------------------------- */

int Wax9::update()
{
    if (bConnected) {
        return readPackets(mBuffer);
    }
    return 0;
}

//vec3 Wax9::getNextReading()
//{
//    WaxSample sample;
//    mBuffers.at(id)->popBack(&sample);
//    return vec3(sample.x/256.0f, sample.y/256.0f, sample.z/256.0f);
//}

bool Wax9::hasNewReadings()
{
    return mSamples->size() > 0;
}

float Wax9::getPitch()
{
    // using accelerometer
    if (mSamples->size() > 0) {
        vec3 acc = mSamples->front().acc;
        return atan2(acc.x, sqrt(acc.y*acc.y + acc.z*acc.z));
    }
    
    // using accelerometer + gyro
    return 0.0;
}

float Wax9::getRoll()
{
    // using accelerometer
    if (mSamples->size() > 0) {
        vec3 acc = mSamples->front().acc;
        return atan2(-acc.y, acc.z);
    }
    
    // using accelerometer + gyro
    return 0.0;
}

/* -------------------------------------------------------------------------------------------------- */
#pragma mark input thread
/* -------------------------------------------------------------------------------------------------- */

int Wax9::readPackets(char* buffer)
{
    while(mSerial.getNumBytesAvailable() > 0)
    {
        // Read data
        size_t bytesRead = lineread(buffer, BUFFER_SIZE);
        
        if (bytesRead == (size_t) - 1)
        {
            bytesRead = slipread(buffer, BUFFER_SIZE);
        }
        if (bytesRead == 0) { return -1; }
        
        
        // Get time now
        unsigned long long now = ticksNow();
        
        // If it appears to be a binary WAX9 packet...
        if (bytesRead > 1 && buffer[0] == '9')
        {
            Wax9Packet *wax9Packet = parseWax9Packet(buffer, bytesRead, now);
            
            if (wax9Packet != NULL)
            {
                if(bDebug) printWax9(wax9Packet);
                
                // process packet and save it
                mSamples->push_front(processPacket(wax9Packet));  //todo: check if we should store packet pointers in the buffer
            }
        }
        return 1;
    }
    return 0;
}

Wax9Sample Wax9::processPacket(Wax9Packet *p)
{
    Wax9Sample s;
    s.timestamp = p->timestamp;
    s.sampleNumber = p->sampleNumber;
    s.acc = vec3(p->accel.x, p->accel.y, p->accel.z) / 4096.0f;     // table 19
    s.gyr = vec3(p->gyro.x, p->gyro.y, p->gyro.z) * 0.07f;          // table 20
    s.mag = vec3(p->mag.x, p->mag.y, -p->mag.z) * 0.1f;
    return s;
}


/* -------------------------------------------------------------------------------------------------- */
#pragma mark packet parsing
/* -------------------------------------------------------------------------------------------------- */

#define SLIP_END     0xC0                   /* End of packet indicator */
#define SLIP_ESC     0xDB                   /* Escape character, next character will be a substitution */
#define SLIP_ESC_END 0xDC                   /* Escaped substitution for the END data byte */
#define SLIP_ESC_ESC 0xDD                   /* Escaped substitution for the ESC data byte */

/* Read a line from the device */
size_t Wax9::lineread(void *inBuffer, size_t len)
{
    unsigned char *p = (unsigned char *)inBuffer;
    unsigned char c;
    size_t bytesRead = 0;
    
    if (inBuffer == NULL) { return 0; }
    *p = '\0';
    
//    while(!bCloseThread)
    while(bEnabled)
    {
        c = '\0';
        
        try{
            c = mSerial.readByte();
        }
        catch(...) {
            return bytesRead;
        }
        
        if (c == SLIP_END) { // A SLIP_END means the reader should switch to slip reading.
            return (size_t)-1;
        }
        if (c == '\r' || c == '\n')
        {
            if (bytesRead) return bytesRead;
        }
        else
        {
            if (bytesRead < len - 1) {
                p[bytesRead++] = (char)c;
                p[bytesRead] = 0;
            }
        }
    }
    return 0;
}

/* Read a SLIP-encoded packet from the device */
size_t Wax9::slipread(void *inBuffer, size_t len)
{
    unsigned char *p = (unsigned char *)inBuffer;
    unsigned char c = '\0';
    size_t bytesRead = 0;
    
    if (inBuffer == NULL) return 0;
    
    //    while(!bCloseThread)
    while(bEnabled)    //not sure if this is going to give problems without threaded
    {
        c = '\0';
        
        try{
            c = mSerial.readByte();
        }
        catch(...) {
            return bytesRead;
        }
        switch (c)
        {
            case SLIP_END:
                if (bytesRead) return bytesRead;
                break;
                
            case SLIP_ESC:
                c = '\0';
                
                try{
                    c = mSerial.readByte();
                }
                catch(...) {
                    return bytesRead;
                }
                
                switch (c){
                    case SLIP_ESC_END:
                        c = SLIP_END;
                        break;
                    case SLIP_ESC_ESC:
                        c = SLIP_ESC;
                        break;
                    default:
                        fprintf(stderr, "<Unexpected escaped value: %02x>", c);
                        break;
                }
                
                /* ... fall through to default case with our replaced character ... */
            default:
                if (bytesRead < len) {
                    p[bytesRead++] = c;
                }
                break;
        }
    }
    return 0;
}

Wax9Packet* Wax9::parseWax9Packet(const void *inputBuffer, size_t len, unsigned long long now)
{
    const unsigned char *buffer = (const unsigned char *)inputBuffer;
    static Wax9Packet wax9Packet;
    
    if (buffer == NULL || len <= 0) { return 0; }
    
    if (buffer[0] != '9')
    {
        fprintf(stderr, "WARNING: Unrecognized packet -- ignoring.\n");
    }
    else if (len >= 20)
    {
        wax9Packet.packetType = buffer[0];
        wax9Packet.packetVersion = buffer[1];
        wax9Packet.sampleNumber = buffer[2] | ((unsigned short)buffer[3] << 8);
        wax9Packet.timestamp = buffer[4] | ((unsigned int)buffer[5] << 8) | ((unsigned int)buffer[6] << 16) | ((unsigned int)buffer[7] << 24);
        
        wax9Packet.accel.x = (short)((unsigned short)(buffer[ 8] | (((unsigned short)buffer[ 9]) << 8)));
        wax9Packet.accel.y = (short)((unsigned short)(buffer[10] | (((unsigned short)buffer[11]) << 8)));
        wax9Packet.accel.z = (short)((unsigned short)(buffer[12] | (((unsigned short)buffer[13]) << 8)));
        
        if (len >= 20)
        {
            wax9Packet.gyro.x  = (short)((unsigned short)(buffer[14] | (((unsigned short)buffer[15]) << 8)));
            wax9Packet.gyro.y  = (short)((unsigned short)(buffer[16] | (((unsigned short)buffer[17]) << 8)));
            wax9Packet.gyro.z  = (short)((unsigned short)(buffer[18] | (((unsigned short)buffer[19]) << 8)));
        }
        else
        {
            wax9Packet.gyro.x   = 0;
            wax9Packet.gyro.y   = 0;
            wax9Packet.gyro.z   = 0;
        }
        
        if (len >= 26)
        {
            wax9Packet.mag.x   = (short)((unsigned short)(buffer[20] | (((unsigned short)buffer[21]) << 8)));
            wax9Packet.mag.y   = (short)((unsigned short)(buffer[22] | (((unsigned short)buffer[23]) << 8)));
            wax9Packet.mag.z   = (short)((unsigned short)(buffer[24] | (((unsigned short)buffer[25]) << 8)));
        }
        else
        {
            wax9Packet.mag.x   = 0;
            wax9Packet.mag.y   = 0;
            wax9Packet.mag.z   = 0;
        }
        
        if (len >= 28)
        {
            wax9Packet.battery = (unsigned short)(buffer[26] | (((unsigned short)buffer[27]) << 8));
        }
        else
        {
            wax9Packet.battery = 0xffff;
        }
        
        if (len >= 30)
        {
            wax9Packet.temperature = (short)((unsigned short)(buffer[28] | (((unsigned short)buffer[29]) << 8)));
        }
        else
        {
            wax9Packet.temperature = 0xffff;
        }
        
        if (len >= 34)
        {
            wax9Packet.pressure = buffer[30] | ((unsigned int)buffer[31] << 8) | ((unsigned int)buffer[32] << 16) | ((unsigned int)buffer[33] << 24);
        }
        else
        {
            wax9Packet.pressure = 0xfffffffful;
        }
        
        return &wax9Packet;
    }
    else
    {
        fprintf(stderr, "WARNING: Unrecognized WAX9 packet -- ignoring.\n");
    }
    return NULL;
}

/* -------------------------------------------------------------------------------------------------- */
#pragma mark utils
/* -------------------------------------------------------------------------------------------------- */

void Wax9::printWax9(Wax9Packet *wax9Packet)
{
    printf( "\nWAX9\ntimestring:\t%s\ntimestamp:\t%f\npacket num:\t%u\naccel\t[%f %f %f]\ngyro\t[%f %f %f]\nmagnet\t[%f %f %f]\n",
            timestamp(wax9Packet->timestamp),
            wax9Packet->timestamp / 65536.0,
            wax9Packet->sampleNumber,
            wax9Packet->accel.x / 4096.0f, wax9Packet->accel.y / 4096.0f, wax9Packet->accel.z / 4096.0f,	// 'G' (9.81 m/s/s)
            wax9Packet->gyro.x * 0.07f,    wax9Packet->gyro.y * 0.07f,    wax9Packet->gyro.z * 0.07f,		// degrees/sec
            wax9Packet->mag.x * 0.10f, wax9Packet->mag.y * 0.10f, wax9Packet->mag.z * 0.10f * -1			// uT (magnetic field ranges between 25-65 uT)
            );
}

/* Returns the number of milliseconds since the epoch */
unsigned long long Wax9::ticksNow(void)
{
    struct timeb tp;
    ftime(&tp);
    return (unsigned long long)tp.time * 1000 + tp.millitm;
}

/* Returns a date/time string for the specific number of milliseconds since the epoch */
const char* Wax9::timestamp(unsigned long long ticks)
{
    static char output[] = "YYYY-MM-DD HH:MM:SS.fff";
    output[0] = '\0';
    
    struct tm *today;
    struct timeb tp = {0};
    tp.time = (time_t)(ticks / 1000);
    tp.millitm = (unsigned short)(ticks % 1000);
    tzset();
    today = localtime(&(tp.time));
    if (strlen(output) != 0) { strcat(output, ","); }
    sprintf(output + strlen(output), "%04d-%02d-%02d %02d:%02d:%02d.%03d", 1900 + today->tm_year, today->tm_mon + 1, today->tm_mday, today->tm_hour, today->tm_min, today->tm_sec, tp.millitm);
    
    return output;
}

//#define ACCELEROMETER_SENSITIVITY 8192.0
//#define GYROSCOPE_SENSITIVITY 65.536
//
//#define M_PI 3.14159265359
//
//#define dt 0.01							// 10 ms sample rate!
//
//void ComplementaryFilter(short accData[3], short gyrData[3], float *pitch, float *roll)
//{
//    float pitchAcc, rollAcc;
//    
//    // Integrate the gyroscope data -> int(angularSpeed) = angle
//    *pitch += ((float)gyrData[0] / GYROSCOPE_SENSITIVITY) * dt; // Angle around the X-axis
//    *roll -= ((float)gyrData[1] / GYROSCOPE_SENSITIVITY) * dt;    // Angle around the Y-axis
//    
//    // Compensate for drift with accelerometer data if !bullshit
//    // Sensitivity = -2 to 2 G at 16Bit -> 2G = 32768 && 0.5G = 8192
//    int forceMagnitudeApprox = abs(accData[0]) + abs(accData[1]) + abs(accData[2]);
//    if (forceMagnitudeApprox > 8192 && forceMagnitudeApprox < 32768)
//    {
//        // Turning around the X axis results in a vector on the Y-axis
//        pitchAcc = atan2f((float)accData[1], (float)accData[2]) * 180 / M_PI;
//        *pitch = *pitch * 0.98 + pitchAcc * 0.02;
//        
//        // Turning around the Y axis results in a vector on the X-axis
//        rollAcc = atan2f((float)accData[0], (float)accData[2]) * 180 / M_PI;
//        *roll = *roll * 0.98 + rollAcc * 0.02;
//    }
//}