#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>

#ifndef HID_REPORT_DESCRIPTOR_TYPE
#define HID_REPORT_DESCRIPTOR_TYPE 0x22
#endif

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL EvtIoInternalDeviceControl;
EVT_WDF_REQUEST_COMPLETION_ROUTINE DescriptorCompletion;

static const UCHAR kBadPrefix[8] = { 0x06, 0x00, 0xFF, 0x09, 0x1A, 0xA1, 0x01, 0xC0 };
static const UCHAR kGoodPrefix[8] = { 0x06, 0x00, 0xFF, 0x06, 0x00, 0xFF, 0x05, 0x20 };

static VOID
PatchReportDescriptor(_Inout_updates_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length)
{
  if (Buffer == NULL || Length < sizeof(kBadPrefix)) {
    return;
  }

  if (RtlCompareMemory(Buffer, kBadPrefix, sizeof(kBadPrefix)) == sizeof(kBadPrefix)) {
    RtlCopyMemory(Buffer, kGoodPrefix, sizeof(kGoodPrefix));
    DbgPrint("Apple Studio Display XDR Ambient Light USB Sensor Fix: report descriptor patched (%lu bytes total)\n", Length);
  } else {
    DbgPrint("Apple Studio Display XDR Ambient Light USB Sensor Fix: descriptor prefix did not match; left unmodified\n");
  }
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
  WDF_DRIVER_CONFIG config;

  WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);

  return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS
EvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
  NTSTATUS status;
  WDFDEVICE device;
  WDF_IO_QUEUE_CONFIG queueConfig;

  UNREFERENCED_PARAMETER(Driver);

  WdfFdoInitSetFilter(DeviceInit);

  status = WdfDeviceCreate(&DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
  queueConfig.EvtIoInternalDeviceControl = EvtIoInternalDeviceControl;

  return WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

VOID
EvtIoInternalDeviceControl(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t OutputBufferLength, _In_ size_t InputBufferLength, _In_ ULONG IoControlCode)
{
  WDFDEVICE device = WdfIoQueueGetDevice(Queue);
  WDF_REQUEST_SEND_OPTIONS options;
  WDF_REQUEST_PARAMETERS params;
  BOOLEAN isReportDescriptor = FALSE;
  NTSTATUS status;

  UNREFERENCED_PARAMETER(OutputBufferLength);
  UNREFERENCED_PARAMETER(InputBufferLength);

  if (IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB) {

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(Request, &params);

    {
      PURB urb = (PURB)params.Parameters.Others.Arg1;

      if (urb != NULL &&
          urb->UrbHeader.Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE &&
          urb->UrbControlDescriptorRequest.DescriptorType == HID_REPORT_DESCRIPTOR_TYPE) {
        isReportDescriptor = TRUE;
      }
    }
  }

  if (isReportDescriptor) {
    WdfRequestFormatRequestUsingCurrentType(Request);
    WdfRequestSetCompletionRoutine(Request, DescriptorCompletion, NULL);

    if (!WdfRequestSend(Request, WdfDeviceGetIoTarget(device), WDF_NO_SEND_OPTIONS)) {
      status = WdfRequestGetStatus(Request);
      DbgPrint("Apple Studio Display XDR Ambient Light USB Sensor Fix: WdfRequestSend failed 0x%x\n", status);
      WdfRequestComplete(Request, status);
    }
    return;
  }

  WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

  if (!WdfRequestSend(Request, WdfDeviceGetIoTarget(device), &options)) {
    status = WdfRequestGetStatus(Request);
    WdfRequestComplete(Request, status);
  }
}

VOID
DescriptorCompletion(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target, _In_ PWDF_REQUEST_COMPLETION_PARAMS CompletionParams, _In_ WDFCONTEXT Context)
{
  NTSTATUS status = CompletionParams->IoStatus.Status;
  WDF_REQUEST_PARAMETERS params;

  UNREFERENCED_PARAMETER(Target);
  UNREFERENCED_PARAMETER(Context);

  if (NT_SUCCESS(status)) {

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(Request, &params);

    {
      PURB urb = (PURB)params.Parameters.Others.Arg1;

      if (urb != NULL) {
        struct _URB_CONTROL_DESCRIPTOR_REQUEST *desc = &urb->UrbControlDescriptorRequest;
        PUCHAR buffer = NULL;
        ULONG length = desc->TransferBufferLength;

        if (desc->TransferBuffer != NULL) {
          buffer = (PUCHAR)desc->TransferBuffer;
        } else if (desc->TransferBufferMDL != NULL) {
          buffer = (PUCHAR)MmGetSystemAddressForMdlSafe(desc->TransferBufferMDL, NormalPagePriority | MdlMappingNoExecute);
        }

        PatchReportDescriptor(buffer, length);
      }
    }
  }

  WdfRequestComplete(Request, status);
}
