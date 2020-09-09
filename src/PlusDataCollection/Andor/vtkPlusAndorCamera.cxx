/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "vtkImageData.h"
#include "vtkPlusDataSource.h"
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

  unsigned epromVer;
  unsigned cofVer;
  unsigned driverRev;
  unsigned driverVer;
  unsigned dllRev;
  unsigned dllVer;

  unsigned andorResult = GetSoftwareVersion(&epromVer, &cofVer, &driverRev, &driverVer, &dllRev, &dllVer);

  versionString << "Andor SDK version: "  << dllVer << "." << dllRev << std::endl;
  return versionString.str();
}


//----------------------------------------------------------------------------
vtkPlusAndorCamera::vtkPlusAndorCamera()
{
  this->RequirePortNameInDeviceSetConfiguration = true;

  // We will acquire the frames sporadically,
  // and usually with long exposure times.
  // We don't want polling to interfere with it.
  this->StartThreadForInternalUpdates = true; //debugging
  this->AcquisitionRate = 1.0;

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

  int x, y;
  result = GetDetector(&x, &y);
  AndorCheckErrorValueAndFailIfNeeded(result, "GetDetectorSize")
  frameSize[0] = static_cast<unsigned>(x);
  frameSize[1] = static_cast<unsigned>(y);

  // binning of 1 (meaning no binning), and full sensor size
  result = SetImage(1, 1, 1, x, 1, y);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetImage")

  result = PrepareAcquisition();
  AndorCheckErrorValueAndFailIfNeeded(result, "PrepareAcquisition")

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::InitializePort(std::vector<vtkPlusDataSource*>& port)
{
  for(unsigned i = 0; i < port.size(); i++)
  {
    port[i]->SetPixelType(VTK_UNSIGNED_SHORT);
    port[i]->SetImageType(US_IMG_BRIGHTNESS);
    port[i]->SetOutputImageOrientation(US_IMG_ORIENT_MF);
    port[i]->SetInputImageOrientation(US_IMG_ORIENT_MF);
    port[i]->SetInputFrameSize(frameSize);

    LOG_INFO("Andor source initialized. ID: " << port[i]->GetId());
  }
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

  if(BLIraw.size() + BLIrectified.size() + GrayRaw.size() + GrayRectified.size() == 0)
  {
    vtkPlusDataSource* aSource = nullptr;
    if(this->GetFirstActiveOutputVideoSource(aSource) != PLUS_SUCCESS || aSource == nullptr)
    {
      LOG_ERROR("Standard data sources are not defined, and unable to retrieve the video source in the capturing device.");
      return PLUS_FAIL;
    }
    BLIraw.push_back(aSource); // this is the default port
  }

  this->InitializePort(BLIraw);
  this->InitializePort(BLIrectified);
  this->InitializePort(GrayRaw);
  this->InitializePort(GrayRectified);

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
      LOG_INFO("Temperature not yet at a safe point, turning the Cooler Off");
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
    igtl::Sleep(5000); // wait a bit
    GetCurrentTemperature(); // logs the status and temperature
  }
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireFrame(float exposure)
{
  unsigned rawFrameSize = frameSize[0] * frameSize[1];
  rawFrame.resize(rawFrameSize, 0);

  unsigned result = ::SetExposureTime(exposure);
  AndorCheckErrorValueAndFailIfNeeded(result, "SetExposureTime")
  result = StartAcquisition();
  AndorCheckErrorValueAndFailIfNeeded(result, "StartAcquisition")
  result = WaitForAcquisition();
  this->currentTime = vtkIGSIOAccurateTimer::GetSystemTime();
  AndorCheckErrorValueAndFailIfNeeded(result, "WaitForAcquisition")

  // iKon-M 934 has 16-bit digitization
  // https://andor.oxinst.com/assets/uploads/products/andor/documents/andor-ikon-m-934-specifications.pdf
  // so we choose 16-bit unsigned
  // GetMostRecentImage() is 32 bit signed variant
  result = GetMostRecentImage16(&rawFrame[0], rawFrameSize);
  AndorCheckErrorValueAndFailIfNeeded(result, "GetMostRecentImage16")

  ++this->FrameNumber;
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireBLIFrame()
{
  //WaitForCooldown();
  AcquireFrame(this->ExposureTime);

  // add it to the data source
  for(unsigned i = 0; i < BLIraw.size(); i++)

  {
    if(BLIraw[i]->AddItem(&rawFrame[0],
                          US_IMG_ORIENT_MF,
                          frameSize, VTK_UNSIGNED_SHORT,
                          1, US_IMG_BRIGHTNESS, 0,
                          this->FrameNumber,
                          currentTime,
                          UNDEFINED_TIMESTAMP,
                          nullptr) != PLUS_SUCCESS)
    {
      LOG_WARNING("Error adding item to AndorCamera video source " << BLIraw[i]->GetSourceId());
    }
    else
    {
      LOG_INFO("Success adding item to AndorCamera video source " << BLIraw[i]->GetSourceId());
    }
  }

  // and if OpenCV is available to rectified data source

  return PLUS_SUCCESS;
}


// Setup the Andor camera parameters ----------------------------------------------

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetShutter(int shutter)
{
  this->Shutter = shutter;
  unsigned result = ::SetShutter(1, this->Shutter, 0, 0);
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

  unsigned result = ::SetExposureTime(this->ExposureTime);
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

  unsigned result = ::SetPreAmpGain(this->PreAmpGain);
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

  unsigned result = ::SetAcquisitionMode(this->AcquisitionMode);
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

  unsigned result = ::SetReadMode(this->ReadMode);
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

  unsigned result = ::SetTriggerMode(this->TriggerMode);
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
