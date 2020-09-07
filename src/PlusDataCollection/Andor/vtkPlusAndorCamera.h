/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlusAndorCamera_h
#define __vtkPlusAndorCamera_h

#include "vtkPlusDataCollectionExport.h"
#include "vtkPlusDevice.h"

/*!
 \class vtkPlusAndorCamera
 \brief Class for acquiring images from Andor cameras

 Requires PLUS_USE_ANDOR_CAM option in CMake.
 Requires the Andor SDK (SDK provided by Andor).

 \ingroup PlusLibDataCollection.
*/
class vtkPlusDataCollectionExport vtkPlusAndorCamera: public vtkPlusDevice
{
public:
  /*! Constructor for a smart pointer of this class*/
  static vtkPlusAndorCamera* New();
  vtkTypeMacro(vtkPlusAndorCamera, vtkPlusDevice);
  virtual void PrintSelf(ostream& os, vtkIndent indent) VTK_OVERRIDE;

  /*! Specify the device connected to this class */
  virtual bool IsTracker() const
  {
    return false;
  }

  /*! Read configuration from xml data */
  virtual PlusStatus ReadConfiguration(vtkXMLDataElement* config);

  /*! Write configuration to xml data */
  virtual PlusStatus WriteConfiguration(vtkXMLDataElement* config);

  /*! Verify the device is correctly configured */
  virtual PlusStatus NotifyConfigured();

  /*! Get the version of SDK */
  virtual std::string GetSdkVersion();

  /*! Shutter mode:
   * 0 Fully Auto
   * 1 Permanently Open
   * 2 Permanently Closed
   * 4 Open for FVB series
   * 5 Open for any series
   * 
   * For an external shutter: Output TTL high signal to open shutter.
   */
  PlusStatus SetAndorShutter(int shutter);
  int GetAndorShutter();

  /*! Frame exposure time, seconds. Sets to the nearest valid value not less than the given value. */
  PlusStatus SetAndorExposureTime(float exposureTime);
  float GetAndorExposureTime();

  /*! Index of the pre-amp gain, not the actual value. */
  PlusStatus SetAndorPreAmpGain(int preAmptGain);
  int GetAndorPreAmpGain();

  // TODO: Need to handle differet cases for read/acquisiton modes?

  /*! Acquisition mode. Valid values:
   * 1 Single Scan
   * 2 Accumulate
   * 3 Kinetics
   * 4 Fast Kinetics
   * 5 Run till abort
   */
  PlusStatus SetAndorAcquisitionMode(int acquisitionMode);
  int GetAndorAcquisitionMode();

  /*! Readout mode. Valid values:
   * 0 Full Vertical Binning
   * 1 Multi-Track
   * 2 Random-Track
   * 3 Single-Track
   * 4 Image
   */
  PlusStatus SetAndorReadMode(int setReadMode);
  int GetAndorReadMode();

  /*! Trigger mode. Valid values:
   * 0. Internal
   * 1. External
   * 6. External Start
   * 7. External Exposure (Bulb)
   * 9. External FVB EM (only valid for EM Newton models in FVB mode)
   * 10. Software Trigger
   * 12. External Charge Shifting
   */
  PlusStatus SetAndorTriggerMode(int triggerMode);
  int GetAndorTriggerMode();

  /*! Horizontal binning */
  PlusStatus SetAndorHbin(int hbin);
  int GetAndorHbin();

  /*! Vertical binning */
  PlusStatus SetAndorVbin(int vbin);
  int GetAndorVbin();

  /*! Normal operating temperature (degrees celsius). */
  PlusStatus SetAndorCoolTemperature(int coolTemp);
  int GetAndorCoolTemperature();

  /*! Lowest temperature at which it is safe to shut down the camera. */
  PlusStatus SetAndorSafeTemperature(int safeTemp);
  int GetAndorSafeTemperature();

  /*! Get the current temperature of the camera in degrees celsius. */
  float GetAndorCurrentTemperature();

  /*! Wait for the camera to reach operating temperature (e.g. -70°C). */
  void WaitForCooldown();

  /*! Acquire a single frame using current parameters. Data is put in the frameBuffer ivar. */
  PlusStatus AcquireFrame();

  vtkPlusAndorCamera(const vtkPlusAndorCamera&) = delete;
  void operator=(const vtkPlusAndorCamera&) = delete;

protected:
  /*! Constructor */
  vtkPlusAndorCamera();

  /*! Destructor */
  ~vtkPlusAndorCamera();

  /*! Device-specific connect */
  virtual PlusStatus InternalConnect();

  /*! Device-specific disconnect */
  virtual PlusStatus InternalDisconnect();

  /*! Device-specific recording start */
  //virtual PlusStatus InternalStartRecording();

  /*! Device-specific recording stop */
  //virtual PlusStatus InternalStopRecording();

  /*! Initialize vtkPlusAndorCamera */
  PlusStatus InitializeAndorCamera();

  /*! The internal function which actually does the grab. */
  PlusStatus InternalUpdate();

  /*! Wait US Data from US device  */
  //PlusStatus WaitForFrame();

  ///* Calculate US Image Display */
  //PlusStatus CalculateDisplay();

  ///* Calculate US Image Display with a given B-Mode view option */
  //PlusStatus CalculateDisplay(unsigned int option);

  ///*! Get probe name from the device */
  //PlusStatus GetProbeNameDevice(std::string& probeName);

  int                AndorShutter;
  float              AndorExposureTime;  // seconds
  std::array<int, 2> AndorHSSpeed;
  int                AndorPreAmpGain;

  /*! From AndorSDK:=> 1: Single Scan   2: Accumulate   3: Kinetics   4: Fast Kinetics   5: Run till abort  */
  int AndorAcquisitionMode;

  /*! From AndorSDK:=> 0: Full Vertical Binning   1: Multi-Track   2: Random-Track   3: Single-Track   4: Image */
  int AndorReadMode;

  /*! From AndorSDK:=> 0. Internal   1. External  6. External Start  7. External Exposure(Bulb)  9. External FVB EM(only valid for EM Newton models in FVB mode) 10. Software Trigger  12. External Charge Shifting */
  int AndorTriggerMode;

  int AndorHbin;
  int AndorVbin;

  /*! Temperatures are in °C (degrees Celsius) */
  int   AndorCoolTemperature;
  int   AndorSafeTemperature;
  float AndorCurrentTemperature;

  int xSize;
  int ySize;
  std::vector<uint16_t> frameBuffer;
};

#endif