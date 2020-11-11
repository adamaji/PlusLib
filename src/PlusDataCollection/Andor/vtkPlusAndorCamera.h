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

 Requires PLUS_USE_ANDOR_CAMERA option in CMake.
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

  /*! Horizontal and vertical binning. Allowed values: 1, 2, 4, 8. */
  PlusStatus SetHorizontalBins(int bins);
  PlusStatus SetVerticalBins(int bins);

  /*! Horizontal and vertical shift speed. */
  PlusStatus SetHSSpeed(int type, int index);
  PlusStatus SetVSSpeed(int index);

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

  /*! Normal operating temperature (degrees celsius). */
  PlusStatus SetCoolTemperature(int coolTemp);
  int GetCoolTemperature();

  /*! Lowest temperature at which it is safe to shut down the camera. */
  PlusStatus SetSafeTemperature(int safeTemp);
  int GetSafeTemperature();

  /*! Get the current temperature of the camera in degrees celsius. */
  float GetCurrentTemperature();

  /*! Paths to additive and multiplicative bias correction images. */
  PlusStatus SetBiasCorrectionImage(std::string biasFilePath);
  std::string GetBiasCorrectionImage()
  {
    return biasCorrection;
  }
  PlusStatus SetFlatCorrectionImage(std::string flatFilePath);
  std::string GetFlatCorrectionImage()
  {
    return flatCorrection;
  }

  /*! -1 uses currently active settings. */
  PlusStatus AcquireBLIFrame(int binning, int vsSpeed, int hsSpeed, float exposureTime);

  /*! -1 uses currently active settings. */
  PlusStatus AcquireGrayscaleFrame(int binning, int vsSpeed, int hsSpeed, float exposureTime);

  /*! Convenience function to save a bias frame for a certain binning/speed configuration. */
  PlusStatus AcquireBiasFrame(std::string biasFilePath, int binning, int vsSpeed, int hsSpeed);

  /*! Wait for the camera to reach operating temperature (e.g. -70°C). */
  void WaitForCooldown();

  /*! Check the return status of Andor SDK functions. */
  unsigned int checkStatus(unsigned int returnStatus, std::string functionName);

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

  using DataSourceArray = std::vector<vtkPlusDataSource*>;

  /*! Initialize all data sources of the provided port */
  void InitializePort(DataSourceArray& port);

  /*! Acquire a single frame using current parameters. Data is put in the frameBuffer ivar. */
  PlusStatus AcquireFrame(float exposure, int shutterMode, int binning, int vsSpeed, int hsSpeed);

  /*! Data from the frameBuffer ivar is added to the provided data source. */
  void AddFrameToDataSource(DataSourceArray& ds);

  /*! Applies bias correction for dark current, flat correction and lens distortion. */
  void ApplyFrameCorrections();

  /*! This will be triggered regularly if this->StartThreadForInternalUpdates is true.
   * Framerate is controlled by this->AcquisitionRate. This is meant for debugging.
   */
  PlusStatus InternalUpdate() override
  {
    AcquireBLIFrame(-1, -1, -1);
    return PLUS_SUCCESS;
  }

  /*! Dev flag whether to use the cooler during acquisition.
      It is better for the camera to undergo fewer temperature changes, so use sparingly.
   */
  PlusStatus SetUseCooling(bool useCooling);
  bool GetUseCooling();

  /*! Cooler Status and Mode control. When CoolerMode is set on, the cooler
      will be kept online when the camera is shutdown. This is helpful to
      reduce the cooling cycles the camera undergoes. Turning the Cooler ON
      and OFF should be done sparingly for the same reason.
  */
  int IsCoolerOn();
  PlusStatus TurnCoolerON();
  PlusStatus TurnCoolerOFF();
  PlusStatus SetCoolerMode(int mode);

  int Shutter = 0;
  float ExposureTime = 1.0; // seconds
  int HorizontalBins = 1;
  int VerticalBins = 1;
  int HSSpeed[2] = { 0, 1 };  // type, index
  int VSSpeed = 0;  // index
  int PreAmpGain = 0;

  // TODO: Need to handle differet cases for read/acquisiton modes?

  /*! From AndorSDK:=> 1: Single Scan   2: Accumulate   3: Kinetics   4: Fast Kinetics   5: Run till abort  */
  int AcquisitionMode = 1;

  /*! From AndorSDK:=> 0: Full Vertical Binning   1: Multi-Track   2: Random-Track   3: Single-Track   4: Image */
  int ReadMode = 4;

  /*! From AndorSDK:=> 0. Internal   1. External  6. External Start  7. External Exposure(Bulb)  9. External FVB EM(only valid for EM Newton models in FVB mode) 10. Software Trigger  12. External Charge Shifting */
  int TriggerMode = 0;

  /*! Temperatures are in °C (degrees Celsius) */
  bool UseCooling = true;  // dev param to bypass cooling procedures
  int CoolerMode = 0;  // whether to return to ambient temperature on ShutDown
  int CoolTemperature = -50;
  int SafeTemperature = 5;
  float CurrentTemperature = 0.123456789; // easy to spot as uninitialized

  FrameSizeType frameSize = {1024, 1024, 1};
  std::vector<uint16_t> rawFrame;
  double currentTime = UNDEFINED_TIMESTAMP;

  // {f_x}{0}{c_x}
  // {0}{f_y}{c_y}
  // {0}{0}{1}
  double cameraIntrinsics[9] = { 0 };
  double distanceCoefficients[4] = { 0 }; // k_1, k_2, p_1, p_2
  std::string flatCorrection; // filepath to master flat image
  std::string biasCorrection; // filepath to master bias image

  DataSourceArray BLIraw;
  DataSourceArray BLIrectified;
  DataSourceArray BLIdark;
  DataSourceArray GrayRaw;
  DataSourceArray GrayRectified;
  DataSourceArray GrayDark;
};

#endif