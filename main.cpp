#include <iostream>
#include <Windows.h>
#include <winscard.h>

using namespace std;

int write_msg(BYTE writeData[], BYTE block) {


	cout << "Starting.." << endl;

	DWORD dwActiveProtocol;
	DWORD dwReadersLen = 0;	// DWORD is 32 bit unsigned int
	LONG ret;	// LONG is 32 bit signed int
	LPTSTR mszReaders = nullptr; // LPTSTR is a pointer to a string
	SCARDCONTEXT hContext;	// context is like a handle between this app and the nfc reader
	SCARDHANDLE hCard;	// SCARDHANDLE is a reference to an established connection (nfc reader)

	// establish context and store it in hContext
	// (nullptrs are optional names for reader group and reader)
	ret = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &hContext);
	if (ret != SCARD_S_SUCCESS) {
		cerr << "Failed to establish context!" << endl;
		return 1;
	}
	else {
		cout << "Successfully established context." << endl;
	}

	// get list of connected smart card readers (usually 0 or 1)
	ret = SCardListReaders(hContext, nullptr, nullptr, &dwReadersLen);
	if (ret != SCARD_S_SUCCESS) {
		cerr << "Failed to get smart card reader list! Did you not \
connect your NFC reader?" << endl;
		SCardReleaseContext(hContext);
		return 1;
	}

	// dwReadersLen now holds length of reader names
	// allocates enough space for these names to be held in mszReaders
	mszReaders = new TCHAR[dwReadersLen];

	// get reader list again but now store results in mszReaders which is large enough
	ret = SCardListReaders(hContext, nullptr, mszReaders, &dwReadersLen);
	if (ret != SCARD_S_SUCCESS) {
		cerr << "Failed to get smart card reader list (2nd call)!" << endl;
		delete[] mszReaders;
		SCardReleaseContext(hContext);
		return 1;
	}

	// print available nfc readers
	wcout << "Available readers: " << endl;
	LPTSTR pReader = mszReaders;
	while (*pReader != '\0') {
		wcout << "- " << pReader << endl;
		pReader += wcslen(pReader) + 1;
	}

	// connect to first reader in list (hCard will hold the active connection)
	// dwActiveProtocol will hold the communication protocol (T0 is character-
	// oriented and used when timing is crucial, T1 is block-based and more likely to be used)

	// SCARD_SHARE_EXCLUSIVE might be an option too
	ret = SCardConnect(hContext, mszReaders, SCARD_SHARE_DIRECT,
		SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &hCard, &dwActiveProtocol);

	if (ret != SCARD_S_SUCCESS) {
		cerr << "Failed to connect to smart card reader: " << ret << endl;
		delete[] mszReaders;
		SCardReleaseContext(hContext);
		return 1;
	}
	else {
		cout << "Successfully connected to device: " << mszReaders << endl;
	}

	// store selected communication protocol in SCARD ioRequest format
	SCARD_IO_REQUEST ioRequest;
	ioRequest.cbPciLength = sizeof(SCARD_IO_REQUEST);
	ioRequest.dwProtocol = dwActiveProtocol;

	// define APDU command (check ACR122U API Documentation)
	// command below loads default key A (FF ..) for Mifare Classic 1k into location 00
	BYTE apdu_command[] = { 0xFF, 0x82, 0x00, 0x00, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	// PART 1
	// send APDU command and receive response
	BYTE apdu_response[256];	// response will be stored in byte array of size 256
	DWORD responseLength = sizeof(apdu_response);	// holds available space in response array
	ret = SCardTransmit(hCard, &ioRequest, apdu_command, sizeof(apdu_command),
		nullptr, apdu_response, &responseLength);

	// print response if command was sent successfully
	if (ret != SCARD_S_SUCCESS) {
		cerr << "\nFailed to send APDU command or receive response! Make sure to hold \
your NFC tag close to the reader before running the program.." << endl;
		delete[] mszReaders;
		SCardDisconnect(hCard, SCARD_LEAVE_CARD);
		SCardReleaseContext(hContext);
		return 1;

	}
	else {
		cout << "\nAPDU command was sent successfully!\nResponse:" << endl;
		for (DWORD i = 0; i < responseLength; ++i) {
			printf("%02X ", apdu_response[i]);
		}
		cout << "\n(If it says 90 00 above, it means everything worked)" << endl;
	}

	// PART 2
	// now use the loaded key in ACR122U to authenticate block 04 
	BYTE apdu_command2[] = { 0xFF, 0x86, 0x00, 0x00, 0x05, 0x01, 0x00, block, 0x60, 0x00 };

	// send APDU command and receive response
	BYTE apdu_response2[256];	// response will be stored in byte array of size 256
	DWORD responseLength2 = sizeof(apdu_response2);	// holds available space in response array
	ret = SCardTransmit(hCard, &ioRequest, apdu_command2, sizeof(apdu_command2),
		nullptr, apdu_response2, &responseLength2);

	// print response if command was sent successfully
	if (ret != SCARD_S_SUCCESS) {
		cerr << "\nFailed to send second APDU command or receive response!" << endl;
		delete[] mszReaders;
		SCardDisconnect(hCard, SCARD_LEAVE_CARD);
		SCardReleaseContext(hContext);
		return 1;

	}
	else {
		cout << "\nSecond APDU command was sent successfully!\nResponse:" << endl;
		for (DWORD i = 0; i < responseLength2; ++i) {
			printf("%02X ", apdu_response2[i]);
		}
		cout << "\n(If it says 90 00 above, it means everything worked)" << endl;
	}

	// BLOCK AUTHENTICATED. WRITE MSG NOW
	
	// write msg to authenticated block 4 (the first 5 bytes are specified in page 17 of acr122u api)
	DWORD writeDataLength = sizeof(writeData);
	BYTE apdu_write3[] = { 0xFF, 0xD6, 0x00, block, 0x10, static_cast<BYTE>(writeDataLength) };
	BYTE apdu_command3[sizeof(apdu_write3) + sizeof(writeData)];
	memcpy(apdu_command3, apdu_write3, sizeof(apdu_write3));
	memcpy(apdu_command3 + sizeof(apdu_write3), writeData, sizeof(writeData));

	BYTE apdu_response3[256];
	DWORD responseLength3 = sizeof(apdu_response3);

	DWORD apdu_command3_length = sizeof(apdu_write3) + sizeof(writeData);

	// Send the APDU command to the card
	ret = SCardTransmit(hCard, &ioRequest, apdu_command3, apdu_command3_length, nullptr, apdu_response3, &responseLength3);

	if (ret != SCARD_S_SUCCESS) {
		cerr << "Failed to write data. Error code: " << ret << endl;
	} else {
		cout << "\n#############\nData successfully written to NFC tag!\nResponse:" << endl;
		for (DWORD i = 0; i < responseLength3; ++i) {
			printf("%02X ", apdu_response3[i]);
		}
		cout << "\n(If it says 90 00 above, it means everything worked)" << endl;
	}

	// cleanup
	delete[] mszReaders;
	SCardDisconnect(hCard, SCARD_LEAVE_CARD);
	SCardReleaseContext(hContext);

	return 0;
}

int main() {
	BYTE writeData[] = { 'h', 'e', 'l', 'l', 'o', '3', '4', '5', '\0' };  // enter msg here
	BYTE block = 0x01;		// enter which block to write to here

	// checks whether msg length is allowed
	int msg_length = sizeof(writeData);
	if (msg_length > 9) {
		cerr << "Message is too long to be stored in a single block!" << endl;
		return 1;
	}

	// checks whether target block is allowed on mifare classic 1k
	bool allowed = false;
	BYTE possible_blocks[] = { 0x01, 0x02, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0A,
							   0x0C, 0x0D, 0x0E, 0x10, 0x11, 0x12, 0x14, 0x15,
							   0x16, 0x18, 0x19, 0x1A, 0x1C, 0x1D, 0x1E, 0x20, 
							   0x21, 0x22, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2A,
							   0x2C, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x34, 0x35,
							   0x36, 0x38, 0x39, 0x3A, 0x3C, 0x3D, 0x3E};
	for (int i = 0; i < sizeof(possible_blocks); ++i) {
		if (possible_blocks[i] == block) {
			allowed = true;
			break;
		}
	}
	if (!allowed) {
		cerr << "Invalid block target!" << endl;
		return 1;
	}

	write_msg(writeData, block);

	return 0;
}
