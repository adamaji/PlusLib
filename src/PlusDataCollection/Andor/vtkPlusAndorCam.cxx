/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "vtkImageData.h"
#include "vtkPlusAndorCam.h"
#include "ATMCD32D.h"

vtkStandardNewMacro(vtkPlusAndorCam);

// ----------------------------------------------------------------------------
// Public member operators ----------------------------------------------------

// ----------------------------------------------------------------------------
void vtkPlusAndorCam::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Shutter: " << AndorShutter << std::endl;
  os << indent << "ExposureTime: " << AndorExposureTime << std::endl;
  os << indent << "HSSpeed: " << AndorHSSpeed[0] << AndorHSSpeed[1] << std::endl;
  os << indent << "PreAmpGain: " << AndorPreAmpGain << std::endl;
  os << indent << "AcquisitionMode: " << AndorAcquisitionMode << std::endl;
  os << indent << "ReadMode: " << AndorReadMode << std::endl;
  os << indent << "TriggerMode: " << AndorTriggerMode << std::endl;
  os << indent << "Hbin: " << AndorHbin << std::endl;
  os << indent << "Vbin: " << AndorVbin << std::endl;
  os << indent << "CoolTemperature: " << AndorCoolTemperature << std::endl;
  os << indent << "SafeTemperature: " << AndorSafeTemperature << std::endl;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusAndorCam::ReadConfiguration");
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  // Load the camera properties parameters -----------------------------------------------

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorShutter, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(float, AndorExposureTime, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorPreAmpGain, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorAcquisitionMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorReadMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorTriggerMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorHbin, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorVbin, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorCoolTemperature, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AndorSafeTemperature, deviceConfig);

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceConfig, rootConfigElement);

  deviceConfig->SetIntAttribute("Shutter", this->AndorShutter);
  deviceConfig->SetFloatAttribute("ExposureTime", this->AndorExposureTime);
  deviceConfig->SetIntAttribute("PreAmptGain", this->AndorPreAmpGain);
  deviceConfig->SetIntAttribute("AcquitisionMode", this->AndorAcquisitionMode);
  deviceConfig->SetIntAttribute("ReadMode", this->AndorReadMode);
  deviceConfig->SetIntAttribute("TriggerMode", this->AndorTriggerMode);
  deviceConfig->SetIntAttribute("Hbin", this->AndorHbin);
  deviceConfig->SetIntAttribute("Vbin", this->AndorVbin);
  deviceConfig->SetIntAttribute("CoolTemperature", this->AndorCoolTemperature);
  deviceConfig->SetIntAttribute("SafeTemperature", this->AndorSafeTemperature);

  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::NotifyConfigured()
{
  if (this->OutputChannels.size() > 1)
  {
    LOG_WARNING("vtkPlusAndorCam is expecting one output channel and there are "
                << this->OutputChannels.size() << " channels. First output channel will be used.");
  }

  if (this->OutputChannels.empty())
  {
    LOG_ERROR("No output channels defined for vtkPlusIntersonVideoSource. Cannot proceed.");
    this->CorrectlyConfigured = false;
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
std::string vtkPlusAndorCam::GetSdkVersion()
{
  std::ostringstream versionString;

  unsigned int epromVer;
  unsigned int cofVer;
  unsigned int driverRev;
  unsigned int driverVer;
  unsigned int dllRev;
  unsigned int dllVer;

  unsigned int andorResult = GetSoftwareVersion(&epromVer, &cofVer, &driverRev, &driverVer, &dllRev, &dllVer);

  versionString << "Andor SDK version: "  << dllVer << "." << dllRev << std::endl;
  return versionString.str();
}

// ------------------------------------------------------------------------
// Protected member operators ---------------------------------------------

 //----------------------------------------------------------------------------
vtkPlusAndorCam::vtkPlusAndorCam()
{
  this->RequireImageOrientationInConfiguration = true;

  // Initialize camera parameters ----------------------------------------
  this->AndorShutter                               = 0;
  this->AndorExposureTime                          = 1.0f; //seconds
  this->AndorHSSpeed                               = { 0,1 };
  this->AndorPreAmpGain                            = 0;
  this->AndorAcquisitionMode                       = 1; //single scan
  this->AndorReadMode                              = 4; // Image
  this->AndorTriggerMode                           = 0; // Internal
  this->AndorHbin                                  = 1; //Horizontal binning
  this->AndorVbin                                  = 1; //Vertical binning
  this->AndorCoolTemperature                       = -50;
  this->AndorSafeTemperature                       = 5;
  this->AndorCurrentTemperature                    = 0;

  // No callback function provided by the device,
  // so the data capture thread will be used
  // to poll the hardware and add new items to the buffer
  this->StartThreadForInternalUpdates          = true;
  this->AcquisitionRate                        = 1;
}

// ----------------------------------------------------------------------------
vtkPlusAndorCam::~vtkPlusAndorCam()
{
  if (!this->Connected)
  {
    this->Disconnect();
  }
}

// Check for the error codes returned from AndorSdK ---------------------------
PlusStatus vtkPlusAndorCam::CheckAndorSDKError(unsigned int _ui_err, const std::string _cp_func)
{
  if (_ui_err == DRV_SUCCESS)
  {
    return PLUS_SUCCESS;
  }
  else
  {
    LOG_ERROR("Failed AndorSDK operation: " << _cp_func << " with error code: " << _ui_err);
    return PLUS_FAIL;
  }
}


// Initialize Andor Camera ---------------------------------------------

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::InitializeAndorCam()
{
  //Initialize AndorSDK
  unsigned int result = Initialize("");

  if (CheckAndorSDKError(result, "Initialize Andor SDK") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  LOG_INFO("yowzaaa, was able to initialize Andor SDK!");

  // Check the safe temperature, and the maximum allowable temperature on the camera.
  // Use the min of the two as the safe temp.
  int MinTemp, MaxTemp;
  result = GetTemperatureRange(&MinTemp, &MaxTemp);
  if (MaxTemp < this->AndorSafeTemperature)
    this->AndorSafeTemperature = MaxTemp;
  LOG_INFO("The temperature range for the connected Andor Camera is: " << MinTemp << " and " << MaxTemp);

  //Check the temperature of the camera, and ajust it if needed.
  CheckAndAdjustCameraTemperature(this->AndorCoolTemperature);

  // Setup the camera

  // Prepare acquisition

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::InternalConnect()
{
  LOG_TRACE("vtkPlusAndorCam::InternalConnect");
  //LOG_DEBUG("AndorSDK version " << bmDLLVer() << ", USB probe DLL version " << usbDLLVer());

  return this->InitializeAndorCam();
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::InternalDisconnect()
{
  LOG_DEBUG("Disconnecting from Andor");

  unsigned int andorResult;
  
  int temperature;
  GetTemperature(&temperature);
  
  // It is vital to raise the temperature to above 0 before closing
  // to reduce damage to the head. This routine simply blocks exiting
  // the program until the temp is above 0
  if (temperature < 0) 
  {
    LOG_INFO("Raising the Andor camera cooler temperature to above 0");
    if (CheckAndAdjustCameraTemperature(this->AndorSafeTemperature) != PLUS_SUCCESS)
    {
      return PLUS_FAIL;
    }
  }
  
  // Switch off the cooler
  andorResult = CoolerOFF();
  if (CheckAndorSDKError(andorResult, "CoolerOff") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  // Free internal memory
  andorResult = FreeInternalMemory();
  if (CheckAndorSDKError(andorResult, "FreeInternalMemory") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  // Shut down the camera
  andorResult = ShutDown();
  if (CheckAndorSDKError(andorResult, "ShutDown") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}


//// ----------------------------------------------------------------------------
//PlusStatus vtkPlusCapistranoVideoSource::InternalStartRecording()
//{
//  FreezeDevice(false);
//  return PLUS_SUCCESS;
//}
//
//// ----------------------------------------------------------------------------
//PlusStatus vtkPlusCapistranoVideoSource::InternalStopRecording()
//{
//  FreezeDevice(true);
//  return PLUS_SUCCESS;
//}
//

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::InternalUpdate()
{
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::CheckAndAdjustCameraTemperature(int targetTemp)
{
  int MinTemp, MaxTemp;

  // check if temp is in valid range
  unsigned int  errorValue = GetTemperatureRange(&MinTemp, &MaxTemp);
  if (CheckAndorSDKError(errorValue, "Get Temperature Range for Andor") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }
  
  if (targetTemp < MinTemp || targetTemp > MaxTemp)
  {
    LOG_ERROR("Requested temperature for Andor camera is out of range");
    return PLUS_FAIL;
  }

  // if it is in range, switch on cooler and set temp
  errorValue = CoolerON();
  if (CheckAndorSDKError(errorValue, "Turn Andor Camera Cooler on") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }
  
  errorValue = SetTemperature(targetTemp);
#ifdef _WIN32
  Sleep(10000);
#else
  usleep(2000000);
#endif
  if (CheckAndorSDKError(errorValue, "Set Andor Cam temperature") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  // Check the temperature
  errorValue = GetTemperature(&this->AndorCurrentTemperature);
  switch (errorValue) {
    case DRV_TEMPERATURE_STABILIZED:
      LOG_INFO("Temperature has stabilized at " << this->AndorCurrentTemperature << " (C)");
      break;
    case DRV_TEMPERATURE_NOT_REACHED:
      LOG_INFO("Current temperature is " << this->AndorCurrentTemperature << " (C)");
      break;
    default:
      LOG_INFO("Temperature control is disabled " << this->AndorCurrentTemperature);
      break;
  }

  return PLUS_SUCCESS;
}

//
//// ----------------------------------------------------------------------------
//PlusStatus vtkPlusCapistranoVideoSource::FreezeDevice(bool freeze)
//{
//  RETURN_WITH_FAIL_IF(this->Internal->ProbeHandle == NULL,
//                      "vtkPlusIntersonVideoSource::FreezeDevice failed: device not connected");
//  RETURN_WITH_FAIL_IF(!usbHardwareDetected(),
//                      "Freeze failed, no hardware is detected");
//
//  if (this->Frozen == freeze) //already in desired mode
//  {
//    return PLUS_SUCCESS;
//  }
//
//  this->Frozen = freeze;
//  if (this->Frozen)
//  {
//    usbProbe(STOP);
//  }
//  else
//  {
//    usbClearCineBuffers();
//    this->FrameNumber = 0;
//    RETURN_WITH_FAIL_IF(this->UpdateUSParameters() == PLUS_FAIL,
//                        "Failed to update US parameters");
//    usbProbe(RUN);
//  }
//
//  return PLUS_SUCCESS;
//}
//
//// ----------------------------------------------------------------------------
//bool vtkPlusCapistranoVideoSource::IsFrozen()
//{
//  return Frozen;
//}
//
//// ----------------------------------------------------------------------------
//PlusStatus vtkPlusCapistranoVideoSource::WaitForFrame()
//{
//  bool  nextFrameReady = (usbWaitFrame() == 1);
//  DWORD usbErrorCode   = usbError();
//
//  if (this->Frozen)
//  {
//    return PLUS_SUCCESS;
//  }
//
//  static bool messagePrinted = false;
//
//  switch (usbErrorCode)
//  {
//    case USB_SUCCESS:
//      messagePrinted = false;
//      break;
//    case USB_FAILED:
//      if (!messagePrinted)
//      {
//        LOG_ERROR("USB: FAILURE. Probe was removed?");
//        messagePrinted = true;
//      }
//      return PLUS_FAIL;
//    case USB_TIMEOUT2A:
//    case USB_TIMEOUT2B:
//    case USB_TIMEOUT6A:
//    case USB_TIMEOUT6B:
//      if (nextFrameReady) // timeout is fine if we're in synchronized mode, so only log error if next frame is ready
//      {
//        LOG_WARNING("USB timeout");
//      }
//      break;
//    case USB_NOTSEQ:
//      if (!messagePrinted)
//      {
//        LOG_ERROR("Lost Probe Synchronization. Please check probe cables and restart.");
//        messagePrinted = true;
//      }
//      FreezeDevice(true);
//      FreezeDevice(false);
//      break;
//    case USB_STOPPED:
//      if (!messagePrinted)
//      {
//        LOG_ERROR("USB: Stopped. Check probe and restart.");
//        messagePrinted = true;
//      }
//      break;
//    default:
//      if (!messagePrinted)
//      {
//        LOG_ERROR("USB: Unknown USB error: " << usbErrorCode);
//        messagePrinted = true;
//      }
//      FreezeDevice(true);
//      FreezeDevice(false);
//      break;
//  }
//
//  return PLUS_SUCCESS;
//}

// Setup the Andor camera parameters ----------------------------------------------

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorShutter(int shutter)
{
  this->AndorShutter = shutter;
  unsigned int result = SetShutter(1, this->AndorShutter, 0, 0);
  if (CheckAndorSDKError(result, "SetShutter") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }
  
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorShutter()
{
  return this->AndorShutter;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorExposureTime(float exposureTime)
{
  this->AndorExposureTime = exposureTime;

  unsigned int result = SetExposureTime(this->AndorExposureTime);
  if (CheckAndorSDKError(result, "SetExposure") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
float vtkPlusAndorCam::GetAndorExposureTime()
{
  return this->AndorExposureTime;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorPreAmpGain(int preAmpGain)
{
  this->AndorPreAmpGain = preAmpGain;

  unsigned int result = SetPreAmpGain(this->AndorPreAmpGain);
  if (CheckAndorSDKError(result, "SetPreAmpGain") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorPreAmpGain()
{
  return this->AndorPreAmpGain;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorAcquisitionMode(int acquisitionMode)
{
  this->AndorAcquisitionMode = acquisitionMode;

  unsigned int result = SetAcquisitionMode(this->AndorAcquisitionMode);
  if (CheckAndorSDKError(result, "SetAcquisitionMode") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorAcquisitionMode()
{
  return this->AndorAcquisitionMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorReadMode(int readMode)
{
  this->AndorReadMode = readMode;

  unsigned int result = SetReadMode(this->AndorReadMode);
  if (CheckAndorSDKError(result, "SetReadMode") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorReadMode()
{
  return this->AndorReadMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorTriggerMode(int triggerMode)
{
  this->AndorTriggerMode = triggerMode;

  unsigned int result = SetTriggerMode(this->AndorTriggerMode);
  if (CheckAndorSDKError(result, "SetTriggerMode") != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorTriggerMode()
{
  return this->AndorTriggerMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorHbin(int hbin)
{
  this->AndorHbin = hbin;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorHbin()
{
  return this->AndorHbin;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorVbin(int vbin)
{
  this->AndorVbin = vbin;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorVbin()
{
  return this->AndorVbin;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorCoolTemperature(int coolTemp)
{
  this->AndorCoolTemperature = coolTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorCoolTemperature()
{
  return this->AndorCoolTemperature;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCam::SetAndorSafeTemperature(int safeTemp)
{
  this->AndorSafeTemperature = safeTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCam::GetAndorSafeTemperature()
{
  return this->AndorSafeTemperature;
}
