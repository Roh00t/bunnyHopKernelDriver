#include<ntifs.h>
#include <ntddk.h>

// These are functions that are undocumented, hence needs to be foward declared:
extern "C" {
	NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);

	NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress,
		PEPROCESS TargetProcess, PVOID TargetAddress, SIZE_T BufferSize,
		KPROCESSOR_MODE PreviousMode, PSIZE_T ReturnSize);
}



// Default kernal driver program setup:
void debug_print(PCTSTR text){
#ifndef DEBUG
	UNREFERENCED_PARAMETER(text);
#endif // !DEBUG
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, text));

}

namespace driver {
	//Holds the input output control codes:
	namespace codes {
		// Used to set up driver
		constexpr ULONG attach =
			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
		// Read p rocess memory
		constexpr ULONG read =
			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
		//Write process memory
		constexpr ULONG write =
			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

	}
	// Shared between usermode and Kernel Mode
	struct Request
	{ 
		HANDLE process_id;

		PVOID target;
		PVOID buffer;

		SIZE_T size;
		SIZE_T return_size;
	};

	NTSTATUS close(PDEVICE_OBJECT device_object, PIRP irp) 
	{
		UNREFERENCED_PARAMETER(device_object);
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return irp->IoStatus.Status;
	}

	//Note: ToDo:
	NTSTATUS device_control(PDEVICE_OBJECT device_object, PIRP irp) 
	{
		UNREFERENCED_PARAMETER(device_object);
		debug_print("[+] Device Control called.\n");

			NTSTATUS status = STATUS_UNSUCCESSFUL;

		//We need this to determine which code was passed through:
		PIO_STACK_LOCATION stack_irp = IoGetCurrentIrpStackLocation(irp);

		// Access the request object sent from user mode
		auto request = reinterpret_cast<Request*>(irp->AssociatedIrp.SystemBuffer);

		// To ensure that the methods are not null, to prevent BSOD:
		if (stack_irp == nullptr || request == nullptr)
		{
			IoCompleteRequest(irp, IO_NO_INCREMENT);

			return status;
		}

		// The taraget process we want to acesss to:
		static PEPROCESS target_process = nullptr;

		const ULONG control_code = stack_irp->Parameters.DeviceIoControl.IoControlCode;
		switch (control_code)
		{
		case codes::attach:
			status = PsLookupProcessByProcessId(request->process_id, &target_process);
			break;

		case codes::read:
			if (target_process != nullptr)
				status = MmCopyVirtualMemory(target_process, request->target, PsGetCurrentProcess(), request->buffer,request->size, KernelMode, &request->return_size);
			break;
		case codes::write:
			if (target_process != nullptr)
				status = MmCopyVirtualMemory(PsGetCurrentProcess(), request->buffer, target_process, request->target, request->size, KernelMode, &request->return_size);
			break;
		default:
			break;
		}
		irp->IoStatus.Status = status;
		irp->IoStatus.Information = sizeof(Request);
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	}

	
	NTSTATUS create(PDEVICE_OBJECT device_object, PIRP irp) 
	{
		UNREFERENCED_PARAMETER(device_object);
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return irp->IoStatus.Status;
	}


}
NTSTATUS driver_main(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path)
{
	UNREFERENCED_PARAMETER(registry_path);

	//Creating the driver device:
	UNICODE_STRING device_name = {};
	RtlInitUnicodeString(&device_name, L"\\Driver\\bunnyHopKernelDriver");

	//Create driver device obj:
	PDEVICE_OBJECT device_object = nullptr;
	NTSTATUS status =  IoCreateDevice(driver_object, 0 , &device_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &device_object);

	if (status != STATUS_SUCCESS)
	{
		debug_print("[-] Failed to create driver device.\n");
		return status;
	}
	debug_print("[+] Driver Device Built Successfully.\n");
	UNICODE_STRING symbolic_link = {};
	RtlInitUnicodeString(&symbolic_link, L"\\DosDevices\\bunnyHopKernelDriver");

	status = IoCreateSymbolicLink(&symbolic_link, &device_name);

	if (status != STATUS_SUCCESS)
	{
		debug_print("[-] Failed to establish symbolic link.\n");
		return status;
	}
	debug_print("[+] Driver Symbolic link created successfully. \n");

	// Allow us to send small amount of data between userMode and Kernel Mode:
	SetFlag(device_object->Flags, DO_BUFFERED_IO);

	// Set the driver handlers to our function with our logic
	driver_object->MajorFunction[IRP_MJ_CREATE] = driver::create;
	driver_object->MajorFunction[IRP_MJ_CLOSE] = driver::close;
	driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver::device_control;

	ClearFlag(device_object->Flags, DO_DEVICE_INITIALIZING);
	debug_print("[+] Drivers initiatized successfully .\n");
	return status;
}



// kdMapper will call this "EntryPoint" but params will be null
NTSTATUS DriverEntry() {
	debug_print("[+] Debug from the kernel\n");

	//Setting up driver:
	UNICODE_STRING driver_name = {};
	RtlInitUnicodeString(&driver_name, L"\\Driver\\bunnyHopKernelDriver");

	return IoCreateDriver(&driver_name, &driver_main);
}