/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "vtkImageData.h"
#include "vtkPlusAndorCamera.h"
#include "ATMCD32D.h"
#include "igtlOSUtil.h" // for Sleep

#define AndorCheckErrorValueAndFailIfNeeded(returnValue, functionName) \
  if(returnValue != DRV_SUCCESS)                                       \
  {                                                                    \
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << " with error code: " << returnValue);                 \
    return PLUS_FAIL;                                                  \
  }

vtkStandardNewMacro(vtkPlusAndorCamera);

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::PrintSelf(ostream& os, vtkIndent indent)
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
  os << indent << "CurrentTemperature: " << AndorCurrentTemperature << std::endl;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusAndorCamera::ReadConfiguration");
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
PlusStatus vtkPlusAndorCamera::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
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
PlusStatus vtkPlusAndorCamera::NotifyConfigured()
{
  if(this->OutputChannels.size() > 1)
  {
    LOG_WARNING("vtkPlusAndorCamera is expecting one output channel and there are "
                << this->OutputChannels.size() << " channels. First output channel will be used.");
  }

  if(this->OutputChannels.empty())
  {
    LOG_ERROR("No output channels defined for vtkPlusIntersonVideoSource. Cannot proceed.");
    this->CorrectlyConfigured = false;
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
std::string vtkPlusAndorCamera::GetSdkVersion()
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
vtkPlusAndorCamera::vtkPlusAndorCamera()
{
  this->RequireImageOrientationInConfiguration = true;

  // Initialize camera parameters ----------------------------------------
  this->AndorShutter                               = 0;
  this->AndorExposureTime                          = 1.0f; //seconds
  this->AndorHSSpeed                               = { 0, 1 };
  this->AndorPreAmpGain                            = 0;
  this->AndorAcquisitionMode                       = 1; //single scan
  this->AndorReadMode                              = 4; // Image
  this->AndorTriggerMode                           = 0; // Internal
  this->AndorHbin                                  = 1; //Horizontal binning
  this->AndorVbin                                  = 1; //Vertical binning
  this->AndorCoolTemperature                       = -50;
  this->AndorSafeTemperature                       = 5;
  this->AndorCurrentTemperature                    = 0.123456789; // easy to spot as uninitialized

  // No callback function provided by the device,
  // so the data capture thread will be used
  // to poll the hardware and add new items to the buffer
  this->StartThreadForInternalUpdates          = true;
  this->AcquisitionRate                        = 1;
}

// ----------------------------------------------------------------------------
vtkPlusAndorCamera::~vtkPlusAndorCamera()
{
  if(!this->Connected)
  {
    this->Disconnect();
  }
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InitializeAndorCamera()
{
  unsigned int result = Initialize("");
  AndorCheckErrorValueAndFailIfNeeded(result, "Initialize Andor SDK")
  LOG_DEBUG("Andor SDK initialized.");

  // Check the safe temperature, and the maximum allowable temperature on the camera.
  // Use the min of the two as the safe temp.
  int MinTemp, MaxTemp;
  result = GetTemperatureRange(&MinTemp, &MaxTemp);
  if(MaxTemp < this->AndorSafeTemperature)
  {
    this->AndorSafeTemperature = MaxTemp;
  }
  LOG_INFO("The temperature range for the connected Andor Camera is: " << MinTemp << " and " << MaxTemp);

  if(this->AndorCoolTemperature < MinTemp || this->AndorCoolTemperature > MaxTemp)
  {
    LOG_ERROR("Requested temperature for Andor camera is out of range");
    return PLUS_FAIL;
  }

  result = CoolerON();
  AndorCheckErrorValueAndFailIfNeeded(result, "Turn Andor Camera Cooler on")

  result = SetTemperature(this->AndorCoolTemperature);
  AndorCheckErrorValueAndFailIfNeeded(result, "Set Andor Camera cool temperature")

  GetAndorCurrentTemperature(); // logs the status and temperature

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalConnect()
{
  LOG_TRACE("vtkPlusAndorCamera::InternalConnect");
  if(this->InitializeAndorCamera() != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  // Setup the camera

  // Prepare acquisition

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalDisconnect()
{
  LOG_DEBUG("Disconnecting from Andor");

  int status;
  unsigned result = IsCoolerOn(&status);
  AndorCheckErrorValueAndFailIfNeeded(result, "IsCoolerOn")

  if(status)
  {
    GetAndorCurrentTemperature(); // updates this->AndorCurrentTemperature
    if(this->AndorCurrentTemperature < this->AndorSafeTemperature)
    {
      LOG_INFO("Temperature yet not at a safe point, turning the Cooler Off");
      result = CoolerOFF();
      AndorCheckErrorValueAndFailIfNeeded(result, "CoolerOff")

      while(this->AndorCurrentTemperature < this->AndorSafeTemperature)
      {
        igtl::Sleep(5000); // wait a bit
        GetAndorCurrentTemperature(); // logs the status and temperature
      }
    }
  }

  result = FreeInternalMemory();
  AndorCheckErrorValueAndFailIfNeeded(result, "FreeInternalMemory")

  result = ShutDown();
  AndorCheckErrorValueAndFailIfNeeded(result, "ShutDown")

  LOG_INFO("Andor camera shut down successfully");
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
PlusStatus vtkPlusAndorCamera::InternalUpdate()
{
  return PLUS_SUCCESS;
}

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
PlusStatus vtkPlusAndorCamera::SetAndorShutter(int shutter)
{
  this->AndorShutter = shutter;
  unsigned int result = SetShutter(1, this->AndorShutter, 0, 0);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetShutter")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorShutter()
{
  return this->AndorShutter;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorExposureTime(float exposureTime)
{
  this->AndorExposureTime = exposureTime;

  unsigned int result = SetExposureTime(this->AndorExposureTime);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetExposureTime")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
float vtkPlusAndorCamera::GetAndorExposureTime()
{
  return this->AndorExposureTime;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorPreAmpGain(int preAmpGain)
{
  this->AndorPreAmpGain = preAmpGain;

  unsigned int result = SetPreAmpGain(this->AndorPreAmpGain);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetPreAmpGain")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorPreAmpGain()
{
  return this->AndorPreAmpGain;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorAcquisitionMode(int acquisitionMode)
{
  this->AndorAcquisitionMode = acquisitionMode;

  unsigned int result = SetAcquisitionMode(this->AndorAcquisitionMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetAcquisitionMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorAcquisitionMode()
{
  return this->AndorAcquisitionMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorReadMode(int readMode)
{
  this->AndorReadMode = readMode;

  unsigned int result = SetReadMode(this->AndorReadMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetReadMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorReadMode()
{
  return this->AndorReadMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorTriggerMode(int triggerMode)
{
  this->AndorTriggerMode = triggerMode;

  unsigned int result = SetTriggerMode(this->AndorTriggerMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetTriggerMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorTriggerMode()
{
  return this->AndorTriggerMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorHbin(int hbin)
{
  this->AndorHbin = hbin;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorHbin()
{
  return this->AndorHbin;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorVbin(int vbin)
{
  this->AndorVbin = vbin;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorVbin()
{
  return this->AndorVbin;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorCoolTemperature(int coolTemp)
{
  this->AndorCoolTemperature = coolTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorCoolTemperature()
{
  return this->AndorCoolTemperature;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAndorSafeTemperature(int safeTemp)
{
  this->AndorSafeTemperature = safeTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAndorSafeTemperature()
{
  return this->AndorSafeTemperature;
}

float vtkPlusAndorCamera::GetAndorCurrentTemperature()
{
  unsigned result = GetTemperatureF(&this->AndorCurrentTemperature);
  switch(result)
  {
    case DRV_TEMPERATURE_STABILIZED:
      LOG_INFO("Temperature has stabilized at " << this->AndorCurrentTemperature << " °C");
      break;
    case DRV_TEMPERATURE_NOT_REACHED:
      LOG_INFO("Cooling down, current temperature is " << this->AndorCurrentTemperature << " °C");
      break;
    default:
      LOG_INFO("Current temperature is " << this->AndorCurrentTemperature << " °C");
      break;
  }

  return this->AndorCurrentTemperature;
}
