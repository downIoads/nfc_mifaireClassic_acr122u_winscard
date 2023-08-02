// thanks @gentilkiwi for reviewing my code and providing the base construct for the improved version!
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winscard.h>

typedef struct _SCARD_DUAL_HANDLE {
	SCARDCONTEXT hContext;
	SCARDHANDLE hCard;
} SCARD_DUAL_HANDLE, * PSCARD_DUAL_HANDLE;


// allowed block values for safely writing data (assuming non-magic tag)
const BYTE allowedBlocks[] = {  0x01, 0x02, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0A,
								0x0C, 0x0D, 0x0E, 0x10, 0x11, 0x12, 0x14, 0x15,
								0x16, 0x18, 0x19, 0x1A, 0x1C, 0x1D, 0x1E, 0x20,
								0x21, 0x22, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2A,
								0x2C, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x34, 0x35,
								0x36, 0x38, 0x39, 0x3A, 0x3C, 0x3D, 0x3E };


void PrintHex(LPCBYTE pbData, DWORD cbData)
{
	DWORD i;
	for (i = 0; i < cbData; i++)
	{
		wprintf(L"%02x ", pbData[i]);
	}
	wprintf(L"\n");
}

BOOL SendRecvReader(PSCARD_DUAL_HANDLE pHandle, const BYTE* pbData, const UINT16 cbData, BYTE* pbResult, UINT16* pcbResult)
{
	BOOL status = FALSE;
	DWORD cbRecvLenght = *pcbResult;
	LONG scStatus;

	wprintf(L"> ");
	PrintHex(pbData, cbData);

	scStatus = SCardTransmit(pHandle->hCard, NULL, pbData, cbData, NULL, pbResult, &cbRecvLenght);
	if (scStatus == SCARD_S_SUCCESS)
	{
		*pcbResult = (UINT16)cbRecvLenght;

		wprintf(L"< ");
		PrintHex(pbResult, *pcbResult);

		status = TRUE;
	}
	else wprintf(L"%08x\n", scStatus);

	return status;
}

BOOL OpenReader(LPCWSTR szReaderName, PSCARD_DUAL_HANDLE pHandle)
{
	BOOL status = FALSE;
	LONG scStatus;
	DWORD dwActiveProtocol;

	scStatus = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &pHandle->hContext);
	if (scStatus == SCARD_S_SUCCESS)
	{
		scStatus = SCardConnect(pHandle->hContext, szReaderName, SCARD_SHARE_SHARED, SCARD_PROTOCOL_Tx, &pHandle->hCard, &dwActiveProtocol);
		if (scStatus == SCARD_S_SUCCESS)
		{
			status = TRUE;
		}
		else
		{
			SCardReleaseContext(pHandle->hContext);
		}
	}

	return status;
}

void CloseReader(PSCARD_DUAL_HANDLE pHandle)
{
	SCardDisconnect(pHandle->hCard, SCARD_LEAVE_CARD);
	SCardReleaseContext(pHandle->hContext);
}

// combine two byte arrays into one
void CombineArrays(const BYTE* arr1, UINT16 arr1Length, const BYTE* arr2, UINT16 arr2Length, BYTE* combinedArray) {
	// Copy data from arr1 to combinedArray
	for (UINT16 i = 0; i < arr1Length; i++) {
		combinedArray[i] = arr1[i];
	}

	// Append data from arr2 to combinedArray
	for (UINT16 i = 0; i < arr2Length; i++) {
		combinedArray[arr1Length + i] = arr2[i];
	}
}

int WriteToTag(const BYTE* Msg, UINT16 msgLength, BYTE block) {
	bool allowedBlock = false;
	for (int i = 0; i < sizeof(allowedBlocks); ++i) {
		if (allowedBlocks[i] == block) {
			allowedBlock = true;
			break;
		}
	}
	if (!allowedBlock) {
		wprintf(L"Target block is invalid.\n");
		return 1;
	}

	// TODO: this might be the error (if instead of characters there is binary data passed). so instead just pass msgLength to the function
	// UINT16 msgLength = strlen((const char*)Msg); 

	if (msgLength > 17) {
		wprintf(L"Your message is too long! Only 16 bytes allowed (excluding mandatory NULL).\n");
		return 1;
	}

	const BYTE APDU_Write_Base[] = { 0xff, 0xd6, 0x00, block, 0x10 };
	UINT16 apduWriteBaseLength = sizeof(APDU_Write_Base) / sizeof(APDU_Write_Base[0]);

	// calculate size of resulting array (after combining APDU_Write_Base and Msg)
	UINT16 apduWrite_Length = apduWriteBaseLength + msgLength;

	// create new array that will hold the merged data
	BYTE* APDU_Write = (BYTE*)malloc(apduWrite_Length * sizeof(APDU_Write_Base[0]));

	// combine arrays APDU_Write_Base and Msg and store in APDU_Write
	CombineArrays(APDU_Write_Base, apduWriteBaseLength, Msg, msgLength, APDU_Write);


	const BYTE APDU_LoadDefaultKey[] = { 0xff, 0x82, 0x00, 0x00, 0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	const BYTE APDU_Authenticate_Block[] = { 0xff, 0x86, 0x00, 0x00, 0x05, 0x01, 0x00, block, 0x60, 0x00 };

	SCARD_DUAL_HANDLE hDual;
	BYTE Buffer[32];
	UINT16 cbBuffer;	// usually will be 2 (e.g. response 90 00 for success)

	// preparations are complete
	printf("Writing Hex:\n");
	for (UINT16 i = 5; i < apduWrite_Length; ++i) {		// first few chars are not part of message that will be written so skip printing them
		printf("0x%02X ", APDU_Write[i]);
	}
	printf("\n");


	// my Laptop:	"ACS ACR122U PICC Interface 0"
	// my PC:		"ACS ACR122 0"
	if (OpenReader(L"ACS ACR122 0", &hDual))
	{

		cbBuffer = 2;
		if (SendRecvReader(&hDual, APDU_LoadDefaultKey, sizeof(APDU_LoadDefaultKey), Buffer, &cbBuffer))
		{
			wprintf(L"Default Key A has been loaded.\n");
		}
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0x90 && Buffer[1] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			free(APDU_Write);
			return 1;
		}

		cbBuffer = 2;
		if (SendRecvReader(&hDual, APDU_Authenticate_Block, sizeof(APDU_Authenticate_Block), Buffer, &cbBuffer))
		{
			wprintf(L"Block has been authenticated.\n");
		}
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0x90 && Buffer[1] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			free(APDU_Write);
			return 1;
		}

		cbBuffer = 2;
		if (SendRecvReader(&hDual, APDU_Write, apduWrite_Length, Buffer, &cbBuffer))
		{
			wprintf(L"Data has successfully been written to the block!\n");
		}
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0x90 && Buffer[1] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			free(APDU_Write);
			return 1;
		}


		CloseReader(&hDual);
	}
	else {
		wprintf(L"Failed to find NFC reader.\n");
	}

	free(APDU_Write);

	return 0;
}

int ReadFromTag(BYTE block) {
	
	const BYTE APDU_Read[] = { 0xff, 0xb0, 0x00, block, 0x10 };	// page 16 of ACR122U_APIDriverManual.pdf (reads 16 bytes, and the page number is a coincidence)
	UINT16 apduReadLength = sizeof(APDU_Read) / sizeof(APDU_Read[0]);


	const BYTE APDU_LoadDefaultKey[] = { 0xff, 0x82, 0x00, 0x00, 0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	const BYTE APDU_Authenticate_Block[] = { 0xff, 0x86, 0x00, 0x00, 0x05, 0x01, 0x00, block, 0x60, 0x00 };

	SCARD_DUAL_HANDLE hDual;
	BYTE Buffer[18];    // return status (success 90 00 or failure 63 00) takes 2 bytes, received data takes 16 bytes
	UINT16 cbBuffer;	// usually will be 2 (e.g. response 90 00 for success)


	// my Laptop:	"ACS ACR122U PICC Interface 0"
	// my PC:		"ACS ACR122 0"
	if (OpenReader(L"ACS ACR122 0", &hDual))
	{

		cbBuffer = 2;
		if (SendRecvReader(&hDual, APDU_LoadDefaultKey, sizeof(APDU_LoadDefaultKey), Buffer, &cbBuffer))
		{
			wprintf(L"Default Key A has been loaded.\n");
		}
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0x90 && Buffer[1] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}

		cbBuffer = 2;
		if (SendRecvReader(&hDual, APDU_Authenticate_Block, sizeof(APDU_Authenticate_Block), Buffer, &cbBuffer))
		{
			wprintf(L"Block has been authenticated.\n");
		}
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0x90 && Buffer[1] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}

		cbBuffer = 18;
		if (SendRecvReader(&hDual, APDU_Read, apduReadLength, Buffer, &cbBuffer))
		{
			// PrintHex(Buffer, sizeof(Buffer));
			// check for success (here the success code is stored after the data read, so read the last two bytes of the buffer)
			if (!(Buffer[16] == 0x90 && Buffer[17] == 0x00)) {
				CloseReader(&hDual);
				wprintf(L"Error code received. Aborting..\n");
				return 1;
			}
			
		}
		else {
			wprintf(L"Failed to read block.");
			CloseReader(&hDual);
			return 1;
		}

		wprintf(L"Successfully read block.\nTrying to print read data in human-readable form:\n");
		for (int i = 0; i < 16; i++) {		// any text would have ended with \n so dont read last Byte
			printf("%c ", Buffer[i]);
		}
		printf("\n");

		CloseReader(&hDual);
	}
	else {
		wprintf(L"Failed to find NFC reader.\n");
	}

	return 0;
}

// todo complete this function (not ready yet)
/*
int ResetCardContents() {
	const BYTE Msg[] = { 0x00 , 0x00 , 0x00, 0x00 , 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 , '\0' };
	
	// get size of message
	UINT16 msgSize = sizeof(Msg) / sizeof(Msg[0]);

	// get size of global allowedBlocks array (amount of elements)
	UINT16 arraySize = sizeof(allowedBlocks) / sizeof(allowedBlocks[0]);

	for (UINT16 i = 0; i < arraySize; ++i) {
		UINT16 status_code = WriteToTag(Msg, msgSize, allowedBlocks[i]);
		if (status_code != 0) {
			printf("Error occured while writing! Terminating.");
			return 1;
		}
	}

	printf("\nSuccessfully reset card contents.\n");
	return 0;
}
*/

int main() {
	const BYTE Msg[] = { 0x00 }; // why does writing to a block work for the array below but not for this one? i have tried with \0 but no success
	// const BYTE Msg[] = { 'a' , 'b' , 'c', 'd' , 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', '\0' };	// msg to write
	BYTE block = 0x34;	// target block for write

	// ----------------------
	// determine size of array now (because you cant do that later on from a pointer)
	UINT16 msgSize = sizeof(Msg) / sizeof(Msg[0]);
	// ----------------------

	// choose function to run (uncomment):
	WriteToTag(Msg, msgSize, block);
	// ReadFromTag(block);
	// ResetCardContents();

	return 0;
}
