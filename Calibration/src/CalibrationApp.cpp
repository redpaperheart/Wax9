#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/CameraUi.h"
#include "cinder/params/Params.h"
#include "cinder/Serial.h"
#include "cinder/Log.h"

#include "Wax9.h"

// To test this sample place the sensor
// with the arrow pointing up and looking at you
// and hit space to calibrate

using namespace ci;
using namespace ci::app;
using namespace std;

class CalibrationApp : public App {
public:
    void setup();
    void update();
    void draw();
    void keyDown(KeyEvent event);
    
    void reset();
    void calibrate();
    void drawOrientation();
    void drawCalibration();
    
    quat mStartRotationAHRS;
    CameraPersp mCam;
    CameraUi mCamUi;
    
    Wax9 mWax9;
    int mSerialName;
    vector<string> mSerialNames;
    params::InterfaceGlRef mParams;
    
    typedef enum {
        CALIBRATION = 0,
        ORIENTATION = 1
    } Mode;
    
    int mMode = 0;
    
    // calibration
    vector<vec3> mMag;
    AxisAlignedBox mBox;
};

void CalibrationApp::setup()
{
    // Let's define the starting position of the sensor.
    // The zero rotation is the sensor flat, with the serial number up
    // so you can read it. On the other side, the arrow should point to the left.
    //
    // We want to define a zero rotation different than that. So look at the
    // sensor coordinate system from the readme and write down the rotations
    // you need in order to get to the starting rotation you want.
    // In this case the starting position will be the one with the arrow pointing up
    // and looking at us. So:
    
    mat4 startRotMat;
    startRotMat *= glm::rotate(toRadians(-90.0f), vec3(1, 0, 0));  // x
    startRotMat *= glm::rotate(toRadians(-90.0f), vec3(0, 1, 0));  // y
    startRotMat *= glm::rotate(0.0f, vec3(0, 0, 1));               // z
    
    mStartRotationAHRS = quat(startRotMat);  // save it in a quaternion
    

    // setup camera
    mCam.setPerspective(45.0f, getWindowAspectRatio(), 0.1f, 1000.0f);
    mCam.lookAt(vec3(0, 0, 100), vec3(0));
    mCamUi = CameraUi(&mCam, app::getWindow(), -1);
    
    // Setup params
    auto devices = Serial::getDevices();
    for (auto device : devices) {
        mSerialNames.push_back(device.getName());
    }
    mParams = params::InterfaceGl::create("Wax9 Mag Calibration", ivec2(300, 300));
    mParams->setOptions( "", "valueswidth=175");
    mParams->addParam("Device", mSerialNames, &mSerialName);
    mParams->addButton("Connect", [this] {
       try {
           mWax9.setup(mSerialNames[mSerialName]);
           mWax9.setDebug(false );
           mWax9.start();
           mWax9.resetOrientation(mStartRotationAHRS);
       }
       catch (Exception e) {
       }
    });
    
    
    mParams->addSeparator();
    mParams->addButton("Reset", std::bind(&CalibrationApp::reset, this));
    mParams->addButton("Calibrate", std::bind(&CalibrationApp::calibrate, this));
    
    mParams->addSeparator();
    vector<string> modes = {"Calibration", "Orientation"};
    mParams->addParam("Display", modes, &mMode);
    mParams->addButton("Reset Orientation", [this] {
        mWax9.resetOrientation();
    });
    
}

void CalibrationApp::update()
{
    mWax9.update();
    
    if (mWax9.isConnected()) {
        if (mMode == Mode::CALIBRATION) {
            int newReadings = mWax9.getNumNewReadings();
            for (int i = 0; i < newReadings; i++) {
                vec3 mag = mWax9.getReading(i).mag;
                
                if (mMag.empty())   mBox.set(mag, mag);
                else                mBox.include(mag);
                mMag.push_back(mag);
            }
            mWax9.markAsRead();
        }
    }
}

void CalibrationApp::draw()
{
    gl::clear(Color::gray(0.1));
    
    if (mWax9.isConnected()) {
        if (mMode == Mode::ORIENTATION) {
            drawOrientation();
        }
        else {
            drawCalibration();
        }
    }
    else {
        gl::drawStringCentered("Wax9 not found. Check Bluetooth pairing and port name", getWindowCenter());
    }
    gl::drawString(to_string((int)getAverageFps()), vec2(20, 20));
    mParams->draw();
}

void CalibrationApp::drawOrientation()
{
    if (mWax9.isConnected() && mWax9.hasReadings()) {
        
        quat sensorRotOGL = mWax9.getOrientation();
        
        gl::ScopedDepth depth(true);
        gl::ScopedMatrices cameraMatrices;
        gl::setMatrices(mCam);
        gl::rotate(sensorRotOGL);
        
        // draw sensor cube
        gl::drawColorCube(vec3(0.0f), vec3(30, 5, 15));
        gl::drawCoordinateFrame(25.0, 2.0, 1.0);
        
        // draw text and arrow
        gl::rotate( M_PI / 2.0f, 1.0f, 0.0f, 0.0f); //M_PI_2
        gl::scale(vec3(0.25, -0.25, 1.0));
        gl::translate(vec3(0, -8, 2.52));
        gl::drawStringCentered("◀︎Axivity", vec2(0, 0), Color::white(), Font("Arial", 24));
    }
}

void CalibrationApp::drawCalibration()
{
    if (!mWax9.isConnected()) return;
    
    gl::ScopedDepth depth(true);
    gl::ScopedMatrices cameraMatrices;
    gl::setMatrices(mCam);
    gl::drawCoordinateFrame(25.0, 2.0, 1.0);
    {
        gl::ScopedMatrices mat;
        gl::scale(vec3(0.75));
        gl::translate(mWax9.getMagOffset());
        
        gl::begin(GL_POINTS);
        for (vec3 &p : mMag) {
            gl::vertex(p);
        }
        gl::end();
        
        gl::ScopedColor yellow(1, 1, 0);
        gl::drawStrokedCube(mBox);
    }
}

void CalibrationApp::keyDown(KeyEvent event)
{
    if (event.getChar() == ' ') {
        mWax9.resetOrientation(mStartRotationAHRS);
    }
}

void CalibrationApp::reset()
{
    mBox.set(vec3(0), vec3(0));
    mMag.clear();
    mWax9.setMagOffset(vec3(0));
}
                
void CalibrationApp::calibrate()
{
    mWax9.setMagOffset(-mBox.getCenter());
}


CINDER_APP( CalibrationApp, RendererGl(RendererGl::Options().msaa(8)), [](CalibrationApp::Settings *s)
{
    s->setWindowSize(1000, 800);
})
