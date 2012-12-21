extern "C"
{
	#include "ntddk.h"
}

#include "ntddkbd.h"

typedef struct _DEVICE_EXTENSION{
	PDEVICE_OBJECT pLowerDO;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

int gnRequests;

NTSTATUS DispatchThru(PDEVICE_OBJECT theDeviceObject, PIRP theIrp)
{
	IoSkipCurrentIrpStackLocation(theIrp);
	return IoCallDriver(((PDEVICE_EXTENSION) theDeviceObject->DeviceExtension)->pLowerDO ,theIrp);
}
NTSTATUS InstallFilter(IN PDRIVER_OBJECT theDO)
{
	PDEVICE_OBJECT pKeyboardDevice;
	NTSTATUS status = {0};

	status = IoCreateDevice(theDO, sizeof(DEVICE_EXTENSION), NULL, FILE_DEVICE_KEYBOARD, 0, FALSE, &pKeyboardDevice);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("IoCreateDevice error..");
		return status;
	}
	pKeyboardDevice->Flags = pKeyboardDevice->Flags | (DO_BUFFERED_IO | DO_POWER_PAGABLE);
	pKeyboardDevice->Flags = pKeyboardDevice->Flags & ~DO_DEVICE_INITIALIZING;

	RtlZeroMemory(pKeyboardDevice->DeviceExtension, sizeof(DEVICE_EXTENSION));

	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)pKeyboardDevice->DeviceExtension;

	CCHAR cName[40] = "\\Device\\KeyboardClass0";
	STRING strName;
	UNICODE_STRING ustrDeviceName;

	RtlInitAnsiString(&strName, cName);
	RtlAnsiStringToUnicodeString(&ustrDeviceName, &strName, TRUE);

	IoAttachDevice(pKeyboardDevice, &ustrDeviceName, &pdx->pLowerDO);
	//DbgPrint("After IoAttachDevice");
	RtlFreeUnicodeString(&ustrDeviceName);
	
	return status;
}
VOID DriverUnload(IN PDRIVER_OBJECT theDO)
{
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)theDO->DeviceObject->DeviceExtension;
	IoDetachDevice(pdx->pLowerDO);
	IoDeleteDevice(theDO->DeviceObject);
	if (gnRequests != 0)
	{
		DbgPrint("There are some not complited requests %d", gnRequests);
		KTIMER ktTimer;
		LARGE_INTEGER liTimeout;
		liTimeout.QuadPart = 1000000;
		KeInitializeTimer(&ktTimer);
		
		while(gnRequests > 0)
		{
			KeSetTimer(&ktTimer, liTimeout, NULL);
			KeWaitForSingleObject(&ktTimer, Executive, KernelMode, FALSE, NULL);
		}
	}
	DbgPrint("Im going to be dead...");
}
NTSTATUS ReadCompletionRoutine(IN PDEVICE_OBJECT pDeviceObject, IN PIRP theIrp, IN PVOID Context)
{
	//DbgPrint("ReadComplitionRoutine!");
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	PKEYBOARD_INPUT_DATA kidData;
	if (NT_SUCCESS(theIrp->IoStatus.Status))
	{
		kidData = (PKEYBOARD_INPUT_DATA)theIrp->AssociatedIrp.SystemBuffer;
		int n = theIrp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA);
		for(int i = 0; i<n; ++i)
		{
			DbgPrint("Code: %x\n", kidData[i].MakeCode);
		}
	}
	if(theIrp->PendingReturned)
		IoMarkIrpPending(theIrp);
	__asm{
		lock dec gnRequests
	}
	return theIrp->IoStatus.Status;
}
NTSTATUS DispatchRead(IN PDEVICE_OBJECT pDeviceObject, IN PIRP theIrp)
{
	//DbgPrint("OS asked for read operation!");
	__asm{
		lock inc gnRequests
	}
	IoCopyCurrentIrpStackLocationToNext(theIrp);
	IoSetCompletionRoutine(theIrp, ReadCompletionRoutine, pDeviceObject, TRUE, TRUE, TRUE);
	return IoCallDriver(((PDEVICE_EXTENSION) pDeviceObject->DeviceExtension)->pLowerDO ,theIrp);
}
extern "C" NTSTATUS DriverEntry(IN PDRIVER_OBJECT theDriverObject, IN PUNICODE_STRING RegistryPath)
{
	NTSTATUS status = {0};
	gnRequests = 0;
	//DbgPrint("In DriverEntry!");
	for (int i = 0; i<IRP_MJ_MAXIMUM_FUNCTION; ++i)
	{
		theDriverObject->MajorFunction[i] = DispatchThru;
	}
	theDriverObject->MajorFunction[IRP_MJ_READ] = DispatchRead;
	
	status = InstallFilter(theDriverObject);
	
	theDriverObject->DriverUnload = DriverUnload;
	//DbgPrint("DriverEntry end!");
	return status;
}
