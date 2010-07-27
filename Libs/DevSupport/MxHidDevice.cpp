#include "stdafx.h"
#include <Assert.h>
#include <cfgmgr32.h>
#include <basetyps.h>
#include <setupapi.h>
#include <initguid.h>
extern "C" {
#include "Libs/WDK/hidsdi.h"
}
#include "Libs/WDK/hidclass.h"
#include "Device.h"
#include "MxHidDevice.h"

#define DEVICE_TIMEOUT			INFINITE // 5000 ms
#define DEVICE_READ_TIMEOUT   10

//The WriteFileEx function is designed solely for asynchronous operation.
//The Write Function is designed solely for synchronous operation.
#define ASYNC_READ_WRITE 0

HANDLE MxHidDevice::m_sync_event_tx = NULL;
HANDLE MxHidDevice::m_sync_event_rx = NULL;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
MxHidDevice::MxHidDevice(DeviceClass * deviceClass, DEVINST devInst, CStdString path)
: Device(deviceClass, devInst, path)
{
    m_hid_drive_handle = INVALID_HANDLE_VALUE;
    m_sync_event_tx = NULL;
    m_sync_event_rx = NULL;
    m_pReadReport = NULL;
    m_pWriteReport = NULL;
    _chipFamily = MX508;
}

MxHidDevice::~MxHidDevice()
{
}

void MxHidDevice::FreeIoBuffers()
{
    if( m_pReadReport )
    {
        free( m_pReadReport );
        m_pReadReport = NULL;
    }

    if( m_pWriteReport )
    {
        free( m_pWriteReport );
        m_pWriteReport = NULL;
    }

}

// Modiifes m_Capabilities member variable
// Modiifes m_pReadReport member variable
// Modiifes m_pWriteReport member variable
int32_t MxHidDevice::AllocateIoBuffers()
{
    // Open the device
    HANDLE hHidDevice = CreateFile(_path.get(), 0, 0, NULL, OPEN_EXISTING, 0, NULL);

    if( hHidDevice == INVALID_HANDLE_VALUE )
    {
		int32_t error = GetLastError();
        ATLTRACE2(_T(" MxHidDevice::AllocateIoBuffers().CreateFile ERROR:(%d)\r\n"), error);
        return error;
    }

    // Get the Capabilities including the max size of the report buffers
    PHIDP_PREPARSED_DATA  PreparsedData = NULL;
    if ( !HidD_GetPreparsedData(hHidDevice, &PreparsedData) )
    {
        CloseHandle(hHidDevice);
        ATLTRACE2(_T(" MxHidDevice::AllocateIoBuffers().GetPreparsedData ERROR:(%d)\r\n"), ERROR_GEN_FAILURE);
        return ERROR_GEN_FAILURE;
    }

    NTSTATUS sts = HidP_GetCaps(PreparsedData, &m_Capabilities);
	if( sts != HIDP_STATUS_SUCCESS )
    {
        CloseHandle(hHidDevice);
        HidD_FreePreparsedData(PreparsedData);
        ATLTRACE2(_T(" MxHidDevice::AllocateIoBuffers().GetCaps ERROR:(%d)\r\n"), HIDP_STATUS_INVALID_PREPARSED_DATA);
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    }

    CloseHandle(hHidDevice);
    HidD_FreePreparsedData(PreparsedData);

    // Allocate a Read and Write Report buffers
    FreeIoBuffers();

    if ( m_Capabilities.InputReportByteLength )
    {
        m_pReadReport = (_MX_HID_DATA_REPORT*)malloc(m_Capabilities.InputReportByteLength);
        if ( m_pReadReport == NULL )
		{
	        ATLTRACE2(_T(" MxHidDevice::AllocateIoBuffers(). Failed to allocate memory (1)\r\n"));
            return ERROR_NOT_ENOUGH_MEMORY;
		}
    }

    if ( m_Capabilities.OutputReportByteLength )
    {
        m_pWriteReport = (_MX_HID_DATA_REPORT*)malloc(m_Capabilities.OutputReportByteLength);
        if ( m_pWriteReport == NULL )
		{
	        ATLTRACE2(_T(" MxHidDevice::AllocateIoBuffers(). Failed to allocate memory (2)\r\n"));
            return ERROR_NOT_ENOUGH_MEMORY;
		}
    }

    return ERROR_SUCCESS;
}

BOOL MxHidDevice::OpenUSBHandle(HANDLE* pHandle, CStdString pipePath)
{
    #if ASYNC_READ_WRITE
	*pHandle = CreateFile(pipePath,
		GENERIC_WRITE | GENERIC_READ,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		NULL);
    #else
    *pHandle = CreateFile(pipePath,
		GENERIC_WRITE | GENERIC_READ,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
    #endif

	if (*pHandle == INVALID_HANDLE_VALUE) {
		TRACE(_T("MxHidDevice::OpenUSBHandle() Failed to open (%s) = %d"), pipePath, GetLastError());
		return FALSE;
	}

	return TRUE;
}


BOOL MxHidDevice::OpenMxHidHandle()
{
    int err = ERROR_SUCCESS;

    #if ASYNC_READ_WRITE
    // create TX and RX events
    m_sync_event_tx = ::CreateEvent( NULL, TRUE, FALSE, NULL );
    if( !m_sync_event_tx )
    {
        assert(false);
        TRACE((__FUNCTION__ " ERROR: CreateEvent failed.ErrCode 0x%x(%d)\n"),GetLastError(),GetLastError());
        return FALSE;
    }
    m_sync_event_rx = ::CreateEvent( NULL, TRUE, FALSE, NULL );
    if( !m_sync_event_rx )
    {
        assert(false);
        TRACE((__FUNCTION__ " ERROR: CreateEvent failed.ErrCode 0x%x(%d)\n"),GetLastError(),GetLastError());
        return FALSE;
    }
    #endif

    memset(&m_Capabilities, 0, sizeof(m_Capabilities));

    err = AllocateIoBuffers();
	if ( err != ERROR_SUCCESS )
	{
		TRACE((__FUNCTION__ " ERROR: AllocateIoBuffers failed. %d\n"),err);
		return FALSE;
	}

    // Open the device 
    if (!OpenUSBHandle(&m_hid_drive_handle,_path.get()))
    {
        TRACE(__FUNCTION__ " ERROR: OpenUSBHandle failed.\n");
		return FALSE;
    }
    
    return TRUE;
}

BOOL MxHidDevice::CloseMxHidHandle()
{
    #if ASYNC_READ_WRITE
    if( m_sync_event_tx != NULL )
    {
        CloseHandle(m_sync_event_tx);
        m_sync_event_tx = NULL;
    }
    if( m_sync_event_rx != NULL )
    {
        CloseHandle(m_sync_event_rx);
        m_sync_event_rx = NULL;
    }
    #endif

    if( m_hid_drive_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hid_drive_handle);
        m_hid_drive_handle = INVALID_HANDLE_VALUE;
    }

    FreeIoBuffers();
        
    return TRUE;
}


#if ASYNC_READ_WRITE
// Write to HID device
int MxHidDevice::Write(UCHAR * _buf,ULONG _size)
{
    DWORD status;

    // Preparation
    m_overlapped.Offset				= 0;
    m_overlapped.OffsetHigh			= 0;
    m_overlapped.hEvent				= (PVOID)(ULONGLONG)_size;

    ::ResetEvent( MxHidDevice::m_sync_event_tx );

    // Write to the device
    if( !::WriteFileEx( m_hid_drive_handle, _buf, _size, &m_overlapped, 
        MxHidDevice::WriteCompletionRoutine ) )
    {
        TRACE(_T("MxHidDevice::Write()fail. Error writing to device 0x%x(%d).\r\n"), GetLastError(), GetLastError());
        return ::GetLastError();
    }

    // wait for completion
    if( (status = ::WaitForSingleObjectEx( m_sync_event_tx, INFINITE, TRUE )) == WAIT_TIMEOUT )
    {
        TRACE(_T("MxHidDevice::Write()fail. WaitForSingleObjectEx TimeOut.\r\n"));
        ::CancelIo( m_hid_drive_handle );
        return WAIT_TIMEOUT;
    }
	
	if( m_overlapped.Offset == 0 )
	{
        TRACE(_T("MxHidDevice::Write() fail.m_overlapped.Offset is 0.\r\n"));
        return -13 ;
	}
    else
        return ERROR_SUCCESS;
}

VOID MxHidDevice::WriteCompletionRoutine( 
    DWORD _err_code, 
    DWORD _bytes_transferred, 
    LPOVERLAPPED _lp_overlapped
    )
{
    if( ((ULONG)(ULONGLONG)_lp_overlapped->hEvent != _bytes_transferred) || _err_code )
    {
        *(BOOL *)&_lp_overlapped->Offset = 0;
    }
    else
    {
        *(BOOL *)&_lp_overlapped->Offset = _bytes_transferred;
    }

    ::SetEvent(MxHidDevice::m_sync_event_tx );
}

// Read from HID device
int MxHidDevice::Read(void* _buf, UINT _size)
{
    DWORD status;

    // Preparation
    m_overlapped.Offset				= 0;
    m_overlapped.OffsetHigh			= 0;
    m_overlapped.hEvent				= (PVOID)(ULONGLONG)_size;

    ::ResetEvent( MxHidDevice::m_sync_event_rx );

    //  The read command does not sleep very well right now.
    Sleep(35); 

    // Read from device
    if( !::ReadFileEx( m_hid_drive_handle, _buf, _size, &m_overlapped, 
        MxHidDevice::ReadCompletionRoutine ) )
    {
        TRACE(_T("MxHidDevice::Read()fail. Error reading from device 0x%x(%d).\r\n"), GetLastError(), GetLastError());
        return ::GetLastError();
    }

    // wait for completion
    if( (status = ::WaitForSingleObjectEx( m_sync_event_rx, DEVICE_READ_TIMEOUT, TRUE )) == WAIT_TIMEOUT )
    {
        TRACE(_T("MxHidDevice::Read()fail. WaitForSingleObjectEx TimeOut.\r\n"));
        ::CancelIo( m_hid_drive_handle );
        return WAIT_TIMEOUT;
    }

    if( m_overlapped.Offset == 0 )
    {
        TRACE(_T("MxHidDevice::Read()fail.m_overlapped.Offset is 0.\r\n"));
        return -13 /*STERR_FAILED_TO_WRITE_FILE_DATA*/;
    }
    else
        return ERROR_SUCCESS;
}

VOID MxHidDevice::ReadCompletionRoutine( 
        DWORD _err_code, 
        DWORD _bytes_transferred, 
        LPOVERLAPPED _lp_overlapped
        )
{
    if( ((ULONG)(ULONGLONG)_lp_overlapped->hEvent != _bytes_transferred) || _err_code )
    {
        *(BOOL *)&_lp_overlapped->Offset = 0;
    }
    else
    {
        *(BOOL *)&_lp_overlapped->Offset = _bytes_transferred;
    }

    if( m_sync_event_rx != NULL) {
        ::SetEvent( m_sync_event_rx );
    }
}

#else

int MxHidDevice::Write(UCHAR* _buf, ULONG _size)
{
	int    nBytesWrite; // for bytes actually written

	if( !WriteFile(m_hid_drive_handle, _buf, _size, (PULONG) &nBytesWrite, NULL) )
	{
		TRACE(_T("MxHidDevice::Write() Error writing to device 0x%x(%d).\r\n"), GetLastError(), GetLastError());
		return FALSE;
	}

    return ERROR_SUCCESS;
}

// Read from HID device
int MxHidDevice::Read(void* _buf, UINT _size)
{
	int    nBytesRead; // for bytes actually read

	Sleep(35);

	if( !ReadFile(m_hid_drive_handle, _buf, _size, (PULONG) &nBytesRead, NULL) )
	{
		TRACE(_T("MxHidDevice::Read() Error reading from device 0x%x(%d).\r\n"), GetLastError(), GetLastError());
		return GetLastError();
	}

	return ERROR_SUCCESS;
}

#endif

/// <summary>
//-------------------------------------------------------------------------------------		
// Function to 16 byte SDP command format, these 16 bytes will be sent by host to 
// device in SDP command field of report 1 data structure
//
// @return
//		a report packet to be sent.
//-------------------------------------------------------------------------------------
//
VOID MxHidDevice::PackSDPCmd(PSDPCmd pSDPCmd)
{
    memset((UCHAR *)m_pWriteReport, 0, m_Capabilities.OutputReportByteLength);
    m_pWriteReport->ReportId = (unsigned char)REPORT_ID_SDP_CMD;
    PLONG pTmpSDPCmd = (PLONG)(m_pWriteReport->Payload);

	pTmpSDPCmd[0] = (  ((pSDPCmd->address  & 0x00FF0000) << 8) 
		          | ((pSDPCmd->address  & 0xFF000000) >> 8) 
		          |  (pSDPCmd->command   & 0x0000FFFF) );

	pTmpSDPCmd[1] = (   (pSDPCmd->dataCount & 0xFF000000)
		          | ((pSDPCmd->format   & 0x000000FF) << 16)
		          | ((pSDPCmd->address  & 0x000000FF) <<  8)
		          | ((pSDPCmd->address  & 0x0000FF00) >>  8 ));

	pTmpSDPCmd[2] = (   (pSDPCmd->data     & 0xFF000000)
		          | ((pSDPCmd->dataCount & 0x000000FF) << 16)
		          |  (pSDPCmd->dataCount & 0x0000FF00)
		          | ((pSDPCmd->dataCount & 0x00FF0000) >> 16));

	pTmpSDPCmd[3] = (  ((0x00  & 0x000000FF) << 24)
		          | ((pSDPCmd->data     & 0x00FF0000) >> 16) 
		          |  (pSDPCmd->data     & 0x0000FF00)
		          | ((pSDPCmd->data     & 0x000000FF) << 16));   

}

BOOL MxHidDevice::GetCmdAck(UINT RequiredCmdAck)
{
    memset((UCHAR *)m_pReadReport, 0, m_Capabilities.InputReportByteLength);

    //Get Report3, Device to Host:
    //4 bytes HAB mode indicating Production/Development part
	if ( Read( (UCHAR *)m_pReadReport, m_Capabilities.InputReportByteLength )  != ERROR_SUCCESS)
	{
		return FALSE;
	}
	if ( (*(unsigned int *)(m_pReadReport->Payload) != HabEnabled)  && 
		 (*(unsigned int *)(m_pReadReport->Payload) != HabDisabled) ) 
	{
		return FALSE;	
	}

    memset((UCHAR *)m_pReadReport, 0, m_Capabilities.InputReportByteLength);

    //Get Report4, Device to Host:
	if ( Read( (UCHAR *)m_pReadReport, m_Capabilities.InputReportByteLength ) != ERROR_SUCCESS)
	{
		return FALSE;
	}

	if (*(unsigned int *)(m_pReadReport->Payload) != RequiredCmdAck)
	{
		TRACE("WriteReg(): Invalid write ack: 0x%x\n", ((PULONG)m_pReadReport)[0]);
		return FALSE; 
	}

    return TRUE;
}

BOOL MxHidDevice::WriteReg(PSDPCmd pSDPCmd)
{
    //First, pack the command to a report.
    PackSDPCmd(pSDPCmd);

	//Send the report to USB HID device
	if ( Write((unsigned char *)m_pWriteReport, m_Capabilities.OutputReportByteLength) != ERROR_SUCCESS)
	{
		return FALSE;
	}

	if ( !GetCmdAck(ROM_WRITE_ACK) )
	{
		return FALSE;
	}
    
    return TRUE;
}

BOOL MxHidDevice::ReadData(UINT address, UINT byteCount, unsigned char * pBuf)
{
    SDPCmd SDPCmd;

    SDPCmd.command = ROM_KERNEL_CMD_RD_MEM;
    SDPCmd.dataCount = byteCount;
    SDPCmd.format = 32;
    SDPCmd.data = 0;
    SDPCmd.address = address;

    //First, pack the command to a report.
    PackSDPCmd(&SDPCmd);

	//Send the report to USB HID device
	if ( Write((unsigned char *)m_pWriteReport, m_Capabilities.OutputReportByteLength)  != ERROR_SUCCESS)
	{
		return FALSE;
	}
    
    //It should be the fault of elvis_mcurom.elf which only returns report3
    memset((UCHAR *)m_pReadReport, 0, m_Capabilities.InputReportByteLength);
    //Get Report3, Device to Host:
    //4 bytes HAB mode indicating Production/Development part
	if ( Read( (UCHAR *)m_pReadReport, m_Capabilities.InputReportByteLength )  != ERROR_SUCCESS)
	{
		return FALSE;
	}
	if ( (*(unsigned int *)(m_pReadReport->Payload) != HabEnabled)  && 
		 (*(unsigned int *)(m_pReadReport->Payload) != HabDisabled) ) 
	{
		return FALSE;	
	}


    UINT MaxHidTransSize = m_Capabilities.InputReportByteLength -1;
    
    while(byteCount > 0)
    {
        UINT TransSize = (byteCount > MaxHidTransSize) ? MaxHidTransSize : byteCount;

        memset((UCHAR *)m_pReadReport, 0, m_Capabilities.InputReportByteLength);

        if ( Read( (UCHAR *)m_pReadReport, m_Capabilities.InputReportByteLength )  != ERROR_SUCCESS)
        {
            return FALSE;
        }

        memcpy(pBuf, m_pReadReport->Payload, TransSize);
        pBuf += TransSize;

        byteCount -= TransSize;
        //TRACE("Transfer Size: %d\n", TransSize);
    }

	return TRUE;
}

BOOL MxHidDevice::TransData(UINT address, UINT byteCount, const unsigned char * pBuf)
{
    SDPCmd SDPCmd;

    SDPCmd.command = ROM_KERNEL_CMD_WR_FILE;
    SDPCmd.dataCount = byteCount;
    SDPCmd.format = 0;
    SDPCmd.data = 0;
    SDPCmd.address = address;

    //First, pack the command to a report.
    PackSDPCmd(&SDPCmd);

	//Send the report to USB HID device
	if ( Write((unsigned char *)m_pWriteReport, m_Capabilities.OutputReportByteLength)  != ERROR_SUCCESS)
	{
		return FALSE;
	}

    m_pWriteReport->ReportId = REPORT_ID_DATA;
    UINT MaxHidTransSize = m_Capabilities.OutputReportByteLength -1;
    UINT TransSize;

    while(byteCount > 0)
    {
        TransSize = (byteCount > MaxHidTransSize) ? MaxHidTransSize : byteCount;

        memcpy(m_pWriteReport->Payload, pBuf, TransSize);

        if (Write((unsigned char *)m_pWriteReport, m_Capabilities.OutputReportByteLength) != ERROR_SUCCESS)
            return FALSE;
        byteCount -= TransSize;
        pBuf += TransSize;
        //TRACE("Transfer Size: %d\n", MaxHidTransSize);
    }
    
    //below function should be invoked for mx50
	if ( !GetCmdAck(ROM_STATUS_ACK) )
	{
		return FALSE;
	}

	return TRUE;
}

BOOL MxHidDevice::Jump(UINT RAMAddress)
{
    SDPCmd SDPCmd;

    SDPCmd.command = ROM_KERNEL_CMD_JUMP_ADDR;
    SDPCmd.dataCount = 0;
    SDPCmd.format = 0;
    SDPCmd.data = 0;
    SDPCmd.address = RAMAddress;

	//Send write Command to USB
    //First, pack the command to a report.
    PackSDPCmd(&SDPCmd);

	//Send the report to USB HID device
	if ( Write((unsigned char *)m_pWriteReport, m_Capabilities.OutputReportByteLength) != ERROR_SUCCESS )
	{
		return FALSE;
	}

    memset((UCHAR *)m_pReadReport, 0, m_Capabilities.InputReportByteLength);
    //Get Report3, Device to Host:
    //4 bytes HAB mode indicating Production/Development part
	if ( Read( (UCHAR *)m_pReadReport, m_Capabilities.InputReportByteLength )  != ERROR_SUCCESS)
	{
		return FALSE;
	}
	if ( (*(unsigned int *)(m_pReadReport->Payload) != HabEnabled)  && 
		 (*(unsigned int *)(m_pReadReport->Payload) != HabDisabled) ) 
	{
		return FALSE;	
	}

	TRACE("*********Jump to Ramkernel successfully!**********\r\n");
	return TRUE;
}


BOOL MxHidDevice::Jump()
{
    //Create device handle and report id
    OpenMxHidHandle();
    
    if(!Jump(m_jumpAddr))
    {
        //Clear device handle and report id
        CloseMxHidHandle();
        return FALSE;
    }

    //Clear device handle and report id
    CloseMxHidHandle();

    return TRUE;
}

BOOL MxHidDevice::Download(PImageParameter pImageParameter,StFwComponent *fwComponent, Device::UI_Callback callbackFn)
{
    //Create device handle and report id
    OpenMxHidHandle();
    
	UCHAR* pBuffer = (UCHAR*)fwComponent->GetDataPtr();
    ULONGLONG dataCount = fwComponent->size();
    
	DWORD byteIndex, numBytesToWrite = 0;
	for ( byteIndex = 0; byteIndex < dataCount; byteIndex += numBytesToWrite )
	{
		// Get some data
		numBytesToWrite = min(MAX_SIZE_PER_DOWNLOAD_COMMAND, dataCount - byteIndex);

		if (!TransData(pImageParameter->PhyRAMAddr4KRL + byteIndex, numBytesToWrite, pBuffer + byteIndex))
		{
			TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X, 0x%X, 0x%X) failed.\n"), \
                pImageParameter->PhyRAMAddr4KRL + byteIndex, numBytesToWrite, pImageParameter->loadSection, pBuffer + byteIndex);
			goto ERR_HANDLE;
		}
	}

	// If we are downloading to DCD or CSF, we don't need to send 
	if ( pImageParameter->loadSection == MemSectionDCD || pImageParameter->loadSection == MemSectionCSF )
	{
		return TRUE;
	}

    if( pImageParameter->setSection == MemSectionAPP)
    {
    	UINT FlashHdrAddr;
    	const unsigned char * pHeaderData = NULL;

    	//transfer length of ROM_TRANSFER_SIZE is a must to ROM code.
    	unsigned char FlashHdr[ROM_TRANSFER_SIZE] = { 0 };
    	unsigned char Tempbuf[ROM_TRANSFER_SIZE] = { 0 };	
    	
    	// Otherwise, create a header and append the data
    	if(_chipFamily == MX508)
    	{
    		PIvtHeader pIvtHeader = (PIvtHeader)FlashHdr;

    		FlashHdrAddr = pImageParameter->PhyRAMAddr4KRL + pImageParameter->CodeOffset - sizeof(IvtHeader);

    		//Copy image data with an offset of ivt header size to the temp buffer.
    		memcpy(FlashHdr + sizeof(IvtHeader), pBuffer+pImageParameter->CodeOffset, ROM_TRANSFER_SIZE - sizeof(IvtHeader));
    		
    		pIvtHeader->IvtBarker = IVT_BARKER_HEADER;
    		pIvtHeader->ImageStartAddr = FlashHdrAddr+sizeof(IvtHeader);
    		pIvtHeader->SelfAddr = FlashHdrAddr;
    	}
    	else
    	{
    		PFlashHeader pFlashHeader = (PFlashHeader)FlashHdr;
    		//Copy image data with an offset of flash header size to the temp buffer.
    		memcpy(FlashHdr + sizeof(FlashHeader), pBuffer, ROM_TRANSFER_SIZE - sizeof(FlashHeader));
    		
    		//We should write actual image address to the first dword of flash header.
    		pFlashHeader->ImageStartAddr = pImageParameter->PhyRAMAddr4KRL;

    		FlashHdrAddr = pImageParameter->PhyRAMAddr4KRL - sizeof(FlashHeader);
    	}
    	pHeaderData = (const unsigned char *)FlashHdr;

    	if ( !TransData(FlashHdrAddr, ROM_TRANSFER_SIZE, pHeaderData) )
    	{
    		TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X, 0x%X, 0x%X) failed.\n"), \
                FlashHdrAddr, ROM_TRANSFER_SIZE, pImageParameter->setSection, pHeaderData);
    		goto ERR_HANDLE;
    	}
        
        //Verify the data
    	if ( !ReadData(FlashHdrAddr, ROM_TRANSFER_SIZE, Tempbuf) )
    	{
    		TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X, 0x%X, 0x%X) failed.\n"), \
                FlashHdrAddr, ROM_TRANSFER_SIZE, pImageParameter->setSection, pHeaderData);
    		goto ERR_HANDLE;
    	}

        if(memcmp(pHeaderData, Tempbuf, ROM_TRANSFER_SIZE)!= 0 )
    	{
    		TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X, 0x%X, 0x%X) failed.\n"), \
                FlashHdrAddr, ROM_TRANSFER_SIZE, pImageParameter->setSection, pHeaderData);
    		goto ERR_HANDLE;
    	}

        /*if( !Jump(FlashHdrAddr))
        	{
                TRACE(_T("DownloadImage(): Failed to jump to RAM address: 0x%x.\n"), FlashHdrAddr);
        		goto ERR_HANDLE;
        	}*/
        // Set Jump Addr
        m_jumpAddr = FlashHdrAddr;
    }

    //Clear device handle and report id
    CloseMxHidHandle();

    return TRUE;

ERR_HANDLE:
    //Clear device handle and report id
    CloseMxHidHandle();
    
	return FALSE;
}

BOOL MxHidDevice::InitMemoryDevice(CString filename)
{
	USES_CONVERSION;
	SDPCmd SDPCmd;

    //Create device handle and report id
    OpenMxHidHandle();
    
    SDPCmd.command = ROM_KERNEL_CMD_WR_MEM;
    SDPCmd.dataCount = 4;

	CFile scriptFile;
	CFileException fileException;
	if( !scriptFile.Open(filename, CFile::modeRead | CFile::shareDenyNone, &fileException) )
	{
		TRACE( _T("Can't open file %s, error = %u\n"), filename, fileException.m_cause );
	}

	CStringT<char,StrTraitMFC<char> > cmdString;
	scriptFile.Read(cmdString.GetBufferSetLength(scriptFile.GetLength()), scriptFile.GetLength());
	cmdString.ReleaseBuffer();

	XNode script;
	if ( script.Load(A2T(cmdString)) != NULL )
	{
		XNodes cmds = script.GetChilds(_T("CMD"));
		XNodes::iterator cmd = cmds.begin();
		for ( ; cmd != cmds.end(); ++cmd )
		{
			MemoryInitCommand* pCmd = (MemoryInitCommand*)(*cmd);
            SDPCmd.format = pCmd->GetFormat();
            SDPCmd.data = pCmd->GetData();
            SDPCmd.address = pCmd->GetAddress();
			if ( !WriteReg(&SDPCmd) )
            {
                TRACE("In InitMemoryDevice(): write memory failed\n");
                CloseMxHidHandle();
                return FALSE;
            }
		}
	}

    //Clear device handle and report id    
    CloseMxHidHandle();
    
	return TRUE;
}