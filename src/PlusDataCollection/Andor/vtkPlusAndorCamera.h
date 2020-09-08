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
  PlusStatus SetShutter(int shutter);
  int GetShutter();

  /*! Frame exposure time, seconds. Sets to the nearest valid value not less than the given value. */
  PlusStatus SetExposureTime(float exposureTime);
  float GetExposureTime();

  /*! Index of the pre-amp gain, not the actual value. */
  PlusStatus SetPreAmpGain(int preAmptGain);
  int GetPreAmpGain();

  /*! Acquisition mode. Valid values:
   * 1 Single Scan
   * 2 Accumulate
   * 3 Kinetics
   * 4 Fast Kinetics
   * 5 Run till abort
   */
  PlusStatus SetAcquisitionMode(int acquisitionMode);
  int GetAcquisitionMode();

  /*! Readout mode. Valid values:
   * 0 Full Vertical Binning
   * 1 Multi-Track
   * 2 Random-Track
   * 3 Single-Track
   * 4 Image
   */
  PlusStatus SetReadMode(int setReadMode);
  int GetReadMode();

  /*! Trigger mode. Valid values:
   * 0. Internal
   * 1. External
   * 6. External Start
   * 7. External Exposure (Bulb)
   * 9. External FVB EM (only valid for EM Newton models in FVB mode)
   * 10. Software Trigger
   * 12. External Charge Shifting
   */
  PlusStatus SetTriggerMode(int triggerMode);
  int GetTriggerMode();

  /*! Horizontal binning */
  PlusStatus SetHbin(int hbin);
  int GetHbin();

  /*! Vertical binning */
  PlusStatus SetVbin(int vbin);
  int GetVbin();

  /*! Normal operating temperature (degrees celsius). */
  PlusStatus SetCoolTemperature(int coolTemp);
  int GetCoolTemperature();

  /*! Lowest temperature at which it is safe to shut down the camera. */
  PlusStatus SetSafeTemperature(int safeTemp);
  int GetSafeTemperature();

  /*! Get the current temperature of the camera in degrees celsius. */
  float GetCurrentTemperature();

  /*! Uses currently active settings. */
  PlusStatus AcquireBLIFrame();

  /*! exposureTime parameter overrides current class' exposure time setting. */
  PlusStatus AcquireGrayscaleFrame(float exposureTime);

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
  PlusStatus InternalStartRecording() override;

  /*! Device-specific recording stop */
  PlusStatus InternalStopRecording() override;

  /*! Initialize vtkPlusAndorCamera */
  PlusStatus InitializeAndorCamera();

  /*! Wait for the camera to reach operating temperature (e.g. -70°C). */
  void WaitForCooldown();

  /*! Acquire a single frame using current parameters. Data is put in the frameBuffer ivar. */
  PlusStatus AcquireFrame();


  int                AndorShutter = 0;
  float              AndorExposureTime = 1.0; // seconds
  std::array<int, 2> AndorHSSpeed = { 0, 1 };
  int                AndorPreAmpGain = 0;

  // TODO: Need to handle differet cases for read/acquisiton modes?

  /*! From AndorSDK:=> 1: Single Scan   2: Accumulate   3: Kinetics   4: Fast Kinetics   5: Run till abort  */
  int AndorAcquisitionMode = 1;

  /*! From AndorSDK:=> 0: Full Vertical Binning   1: Multi-Track   2: Random-Track   3: Single-Track   4: Image */
  int AndorReadMode = 4;

  /*! From AndorSDK:=> 0. Internal   1. External  6. External Start  7. External Exposure(Bulb)  9. External FVB EM(only valid for EM Newton models in FVB mode) 10. Software Trigger  12. External Charge Shifting */
  int AndorTriggerMode = 0;

  /*! Temperatures are in °C (degrees Celsius) */
  int   AndorCoolTemperature = -50;
  int   AndorSafeTemperature = 5;
  float AndorCurrentTemperature = 0.123456789; // easy to spot as uninitialized

  int AndorHbin = 1;
  int AndorVbin = 1;
  int xSize = 1024;
  int ySize = 1024;
  std::vector<uint16_t> rawFrame;

  std::vector<vtkPlusDataSource*> BLIraw;
  std::vector<vtkPlusDataSource*> BLIrectified;
  std::vector<vtkPlusDataSource*> GrayRaw;
  std::vector<vtkPlusDataSource*> GrayRectified;
};

#endif