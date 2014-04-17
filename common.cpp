#include "common.h"
#include "addrlist.h"

int g_fd;
char g_leaderinfo[MAXNAME + 20]; //keeps leader/sequencer info
int g_fdclient;
char g_name[MAXNAME];
char g_server[20];
int g_port;
struct sockaddr_in g_remaddrclient, g_myaddr;
int isLeaderChanged;
int isEOF;
int livecountForSequencer;
pthread_t g_pid_receive_thread_client;
pthread_t g_keep_alive_thread_client;
pthread_t g_fgets_thread_client;

//controller for sequencer
void SequencerController(char* recv_data, sockaddr_in addr){
	char *cmd;
	cmd = strsep(&recv_data, ":");
	//when a client joins
	//register ip, port, name etc
	//in-protocol: reg:name
	if (strcmp(cmd, "reg") == 0) {
		char listbuffer[BUFSIZE];
		char buffer[BUFSIZE];
		//first notice all other client EXCEPT the one that just joined
		//NOTICE Alice joined on 192.168.5.81:1923
		//out-protocol: msg:send_data
		sprintf(buffer, "msg:NOTICE %s joined on %s:%d\n", recv_data, inet_ntoa(addr.sin_addr), addr.sin_port);
		MultiCast(buffer);
		memset((char *)&buffer, 0, sizeof(buffer));

		//add addr to address list		
		Push(&g_alist, addr, recv_data);
		// Push(&g_alist, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), recv_data);
		
		//send upd:name1:ip1:port1:name2:ip2:port2:...:end
		GetUpdateList(buffer);
		MultiCast(buffer);

		//out-protocol: reg:clientip:clientport:userlist
		ShowListWithLeader(listbuffer);
		sprintf(buffer, "reg:%s:%d:%s", inet_ntoa(addr.sin_addr), addr.sin_port, listbuffer);
		//send a list of users to the client
		sendto(g_fd, buffer, strlen(buffer), 0, 
			(struct sockaddr *)&addr, sizeof(addr));
	}
	//msg
	//in-protocol: msg:message
	else if (strcmp(cmd, "msg") == 0) {
		//get the name
		char name[MAXNAME];
		GetNameByAddr(g_alist, addr, name);
		//form msg to send
		//out-protocol: msg:name:: MessageToMulticast
		char send_data[BUFSIZE];
		sprintf(send_data, "msg:%s:: %s", name, recv_data);
		//multicast that msg to everyone that is connected
		MultiCast(send_data);
	}
	else if (strcmp(cmd, "kpa") == 0){
		char name[MAXNAME];
		GetNameByAddr(g_alist, addr, name);
		ZeroizeLiveCount(g_alist, name);
	}
	else {
		printf("nothing\n");
	}
	return;
}

//multicast to those in client list
void MultiCast(char* msg){
	struct anode* current = g_alist;
	//multicasting for all the registered clients
	while (current != NULL) {
		//do multicast
		sendto(g_fd, msg, 
			strlen(msg), 
			0, 
			(struct sockaddr *)&(current->addr), 
			sizeof(current->addr));
		current = current->next;
	}
	//at last, sequencer gets the msg as well
	char *cmd;
	cmd = strsep(&msg, ":");
	if (strcmp(cmd, "msg") == 0)
		printf("%s", msg);
	return;
}

//get client list
void ShowListWithLeader(char* buffer){	
	char alistbuffer[BUFSIZE];
	ShowList(g_alist, alistbuffer);
	sprintf(buffer, "%s (Leader)\n%s", g_leaderinfo, alistbuffer);
	return;	
}

//get upd message for sequencer
void GetUpdateList(char* buffer){
	char line[MAXNAME + 20];
	strcpy(buffer, "upd:");
	struct anode* current = g_alist;
	while (current != NULL) {
		sprintf(line, "%s:%s:%d:", 
			current->name, 
			inet_ntoa(current->addr.sin_addr), 
			current->addr.sin_port);
		strcat(buffer, line);
		current = current->next;
	}
	strcat(buffer, "end");
	return;
}

//controller for client
void ClientController(char* recv_data, sockaddr_in recvaddr){
	char *cmd;
	cmd = strsep(&recv_data, ":");
	//response to register req, it should get user name
	//in-protocol: reg:clientip:clientport:userlist
	if (strcmp(cmd, "reg") == 0){
		char myip[20];
		char myport[10];
		cmd = strsep(&recv_data, ":");
		strcpy(myip, cmd);
		cmd = strsep(&recv_data, ":");
		strcpy(myport, cmd);
		printf("%s joining a new chat on %s:%d, listening on %s:%s\n", g_name, g_server, g_port, myip, myport);
		printf("Succeeded, current users:\n");
		printf("%s", recv_data);
	}
	//in-protocol: msg:MessageToThisClient
	else if (strcmp(cmd, "msg") == 0) {
		printf("%s", recv_data);		
	}
	else if (strcmp(cmd, "upd") == 0) {
		UpdateClientList(recv_data);
	}
	else if (strcmp(cmd, "upl") == 0) {
		strcpy(g_name, recv_data);
		isLeaderChanged = 1;
		strcpy(g_server, inet_ntoa(recvaddr.sin_addr));
		g_port = recvaddr.sin_port;
	}
	else if (strcmp(cmd, "kpa") == 0) {
		if (strcmp(recv_data, "KEEP_ALIVE") == 0){
			char alive[20];
			strcpy(alive, "kpa:ALIVE");
			socklen_t slen = sizeof(recvaddr);
			if (sendto(g_fdclient, alive, strlen(alive), 0, (struct sockaddr *)&recvaddr, slen)==-1) {
				perror("sendto");
				exit(1);
			}
			livecountForSequencer = 0;
		}
	}
	return;
}

// update client list using upd message
// name:ip:port:...:end
void UpdateClientList(char* recv_data){
	char *token;
	char name[MAXNAME], ip[20], port[10];
	//initialize the client list
	DeleteList(&g_alist);
	//add clients to the list
	while ((token = strsep(&recv_data, ":")) != NULL){
		if (strcmp(token, "end") == 0)
			break;
		strcpy(name, token);
		if ((token = strsep(&recv_data, ":")) == NULL){
			printf("error\n");
		}
		strcpy(ip, token);
		if ((token = strsep(&recv_data, ":")) == NULL){
			printf("error\n");
		}
		strcpy(port, token);
		Push(&g_alist, ip, ntohs(atoi(port)), name);
	}
}

//leader is whoever last joined in the chat
//return port number of the new leader
//todo: select alphabetically ordered leader
int LeaderElection(char* name, char* leaderName) {
	struct anode* current = g_alist;
	while (current->next != NULL) {
		current = current->next;
	}
	strcpy(leaderName, current->name);
	if (strcmp(name, current->name) == 0) {
		return htons(current->addr.sin_port);
	}
	return 0;
}