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

  os << indent << "Shutter: " << Shutter << std::endl;
  os << indent << "ExposureTime: " << ExposureTime << std::endl;
  os << indent << "HSSpeed: " << HSSpeed[0] << HSSpeed[1] << std::endl;
  os << indent << "PreAmpGain: " << PreAmpGain << std::endl;
  os << indent << "AcquisitionMode: " << AcquisitionMode << std::endl;
  os << indent << "ReadMode: " << ReadMode << std::endl;
  os << indent << "TriggerMode: " << TriggerMode << std::endl;
  os << indent << "CoolTemperature: " << CoolTemperature << std::endl;
  os << indent << "SafeTemperature: " << SafeTemperature << std::endl;
  os << indent << "CurrentTemperature: " << CurrentTemperature << std::endl;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusAndorCamera::ReadConfiguration");
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  // Load the camera properties parameters -----------------------------------------------

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, Shutter, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(float, ExposureTime, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, PreAmpGain, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AcquisitionMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, ReadMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, TriggerMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, CoolTemperature, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, SafeTemperature, deviceConfig);

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceConfig, rootConfigElement);

  deviceConfig->SetIntAttribute("Shutter", this->Shutter);
  deviceConfig->SetFloatAttribute("ExposureTime", this->ExposureTime);
  deviceConfig->SetIntAttribute("PreAmpGain", this->PreAmpGain);
  deviceConfig->SetIntAttribute("AcquisitionMode", this->AcquisitionMode);
  deviceConfig->SetIntAttribute("ReadMode", this->ReadMode);
  deviceConfig->SetIntAttribute("TriggerMode", this->TriggerMode);
  deviceConfig->SetIntAttribute("CoolTemperature", this->CoolTemperature);
  deviceConfig->SetIntAttribute("SafeTemperature", this->SafeTemperature);

  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::NotifyConfigured()
{
  if(this->OutputChannels.empty())
  {
    LOG_ERROR("No output channels defined for vtkPlusAndorCamera. Cannot proceed.");
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
  this->RequirePortNameInDeviceSetConfiguration = true;

  // We will acquire the frames sporadically,
  // and usually with long exposure times.
  // We don't want polling to interfere with it.
  this->StartThreadForInternalUpdates = false;

  unsigned result = Initialize("");
  if(result != DRV_SUCCESS)
  {
    LOG_ERROR("Andor SDK could not be initialized");
  }
  else
  {
    LOG_DEBUG("Andor SDK initialized.");
  }
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
  // Check the safe temperature, and the maximum allowable temperature on the camera.
  // Use the min of the two as the safe temp.
  int MinTemp, MaxTemp;
  unsigned result = GetTemperatureRange(&MinTemp, &MaxTemp);
  if(MaxTemp < this->SafeTemperature)
  {
    this->SafeTemperature = MaxTemp;
  }
  LOG_INFO("The temperature range for the connected Andor Camera is: " << MinTemp << " and " << MaxTemp);

  if(this->CoolTemperature < MinTemp || this->CoolTemperature > MaxTemp)
  {
    LOG_ERROR("Requested temperature for Andor camera is out of range");
    return PLUS_FAIL;
  }

  result = CoolerON();
  AndorCheckErrorValueAndFailIfNeeded(result, "Turn Andor Camera Cooler on")

  result = SetTemperature(this->CoolTemperature);
  AndorCheckErrorValueAndFailIfNeeded(result, "Set Andor Camera cool temperature")

  GetCurrentTemperature(); // logs the status and temperature

  result = GetDetector(&xSize, &ySize);
  AndorCheckErrorValueAndFailIfNeeded(result, "GetDetectorSize")

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

  this->GetVideoSourcesByPortName("BLIraw", BLIraw);
  this->GetVideoSourcesByPortName("BLIrectified", BLIrectified);
  this->GetVideoSourcesByPortName("GrayRaw", GrayRaw);
  this->GetVideoSourcesByPortName("GrayRectified", GrayRectified);
  if (BLIraw.size()+BLIrectified.size()+GrayRaw.size()+GrayRectified.size()==0)
  {
      vtkPlusDataSource* aSource = nullptr;
      if (this->GetFirstActiveOutputVideoSource(aSource) != PLUS_SUCCESS || aSource == nullptr)
      {
          LOG_ERROR("Standard data sources are not defined, and unable to retrieve the video source in the capturing device.");
          return PLUS_FAIL;
      }
      BLIraw.push_back(aSource); // this is the default port
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalDisconnect()
{
  LOG_DEBUG("Disconnecting from Andor");
  if(IsRecording())
  {
    this->InternalStopRecording();
  }

  int status;
  unsigned result = IsCoolerOn(&status);
  AndorCheckErrorValueAndFailIfNeeded(result, "IsCoolerOn")

  if(status)
  {
    GetCurrentTemperature(); // updates this->CurrentTemperature
    if(this->CurrentTemperature < this->SafeTemperature)
    {
      LOG_INFO("Temperature yet not at a safe point, turning the Cooler Off");
      result = CoolerOFF();
      AndorCheckErrorValueAndFailIfNeeded(result, "CoolerOff")

      while(this->CurrentTemperature < this->SafeTemperature)
      {
        igtl::Sleep(5000); // wait a bit
        GetCurrentTemperature(); // logs the status and temperature
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


// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalStartRecording()
{
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalStopRecording()
{
  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
float vtkPlusAndorCamera::GetCurrentTemperature()
{
  unsigned result = GetTemperatureF(&this->CurrentTemperature);
  switch(result)
  {
    case DRV_TEMPERATURE_STABILIZED:
      LOG_INFO("Temperature has stabilized at " << this->CurrentTemperature << " °C");
      break;
    case DRV_TEMPERATURE_NOT_REACHED:
      LOG_INFO("Cooling down, current temperature is " << this->CurrentTemperature << " °C");
      break;
    default:
      LOG_INFO("Current temperature is " << this->CurrentTemperature << " °C");
      break;
  }

  return this->CurrentTemperature;
}

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::WaitForCooldown()
{
  while(GetTemperatureF(&this->CurrentTemperature) != DRV_TEMPERATURE_STABILIZED)
  {
    igtl::Sleep(1000); // wait a bit
  }
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireFrame()
{
  unsigned long frameSize = xSize * ySize;
  rawFrame.resize(frameSize, 0);

  unsigned result = StartAcquisition();
  AndorCheckErrorValueAndFailIfNeeded(result, "StartAcquisition")
  result = WaitForAcquisition();
  AndorCheckErrorValueAndFailIfNeeded(result, "WaitForAcquisition")

  // iKon-M 934 has 16-bit digitization
  // https://andor.oxinst.com/assets/uploads/products/andor/documents/andor-ikon-m-934-specifications.pdf
  // so we choose 16-bit unsigned
  // GetMostRecentImage() is 32 bit signed variant
  result = GetMostRecentImage16(&rawFrame[0], frameSize);
  AndorCheckErrorValueAndFailIfNeeded(result, "GetMostRecentImage16")

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireBLIFrame()
{
  WaitForCooldown();
  AcquireFrame();

  // add it to the data source
  // and if OpenCV is available to rectified data source

  return PLUS_SUCCESS;
}


// Setup the Andor camera parameters ----------------------------------------------

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetShutter(int shutter)
{
  this->Shutter = shutter;
  unsigned int result = ::SetShutter(1, this->Shutter, 0, 0);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetShutter")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetShutter()
{
  return this->Shutter;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetExposureTime(float exposureTime)
{
  this->ExposureTime = exposureTime;

  unsigned int result = ::SetExposureTime(this->ExposureTime);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetExposureTime")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
float vtkPlusAndorCamera::GetExposureTime()
{
  return this->ExposureTime;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetPreAmpGain(int preAmpGain)
{
  this->PreAmpGain = preAmpGain;

  unsigned int result = ::SetPreAmpGain(this->PreAmpGain);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetPreAmpGain")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetPreAmpGain()
{
  return this->PreAmpGain;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAcquisitionMode(int acquisitionMode)
{
  this->AcquisitionMode = acquisitionMode;

  unsigned int result = ::SetAcquisitionMode(this->AcquisitionMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetAcquisitionMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAcquisitionMode()
{
  return this->AcquisitionMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetReadMode(int readMode)
{
  this->ReadMode = readMode;

  unsigned int result = ::SetReadMode(this->ReadMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetReadMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetReadMode()
{
  return this->ReadMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetTriggerMode(int triggerMode)
{
  this->TriggerMode = triggerMode;

  unsigned int result = ::SetTriggerMode(this->TriggerMode);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetTriggerMode")
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetTriggerMode()
{
  return this->TriggerMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetCoolTemperature(int coolTemp)
{
  this->CoolTemperature = coolTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetCoolTemperature()
{
  return this->CoolTemperature;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetSafeTemperature(int safeTemp)
{
  this->SafeTemperature = safeTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetSafeTemperature()
{
  return this->SafeTemperature;
}
